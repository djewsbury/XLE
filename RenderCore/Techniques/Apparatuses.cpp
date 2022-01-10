// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "Apparatuses.h"
#include "Services.h"
#include "CompiledShaderPatchCollection.h"
#include "Techniques.h"
#include "TechniqueDelegates.h"
#include "PipelineAccelerator.h"
#include "DeformAccelerator.h"
#include "ImmediateDrawables.h"
#include "RenderPass.h"
#include "SubFrameEvents.h"
#include "SimpleModelDeform.h"
#include "SkinDeformer.h"
#include "CommonResources.h"
#include "SystemUniformsDelegate.h"
#include "Drawables.h"
#include "PipelineCollection.h"
#include "PipelineOperators.h"
#include "../Assets/TextureCompiler.h"
#include "../Assets/PredefinedPipelineLayout.h"
#include "../Assets/PipelineConfigurationUtils.h"
#include "../Assets/MaterialCompiler.h"
#include "../Assets/MergedAnimationSetCompiler.h"
#include "../IDevice.h"
#include "../MinimalShaderSource.h"
#include "../ShaderService.h"
#include "../Vulkan/IDeviceVulkan.h"
#include "../../ShaderParser/AutomaticSelectorFiltering.h"
#include "../../RenderOverlays/FontRendering.h"
#include "../../BufferUploads/IBufferUploads.h"
#include "../../Assets/Assets.h"
#include "../../Assets/Marker.h"
#include "../../Assets/IntermediateCompilers.h"
#include "../../Assets/CompileAndAsyncManager.h"
#include "../../Utility/Profiling/CPUProfiler.h"
#include "../../xleres/FileList.h"
#include <regex>

namespace RenderCore { namespace Techniques
{
	static std::shared_ptr<RenderCore::ILowLevelCompiler> CreateDefaultShaderCompiler(RenderCore::IDevice& device, const LegacyRegisterBindingDesc& legacyRegisterBinding);

	DrawingApparatus::DrawingApparatus(std::shared_ptr<IDevice> device)
	{
		_depValPtr = ::Assets::GetDepValSys().Make();
		_legacyRegisterBindingDesc = std::make_shared<LegacyRegisterBindingDesc>(RenderCore::Assets::CreateDefaultLegacyRegisterBindingDesc());

		_device = device;
		_shaderCompiler = CreateDefaultShaderCompiler(*device, *_legacyRegisterBindingDesc);
		_shaderSource = std::make_shared<MinimalShaderSource>(_shaderCompiler);
		_shaderService = std::make_unique<ShaderService>();
		_shaderService->SetShaderSource(_shaderSource);
		
		auto& compilers = ::Assets::Services::GetAsyncMan().GetIntermediateCompilers();
		_shaderFilteringRegistration = ShaderSourceParser::RegisterShaderSelectorFilteringCompiler(compilers);
		_shaderCompilerRegistration = RegisterShaderCompiler(_shaderSource, compilers);
		_graphShaderCompiler2Registration = RegisterInstantiateShaderGraphCompiler(_shaderSource, compilers);

		_commonResources = std::make_shared<CommonResourceBox>(*_device);
		_drawablesPacketsPool = std::make_shared<DrawablesPacketPool>();

		auto pipelineLayoutFileFuture = ::Assets::MakeAssetPtr<RenderCore::Assets::PredefinedPipelineLayoutFile>(MAIN_PIPELINE);
		pipelineLayoutFileFuture->StallWhilePending();
		_pipelineLayoutFile = pipelineLayoutFileFuture->Actualize();
		_depValPtr.RegisterDependency(_pipelineLayoutFile->GetDependencyValidation());

		auto descSetLayoutFuture = ::Assets::MakeAssetPtr<RenderCore::Assets::PredefinedPipelineLayoutFile>(SEQUENCER_DS);
		descSetLayoutFuture->StallWhilePending();
		auto descSetLayoutContainer = descSetLayoutFuture->Actualize();
		auto i = descSetLayoutContainer->_descriptorSets.find("Sequencer");
		if (i == descSetLayoutContainer->_descriptorSets.end())
			Throw(std::runtime_error("Missing 'Sequencer' descriptor set entry in sequencer pipeline file"));
		_sequencerDescSetLayout = i->second;
		_depValPtr.RegisterDependency(descSetLayoutContainer->GetDependencyValidation());

		const std::string pipelineLayoutName = "GraphicsMain";
		auto pipelineInit = RenderCore::Assets::PredefinedPipelineLayout(*_pipelineLayoutFile, pipelineLayoutName).MakePipelineLayoutInitializer(_shaderCompiler->GetShaderLanguage(), &_commonResources->_samplerPool);
		_compiledPipelineLayout = device->CreatePipelineLayout(pipelineInit);

		PipelineAcceleratorPoolFlags::BitField poolFlags = 0;
		_pipelineAccelerators = CreatePipelineAcceleratorPool(
			device,
			FindLayout(*_pipelineLayoutFile, pipelineLayoutName, "Material"),
			poolFlags);
		
		_systemUniformsDelegate = std::make_shared<SystemUniformsDelegate>(*_device);

		_graphicsPipelinePool = std::make_shared<PipelineCollection>(_device);

		if (!_techniqueServices)
			_techniqueServices = std::make_shared<Services>(_device);
		_techniqueServices->SetCommonResources(_commonResources);

		auto& subFrameEvents = _techniqueServices->GetSubFrameEvents();
		_frameBarrierBinding = subFrameEvents._onFrameBarrier.Bind(
			[pa=std::weak_ptr<IPipelineAcceleratorPool>{_pipelineAccelerators}]() {
				auto l = pa.lock();
				if (l) l->RebuildAllOutOfDatePipelines();
			});

		_onCheckCompleteInitialization = subFrameEvents._onCheckCompleteInitialization.Bind(
			[cr=std::weak_ptr<CommonResourceBox>{_commonResources}](IThreadContext& threadContext) {
				auto l = cr.lock();
				if (l) l->CompleteInitialization(threadContext);
			});

		assert(_assetServices != nullptr);

		_mainUniformDelegateManager = CreateUniformDelegateManager();
		_mainUniformDelegateManager->AddSemiConstantDescriptorSet(Hash64("Sequencer"), *_sequencerDescSetLayout, *_device);
		_mainUniformDelegateManager->AddShaderResourceDelegate(_systemUniformsDelegate);
	}

