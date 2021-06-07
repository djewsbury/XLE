// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "Apparatuses.h"
#include "Services.h"
#include "CompiledShaderPatchCollection.h"
#include "Techniques.h"
#include "TechniqueDelegates.h"
#include "PipelineAccelerator.h"
#include "ImmediateDrawables.h"
#include "RenderPass.h"
#include "SubFrameEvents.h"
#include "SimpleModelDeform.h"
#include "SkinDeformer.h"
#include "CommonResources.h"
#include "SystemUniformsDelegate.h"
#include "Drawables.h"
#include "../Assets/PredefinedPipelineLayout.h"
#include "../Assets/PipelineConfigurationUtils.h"
#include "../Assets/MaterialCompiler.h"
#include "../IDevice.h"
#include "../MinimalShaderSource.h"
#include "../ShaderService.h"
#include "../Vulkan/IDeviceVulkan.h"
#include "../../ShaderParser/AutomaticSelectorFiltering.h"
#include "../../RenderOverlays/FontRendering.h"
#include "../../BufferUploads/IBufferUploads.h"
#include "../../Assets/Assets.h"
#include "../../Assets/AssetFuture.h"
#include "../../Assets/IntermediateCompilers.h"
#include "../../Assets/CompileAndAsyncManager.h"
#include "../../Assets/CompilerLibrary.h"
#include "../../Utility/Profiling/CPUProfiler.h"
#include "../../xleres/FileList.h"
#include "thousandeyes/futures/DefaultExecutor.h"
#include "thousandeyes/futures/Default.h"
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

		auto techniqueSetFile = ::Assets::MakeAsset<Techniques::TechniqueSetFile>(ILLUM_TECH);
		_techniqueSharedResources = Techniques::CreateTechniqueSharedResources(*device);
		_techniqueDelegateDeferred = Techniques::CreateTechniqueDelegate_Deferred(techniqueSetFile, _techniqueSharedResources);

		auto pipelineLayoutFileFuture = ::Assets::MakeAsset<RenderCore::Assets::PredefinedPipelineLayoutFile>(MAIN_PIPELINE);
		pipelineLayoutFileFuture->StallWhilePending();
		_pipelineLayoutFile = pipelineLayoutFileFuture->Actualize();
		_depValPtr.RegisterDependency(_pipelineLayoutFile->GetDependencyValidation());

		const std::string pipelineLayoutName = "GraphicsMain";
		auto i = _pipelineLayoutFile->_pipelineLayouts.find(pipelineLayoutName);
		if (i == _pipelineLayoutFile->_pipelineLayouts.end())
			Throw(std::runtime_error("Did not find pipeline layout with the name " + pipelineLayoutName + " in the given pipeline layout file"));
		auto pipelineInit = i->second->MakePipelineLayoutInitializer(_shaderCompiler->GetShaderLanguage());
		_compiledPipelineLayout = device->CreatePipelineLayout(pipelineInit);

		PipelineAcceleratorPoolFlags::BitField poolFlags = 0;
		_pipelineAccelerators = CreatePipelineAcceleratorPool(
			device,
			_compiledPipelineLayout,
			poolFlags,
			FindLayout(*_pipelineLayoutFile, pipelineLayoutName, "Material"),
			FindLayout(*_pipelineLayoutFile, pipelineLayoutName, "Sequencer"));

		_commonResources = std::make_shared<CommonResourceBox>(*_device);
		_drawablesSharedResources = CreateDrawablesSharedResources();
		_systemUniformsDelegate = std::make_shared<SystemUniformsDelegate>(*_device, *_commonResources);

		if (!_techniqueServices)
			_techniqueServices = std::make_shared<Services>(_device);
		assert(_assetServices != nullptr);
	}

	DrawingApparatus::~DrawingApparatus()
	{
		auto& compilers = ::Assets::Services::GetAsyncMan().GetIntermediateCompilers();
		compilers.DeregisterCompiler(_graphShaderCompiler2Registration._registrationId);
		compilers.DeregisterCompiler(_shaderCompilerRegistration._registrationId);
		compilers.DeregisterCompiler(_shaderFilteringRegistration._registrationId);
	}

	std::shared_ptr<RenderCore::ILowLevelCompiler> CreateDefaultShaderCompiler(RenderCore::IDevice& device, const LegacyRegisterBindingDesc& legacyRegisterBinding)
	{
		auto* vulkanDevice  = (RenderCore::IDeviceVulkan*)device.QueryInterface(typeid(RenderCore::IDeviceVulkan).hash_code());
		if (vulkanDevice) {
			// Vulkan allows for multiple ways for compiling shaders. The tests currently use a HLSL to GLSL to SPIRV 
			// cross compilation approach
			RenderCore::VulkanCompilerConfiguration cfg;
			cfg._shaderMode = RenderCore::VulkanShaderMode::HLSLCrossCompiled;
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
		
		auto pipelineLayoutFuture = ::Assets::MakeAsset<RenderCore::Assets::PredefinedPipelineLayoutFile>(IMMEDIATE_PIPELINE);
		pipelineLayoutFuture->StallWhilePending();
		_pipelineLayoutFile = pipelineLayoutFuture->Actualize();
		_depValPtr.RegisterDependency(_pipelineLayoutFile->GetDependencyValidation());

		const std::string pipelineLayoutName = "ImmediateDrawables";
		auto i = _pipelineLayoutFile->_pipelineLayouts.find(pipelineLayoutName);
		if (i == _pipelineLayoutFile->_pipelineLayouts.end())
			Throw(std::runtime_error("Did not find pipeline layout with the name " + pipelineLayoutName + " in the given pipeline layout file"));
		auto pipelineInit = i->second->MakePipelineLayoutInitializer(_mainDrawingApparatus->_shaderCompiler->GetShaderLanguage());
		_compiledPipelineLayout = _mainDrawingApparatus->_device->CreatePipelineLayout(pipelineInit);

		_immediateDrawables =  RenderCore::Techniques::CreateImmediateDrawables(
			_mainDrawingApparatus->_device, 
			_compiledPipelineLayout,
			RenderCore::Techniques::FindLayout(*_pipelineLayoutFile, pipelineLayoutName, "Material"),
			RenderCore::Techniques::FindLayout(*_pipelineLayoutFile, pipelineLayoutName, "Sequencer"));

		_fontRenderingManager = std::make_shared<RenderOverlays::FontRenderingManager>(*_mainDrawingApparatus->_device);
	}
	
	ImmediateDrawingApparatus::~ImmediateDrawingApparatus()
	{
	}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////	
		//   P R I M A R Y   R E S O U R C E S   //
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	class PrimaryResourcesApparatus::ContinuationExecutor
	{
	public:
		std::shared_ptr<thousandeyes::futures::DefaultExecutor> _continuationExecutor;
		thousandeyes::futures::Default<thousandeyes::futures::Executor>::Setter _continuationExecutorSetter;

		ContinuationExecutor()
		: _continuationExecutor(std::make_shared<thousandeyes::futures::DefaultExecutor>(std::chrono::milliseconds(2)))
		, _continuationExecutorSetter(_continuationExecutor)
		{
		}
	};

	PrimaryResourcesApparatus::PrimaryResourcesApparatus(std::shared_ptr<IDevice> device)
	{
		if (!_techniqueServices)
			_techniqueServices = std::make_shared<Services>(device);

		_continuationExecutor = std::make_unique<ContinuationExecutor>();
		_bufferUploads = BufferUploads::CreateManager(*device);
		_techniqueServices->SetBufferUploads(_bufferUploads);

		_techniqueServices->RegisterTextureLoader(std::regex{R"(.*\.[dD][dD][sS])"}, Techniques::CreateDDSTextureLoader());
		_techniqueServices->RegisterTextureLoader(std::regex{R"(.*)"}, Techniques::CreateWICTextureLoader());

		_techniqueServices->GetDeformOperationFactory().RegisterDeformOperation("skin", SkinDeformer::InstantiationFunction);

		auto& compilers = ::Assets::Services::GetAsyncMan().GetIntermediateCompilers();
		_materialCompilerRegistration = RenderCore::Assets::RegisterMaterialCompiler(compilers);
		_modelCompilers = ::Assets::DiscoverCompileOperations(compilers, "*Conversion.dll");

		_subFrameEvents = std::make_shared<SubFrameEvents>();
		
		_subFrameEvents->_onPrePresent.Bind(
			[](RenderCore::IThreadContext& context) {
				RenderCore::Techniques::Services::GetBufferUploads().Update(context);
			});

		_subFrameEvents->_onFrameBarrier.Bind(
			[]() {
				::Assets::Services::GetAsyncMan().Update();
				::Assets::Services::GetAssetSets().OnFrameBarrier();
			});

		assert(_assetServices != nullptr);
	}

	PrimaryResourcesApparatus::~PrimaryResourcesApparatus()
	{
		auto& compilers = ::Assets::Services::GetAsyncMan().GetIntermediateCompilers();
		compilers.DeregisterCompiler(_materialCompilerRegistration._registrationId);
		for (const auto&m:_modelCompilers)
			compilers.DeregisterCompiler(m);
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

}}