	DrawingApparatus::~DrawingApparatus()
	{
		auto& subFrameEvents = _techniqueServices->GetSubFrameEvents();
		subFrameEvents._onCheckCompleteInitialization.Unbind(_onCheckCompleteInitialization);
		subFrameEvents._onFrameBarrier.Unbind(_frameBarrierBinding);
	}

	std::shared_ptr<RenderCore::ILowLevelCompiler> CreateDefaultShaderCompiler(RenderCore::IDevice& device, const LegacyRegisterBindingDesc& legacyRegisterBinding)
	{
		auto* vulkanDevice  = (RenderCore::IDeviceVulkan*)device.QueryInterface(typeid(RenderCore::IDeviceVulkan).hash_code());
		if (vulkanDevice) {
			// Vulkan allows for multiple ways for compiling shaders. The tests currently use a HLSL to GLSL to SPIRV 
			// cross compilation approach
			RenderCore::VulkanCompilerConfiguration cfg;
			cfg._shaderMode = RenderCore::VulkanShaderMode::HLSLToSPIRV;
			cfg._legacyBindings = legacyRegisterBinding;
		 	return vulkanDevice->CreateShaderCompiler(cfg);
		} else {
			return device.CreateShaderCompiler();
		}
	}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////	
		//   I M M E D I A T E   D R A W I N G   //
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	ImmediateDrawingApparatus::ImmediateDrawingApparatus(std::shared_ptr<DrawingApparatus> mainDrawingApparatus)
	{
		_depValPtr = ::Assets::GetDepValSys().Make();

		_mainDrawingApparatus = std::move(mainDrawingApparatus);
		_depValPtr.RegisterDependency(_mainDrawingApparatus->GetDependencyValidation());
		
		_immediateDrawables =  RenderCore::Techniques::CreateImmediateDrawables(_mainDrawingApparatus->_device);
		_fontRenderingManager = std::make_shared<RenderOverlays::FontRenderingManager>(*_mainDrawingApparatus->_device);

		auto& subFrameEvents = _techniqueServices->GetSubFrameEvents();
		_frameBarrierBinding = subFrameEvents._onFrameBarrier.Bind(
			[im=std::weak_ptr<IImmediateDrawables>{_immediateDrawables}]() {
				auto l = im.lock();
				if (l) l->OnFrameBarrier();
			});
	}
	
	ImmediateDrawingApparatus::~ImmediateDrawingApparatus()
	{
		auto& subFrameEvents = _techniqueServices->GetSubFrameEvents();
		subFrameEvents._onFrameBarrier.Unbind(_frameBarrierBinding);
	}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////	
		//   P R I M A R Y   R E S O U R C E S   //
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	PrimaryResourcesApparatus::PrimaryResourcesApparatus(std::shared_ptr<IDevice> device)
	{
		if (!_techniqueServices)
			_techniqueServices = std::make_shared<Services>(device);

		_bufferUploads = BufferUploads::CreateManager(*device);
		_techniqueServices->SetBufferUploads(_bufferUploads);

		_techniqueServices->RegisterTextureLoader(std::regex{R"(.*\.[dD][dD][sS])"}, RenderCore::Assets::CreateDDSTextureLoader());
		_techniqueServices->RegisterTextureLoader(std::regex{R"(.*\.[hH][dD][rR])"}, RenderCore::Assets::CreateHDRTextureLoader());
		_techniqueServices->SetFallbackTextureLoader(RenderCore::Assets::CreateWICTextureLoader());

		auto pipelineCollection = std::make_shared<Techniques::PipelineCollection>(device);
		_techniqueServices->GetDeformOperationFactorySet().Register("skin", CreateGPUSkinDeformerFactory(pipelineCollection));
		_techniqueServices->GetDeformOperationFactorySet().Register("cpu_skin", CreateCPUSkinDeformerFactory());
		_deformAccelerators = CreateDeformAcceleratorPool(device);

		auto& compilers = ::Assets::Services::GetAsyncMan().GetIntermediateCompilers();
		_materialCompilerRegistration = RenderCore::Assets::RegisterMaterialCompiler(compilers);
		_mergedAnimSetCompilerRegistration = RenderCore::Assets::RegisterMergedAnimationSetCompiler(compilers);
		_modelCompilers = ::Assets::DiscoverCompileOperations(compilers, "*Conversion.dll");

		_textureCompilerRegistration = RenderCore::Assets::RegisterTextureCompiler(compilers);
		
		auto& subFrameEvents = _techniqueServices->GetSubFrameEvents();
		_prePresentBinding = subFrameEvents._onPrePresent.Bind(
			[bu=_bufferUploads](RenderCore::IThreadContext& context) {
				bu->Update(context);
			});

		_frameBarrierBinding = subFrameEvents._onFrameBarrier.Bind(
			[weakDeformAccelerators=std::weak_ptr<IDeformAcceleratorPool>{_deformAccelerators}]() {
				::Assets::Services::GetAssetSets().OnFrameBarrier();

				auto da=weakDeformAccelerators.lock();
				if (da) da->OnFrameBarrier();
			});

		assert(_assetServices != nullptr);
	}

	PrimaryResourcesApparatus::~PrimaryResourcesApparatus()
	{
		auto& subFrameEvents = _techniqueServices->GetSubFrameEvents();
		subFrameEvents._onFrameBarrier.Unbind(_frameBarrierBinding);
		subFrameEvents._onPrePresent.Unbind(_prePresentBinding);
	}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////	
		//   F R A M E   R E N D E R I N G   //
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	FrameRenderingApparatus::FrameRenderingApparatus(std::shared_ptr<IDevice> device)
	{
		_attachmentPool = std::make_shared<RenderCore::Techniques::AttachmentPool>(device);
		_frameBufferPool = RenderCore::Techniques::CreateFrameBufferPool();
		_frameCPUProfiler = std::make_shared<Utility::HierarchicalCPUProfiler>();
	}

	FrameRenderingApparatus::~FrameRenderingApparatus()
	{

	}

	std::shared_ptr<SubFrameEvents> FrameRenderingApparatus::GetSubFrameEvents()
	{
		return Services::GetSubFrameEventsPtr();
	}

}}

