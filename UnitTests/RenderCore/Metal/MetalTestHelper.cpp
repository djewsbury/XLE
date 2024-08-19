// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "MetalTestHelper.h"
#include "../../../RenderCore/Metal/FrameBuffer.h"
#include "../../../RenderCore/Metal/DeviceContext.h"
#include "../../../RenderCore/Metal/ObjectFactory.h"
#include "../../../RenderCore/Metal/Resource.h"
#include "../../../RenderCore/IDevice.h"
#include "../../../RenderCore/IAnnotator.h"
#include "../../../RenderCore/OpenGLES/IDeviceOpenGLES.h"
#include "../../../RenderCore/Vulkan/IDeviceVulkan.h"
#include "../../../RenderCore/MinimalShaderSource.h"
#include "../../../RenderCore/ResourceUtils.h"
#include "../../../Assets/AssetUtils.h"
#include "../../../Assets/DepVal.h"
#include "../../../Assets/AssetServices.h"
#include "../../../Assets/AssetSetManager.h"
#include "../../../Formatters/TextFormatter.h"
#include "../../../Formatters/StreamDOM.h"
#include "../../../Utility/Streams/SerializationUtils.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "../../../Foreign/stb/stb_image_write.h"
#include <filesystem>
#include <fstream>

#if GFXAPI_TARGET == GFXAPI_DX11
	#include "../../../RenderCore/Metal/State.h"
#endif

using namespace Utility::Literals;

namespace UnitTests
{
	static std::shared_ptr<RenderCore::ICompiledPipelineLayout> CreateDefaultPipelineLayout(RenderCore::IDevice& device);
	static std::shared_ptr<RenderCore::LegacyRegisterBindingDesc> CreateDefaultLegacyRegisterBindingDesc();
	
	RenderCore::CompiledShaderByteCode MetalTestHelper::MakeShader(StringSection<> shader, StringSection<> shaderModel, StringSection<> defines)
	{
		return UnitTests::MakeShader(_shaderSource, shader, shaderModel, defines);
	}

	RenderCore::Metal::ShaderProgram MetalTestHelper::MakeShaderProgram(StringSection<> vs, StringSection<> ps)
	{
		return UnitTests::MakeShaderProgram(_shaderSource, _pipelineLayout, vs, ps);
	}

	MetalTestHelper::MetalTestHelper(RenderCore::UnderlyingAPI api)
	{
		// Basically every test needs to use dep vals; so let's ensure the dep val sys exists here
		if (!_depValSys)
			_depValSys = ::Assets::CreateDepValSys();

		auto instance = RenderCore::CreateAPIInstance(api);
		_device = instance->CreateDevice(0, instance->QueryFeatureCapability(0));

		// For GLES, we must initialize the root context to something. Since we're not going to be
		// rendering to window for unit tests, we will never create a PresentationChain (during which the
		// device implicitly initializes the root context in the normal flow)
		if (auto* glesDevice = query_interface_cast<RenderCore::IDeviceOpenGLES*>(_device.get()))
			glesDevice->InitializeRootContextHeadless();

		_defaultLegacyBindings = CreateDefaultLegacyRegisterBindingDesc();
		_pipelineLayout = CreateDefaultPipelineLayout(*_device);

		_shaderCompiler = CreateDefaultShaderCompiler(*_device, *_defaultLegacyBindings);
		_shaderSource = RenderCore::CreateMinimalShaderSource(_shaderCompiler);
	}

	MetalTestHelper::MetalTestHelper(const std::shared_ptr<RenderCore::IDevice>& device)
	{
		// Basically every test needs to use dep vals; so let's ensure the dep val sys exists here
		if (!_depValSys)
			_depValSys = ::Assets::CreateDepValSys();

		_device = device;

		_defaultLegacyBindings = CreateDefaultLegacyRegisterBindingDesc();
		_pipelineLayout = CreateDefaultPipelineLayout(*_device);

		_shaderCompiler = _device->CreateShaderCompiler();
		_shaderSource = RenderCore::CreateMinimalShaderSource(_shaderCompiler);
	}

	MetalTestHelper::~MetalTestHelper()
	{
		if (auto assetSets = ::Assets::Services::GetAssetSetsPtr())
			assetSets->Clear();
		_pipelineLayout.reset();
		_shaderSource.reset();
		_device.reset();
	}

	void MetalTestHelper::BeginFrameCapture()
	{
		_device->GetImmediateContext()->GetAnnotator().BeginFrameCapture();
	}
	void MetalTestHelper::EndFrameCapture()
	{
		auto immediateContext = _device->GetImmediateContext();
		if (immediateContext->GetAnnotator().IsCaptureToolAttached()) {
			immediateContext->CommitCommands();
			immediateContext->GetAnnotator().EndFrameCapture();
		}
	}

	std::unique_ptr<MetalTestHelper> MakeTestHelper()
	{
		#if GFXAPI_TARGET == GFXAPI_APPLEMETAL
			return std::make_unique<MetalTestHelper>(RenderCore::UnderlyingAPI::AppleMetal);
		#elif GFXAPI_TARGET == GFXAPI_OPENGLES
			return std::make_unique<MetalTestHelper>(RenderCore::UnderlyingAPI::OpenGLES);
		#elif GFXAPI_TARGET == GFXAPI_VULKAN
			return std::make_unique<MetalTestHelper>(RenderCore::UnderlyingAPI::Vulkan);
		#elif GFXAPI_TARGET == GFXAPI_DX11
			auto res = std::make_unique<MetalTestHelper>(RenderCore::UnderlyingAPI::DX11);
			// hack -- required for D3D11 currently
			auto metalContext = RenderCore::Metal::DeviceContext::Get(*res->_device->GetImmediateContext());
			metalContext->Bind(RenderCore::Metal::RasterizerState{RenderCore::CullMode::None});
			return res;
		#else
			#error GFX-API not handled in MakeTestHelper()
		#endif
	}

	std::shared_ptr<RenderCore::ILowLevelCompiler> CreateDefaultShaderCompiler(RenderCore::IDevice& device, const RenderCore::LegacyRegisterBindingDesc& registerBindings)
	{
		if (auto* vulkanDevice  = query_interface_cast<RenderCore::IDeviceVulkan*>(&device)) {
			// Vulkan allows for multiple ways for compiling shaders. The tests currently use a HLSL to GLSL to SPIRV 
			// cross compilation approach
			RenderCore::VulkanCompilerConfiguration cfg;
			cfg._shaderMode = RenderCore::VulkanShaderMode::HLSLToSPIRV;
			cfg._legacyBindings = registerBindings;
		 	return vulkanDevice->CreateShaderCompiler(cfg);
		} else {
			return device.CreateShaderCompiler();
		}
	}

	RenderCore::ShaderCompileResourceName::CompilationFlags::BitField GetDefaultShaderCompilationFlags(RenderCore::IDevice& device)
	{
		#if defined(_DEBUG)
			if (device.GetImmediateContext()->GetAnnotator().IsCaptureToolAttached())
				return RenderCore::ShaderCompileResourceName::CompilationFlags::DebugSymbols | RenderCore::ShaderCompileResourceName::CompilationFlags::DisableOptimizations;
		#endif
		return 0;
	}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////


	class UnitTestFBHelper::Pimpl : public RenderCore::INamedAttachments
	{
	public:
		std::vector<std::shared_ptr<RenderCore::IResource>> _targets;
		std::vector<RenderCore::ResourceDesc> _targetDescs;
		std::shared_ptr<RenderCore::Metal::FrameBuffer> _fb;
		RenderCore::FrameBufferDesc _fbDesc;
		RenderCore::ViewPool _srvPool;

		std::shared_ptr<RenderCore::IResourceView> GetResourceView(
			RenderCore::AttachmentName resName, 
			RenderCore::BindFlag::Enum bindFlag, RenderCore::TextureViewDesc viewDesc,
			const RenderCore::AttachmentDesc& requestDesc,
			const RenderCore::FrameBufferProperties& props) override
		{
			assert(resName <= _targets.size());
			// the "requestDesc" is passed in here so that we can validate it. We're expecting
			// it to match up to the desc that was provided in the FrameBufferDesc
			assert(requestDesc._format == _targetDescs[resName]._textureDesc._format);
			return _srvPool.GetTextureView(_targets[resName], bindFlag, viewDesc);
		}
	};

	class RenderPassToken : public UnitTestFBHelper::IRenderPassToken
	{
	public:
		std::shared_ptr<RenderCore::Metal::DeviceContext> _devContext;
		std::shared_ptr<RenderCore::Metal::FrameBuffer> _fb;

		RenderPassToken(
			const std::shared_ptr<RenderCore::Metal::DeviceContext>& devContext, 
			const std::shared_ptr<RenderCore::Metal::FrameBuffer>& fb)
		: _devContext(devContext), _fb(fb) {}

		~RenderPassToken()
		{
			_devContext->EndRenderPass();
		}
	};

	auto UnitTestFBHelper::BeginRenderPass(RenderCore::IThreadContext& threadContext, IteratorRange<const RenderCore::ClearValue*> clearValues) -> std::shared_ptr<IRenderPassToken>
	{
		auto devContext = RenderCore::Metal::DeviceContext::Get(threadContext);
		devContext->BeginRenderPass(*_pimpl->_fb, clearValues);
		return std::make_shared<RenderPassToken>(devContext, _pimpl->_fb);
	}

	std::map<unsigned, unsigned> GetFullColorBreakdown(RenderCore::IThreadContext& threadContext, RenderCore::IResource& resource)
	{
		std::map<unsigned, unsigned> result;

		auto data = resource.ReadBackSynchronized(threadContext);

		assert(data.size() == (size_t)RenderCore::ByteCount(resource.GetDesc()));
		auto pixels = MakeIteratorRange((unsigned*)AsPointer(data.begin()), (unsigned*)AsPointer(data.end()));
		for (auto p:pixels) ++result[p];

		return result;
	}

	std::map<unsigned, unsigned> UnitTestFBHelper::GetFullColorBreakdown(RenderCore::IThreadContext& threadContext)
	{
		return UnitTests::GetFullColorBreakdown(threadContext, *_pimpl->_targets[0]);
	}

	void UnitTestFBHelper::SaveImage(RenderCore::IThreadContext& threadContext, StringSection<> filename) const
	{
		UnitTests::SaveImage(threadContext, *_pimpl->_targets[0], filename);
	}

	void SaveImage(RenderCore::IThreadContext& threadContext, RenderCore::IResource& resource, StringSection<> filename)
	{
		using namespace RenderCore;
		{
			Metal::BarrierHelper barrierHelper(threadContext);
			barrierHelper.Add(resource, BindFlag::RenderTarget, BindFlag::TransferSrc);
		}

		auto desc = resource.GetDesc();
		auto data = resource.ReadBackSynchronized(threadContext);
		if (RenderCore::GetCompressionType(desc._textureDesc._format) != RenderCore::FormatCompressionType::None
			|| RenderCore::GetComponentPrecision(desc._textureDesc._format) != 8) {
			auto outputName = std::filesystem::temp_directory_path() / "xle-unit-tests" / (filename.AsString() + ".bin");
			std::fstream outf{outputName, std::ios::out|std::ios::binary|std::ios::trunc};
			outf.write((const char*)AsPointer(data.begin()), data.size());
			return;
		}

		auto components = RenderCore::GetComponents(desc._textureDesc._format);
		unsigned compCount = 0;
		switch (components) {
		case RenderCore::FormatComponents::Alpha:
		case RenderCore::FormatComponents::Luminance:
			compCount = 1;
			break;
		case RenderCore::FormatComponents::LuminanceAlpha:
		case RenderCore::FormatComponents::RG:
			compCount = 2;
			break;
		case RenderCore::FormatComponents::RGB:
			compCount = 3;
			break;
		case RenderCore::FormatComponents::RGBAlpha:
			compCount = 4;
			break;
		default:
			Throw(std::runtime_error("Component type not supported for image output"));
		}

		auto outputName = std::filesystem::temp_directory_path() / "xle-unit-tests" / (filename.AsString() + ".png");
		
		if (compCount == 4) {
			// nuke alpha channel for RGBA outputs
			uint8_t* bytes = (uint8_t*)AsPointer(data.begin());
			while( bytes != AsPointer(data.end())) { bytes[3] = 0xff; bytes += 4; }
		}

		auto res = stbi_write_png(
			outputName.string().c_str(),
			desc._textureDesc._width, desc._textureDesc._height,
			compCount, 
			AsPointer(data.begin()),
			int(data.size() / desc._textureDesc._height));
		assert(res != 0);
	}

	std::shared_ptr<RenderCore::IResource> UnitTestFBHelper::GetMainTarget() const
	{
		return _pimpl->_targets[0];
	}

	const RenderCore::FrameBufferDesc& UnitTestFBHelper::GetDesc() const
	{
		return _pimpl->_fbDesc;
	}

	RenderCore::ViewportDesc UnitTestFBHelper::GetDefaultViewport() const
	{
		return _pimpl->_fb->GetDefaultViewport();
	}

	UnitTestFBHelper::UnitTestFBHelper(
		RenderCore::IDevice& device, 
		RenderCore::IThreadContext& threadContext,
		const RenderCore::ResourceDesc& mainTargetDesc,
		RenderCore::LoadStore beginLoadStore)
	{
		using namespace RenderCore;
		_pimpl = std::make_unique<UnitTestFBHelper::Pimpl>();

		// Create a resource that matches the given desc, and then also create
		// a framebuffer with a single subpass rendering into that resource;
		_pimpl->_targets.emplace_back(device.CreateResource(mainTargetDesc, "unit-test-fb"));
		_pimpl->_targetDescs.emplace_back(mainTargetDesc);

		AttachmentDesc mainAttachment { mainTargetDesc._textureDesc._format };
		mainAttachment._loadFromPreviousPhase = beginLoadStore;
		SubpassDesc mainSubpass;
		mainSubpass.AppendOutput(0);
		mainSubpass.SetName("unit-test-subpass");
		_pimpl->_fbDesc = FrameBufferDesc { 
			std::vector<AttachmentDesc>{ mainAttachment },
			std::vector<SubpassDesc>{ mainSubpass } };

		_pimpl->_fb = std::make_shared<RenderCore::Metal::FrameBuffer>(
			Metal::GetObjectFactory(device),
			_pimpl->_fbDesc,
			*_pimpl);

		Metal::CompleteInitialization(*Metal::DeviceContext::Get(threadContext), {_pimpl->_targets[0].get()});
	}

	UnitTestFBHelper::UnitTestFBHelper(
		RenderCore::IDevice& device, 
		RenderCore::IThreadContext& threadContext,
		const RenderCore::ResourceDesc& target0Desc,
		const RenderCore::ResourceDesc& target1Desc,
		const RenderCore::ResourceDesc& target2Desc)
	{
		using namespace RenderCore;
		_pimpl = std::make_unique<UnitTestFBHelper::Pimpl>();

		// Create a resource that matches the given desc, and then also create
		// a framebuffer with a single subpass rendering into that resource;
		_pimpl->_targets.emplace_back(device.CreateResource(target0Desc, "unit-test-fb0"));
		_pimpl->_targets.emplace_back(device.CreateResource(target1Desc, "unit-test-fb1"));
		_pimpl->_targets.emplace_back(device.CreateResource(target2Desc, "unit-test-fb2"));
		_pimpl->_targetDescs.emplace_back(target0Desc);
		_pimpl->_targetDescs.emplace_back(target1Desc);
		_pimpl->_targetDescs.emplace_back(target2Desc);

		std::vector<AttachmentDesc> attachments;
		attachments.emplace_back(AttachmentDesc{target0Desc._textureDesc._format, 0, LoadStore::Clear});
		attachments.emplace_back(AttachmentDesc{target1Desc._textureDesc._format, 0, LoadStore::Clear});
		attachments.emplace_back(AttachmentDesc{target2Desc._textureDesc._format, 0, LoadStore::Clear});
		SubpassDesc mainSubpass;
		mainSubpass.AppendOutput(0);
		mainSubpass.AppendOutput(1);
		mainSubpass.AppendOutput(2);
		mainSubpass.SetName("unit-test-subpass");
		_pimpl->_fbDesc = FrameBufferDesc { 
			std::move(attachments),
			std::vector<SubpassDesc>{ mainSubpass } };

		_pimpl->_fb = std::make_shared<RenderCore::Metal::FrameBuffer>(
			Metal::GetObjectFactory(device),
			_pimpl->_fbDesc,
			*_pimpl);

		Metal::CompleteInitialization(*Metal::DeviceContext::Get(threadContext), {_pimpl->_targets[0].get(), _pimpl->_targets[1].get(), _pimpl->_targets[2].get()});
	}

	UnitTestFBHelper::UnitTestFBHelper(
		RenderCore::IDevice& device,
		RenderCore::IThreadContext& threadContext)
	{
		// This constructs a frame buffer with 1 subpass, but no attachments
		// It's useful for stream output cases
		using namespace RenderCore;
		_pimpl = std::make_unique<UnitTestFBHelper::Pimpl>();

		SubpassDesc mainSubpass;
		mainSubpass.SetName("stream-output-subpass");
		_pimpl->_fbDesc = FrameBufferDesc { {}, std::vector<SubpassDesc>{ mainSubpass } };
		_pimpl->_fb = std::make_shared<RenderCore::Metal::FrameBuffer>(
			Metal::GetObjectFactory(device),
			_pimpl->_fbDesc,
			*_pimpl);
	}

	UnitTestFBHelper::~UnitTestFBHelper()
	{
	}

	class DescriptorSetHelper::Pimpl
	{
	public:
		std::vector<std::shared_ptr<RenderCore::IResourceView>> _resources;
		std::vector<std::shared_ptr<RenderCore::ISampler>> _samplers;
		std::vector<RenderCore::DescriptorSetInitializer::BindTypeAndIdx> _slotBindings;
	};

	void DescriptorSetHelper::Bind(unsigned descriptorSetSlot, const std::shared_ptr<RenderCore::IResourceView>& res)
	{
		auto i = std::find_if(_pimpl->_slotBindings.begin(), _pimpl->_slotBindings.end(), [descriptorSetSlot](const auto& l) { return l._descriptorSetSlot == descriptorSetSlot; });
		if (i == _pimpl->_slotBindings.end()) {
			_pimpl->_slotBindings.push_back({});
			i = _pimpl->_slotBindings.end()-1;
			i->_descriptorSetSlot = descriptorSetSlot;
		}

		i->_type = RenderCore::DescriptorSetInitializer::BindType::ResourceView;
		i->_uniformsStreamIdx = (unsigned)_pimpl->_resources.size();
		_pimpl->_resources.push_back(res);
	}
	
	void DescriptorSetHelper::Bind(unsigned descriptorSetSlot, const std::shared_ptr<RenderCore::ISampler>& sampler)
	{
		auto i = std::find_if(_pimpl->_slotBindings.begin(), _pimpl->_slotBindings.end(), [descriptorSetSlot](const auto& l) { return l._descriptorSetSlot == descriptorSetSlot; });
		if (i == _pimpl->_slotBindings.end()) {
			_pimpl->_slotBindings.push_back({});
			i = _pimpl->_slotBindings.end()-1;
			i->_descriptorSetSlot = descriptorSetSlot;
		}

		i->_type = RenderCore::DescriptorSetInitializer::BindType::Sampler;
		i->_uniformsStreamIdx = (unsigned)_pimpl->_samplers.size();
		_pimpl->_samplers.push_back(sampler);
	}

	std::shared_ptr<RenderCore::IDescriptorSet> DescriptorSetHelper::CreateDescriptorSet(
		RenderCore::IDevice& device,
		const RenderCore::DescriptorSetSignature& signature,
		RenderCore::PipelineType pipelineType)
	{
		RenderCore::DescriptorSetInitializer init;

		VLA(RenderCore::IResourceView*, resViews, _pimpl->_resources.size());
		VLA(RenderCore::ISampler*, samplers, _pimpl->_samplers.size());
		for (unsigned c=0; c<_pimpl->_resources.size(); ++c) resViews[c] = _pimpl->_resources[c].get();
		for (unsigned c=0; c<_pimpl->_samplers.size(); ++c) samplers[c] = _pimpl->_samplers[c].get();

		init._bindItems._resourceViews = MakeIteratorRange(resViews, resViews+_pimpl->_resources.size());
		init._bindItems._samplers = MakeIteratorRange(samplers, samplers+_pimpl->_samplers.size());

		auto result = device.CreateDescriptorSet(pipelineType, signature, "unittest");
		result->Write(init);
		return result;
	}

	DescriptorSetHelper::DescriptorSetHelper()
	{
		_pimpl = std::make_unique<Pimpl>();
	}

	DescriptorSetHelper::~DescriptorSetHelper()
	{}

////////////////////////////////////////////////////////////////////////////////////////////////////
			//    U T I L I T Y    F N S

	RenderCore::CompiledShaderByteCode MakeShader(const std::shared_ptr<RenderCore::IShaderSource>& shaderSource, StringSection<> shader, StringSection<> shaderModel, StringSection<> defines)
	{
		auto codeBlob = shaderSource->CompileFromMemory(shader, "main", shaderModel, defines);
		if (!codeBlob._payload || codeBlob._payload->empty()) {
			std::cout << "Shader compile failed with errors: " << ::Assets::AsString(codeBlob._errors) << std::endl;
			assert(0);
		}
		return RenderCore::CompiledShaderByteCode {
			codeBlob._payload,
			::Assets::GetDepValSys().Make(MakeIteratorRange(codeBlob._deps)),
			{}
		};
	}

	RenderCore::Metal::ShaderProgram MakeShaderProgram(
        const std::shared_ptr<RenderCore::IShaderSource>& shaderSource,
        const std::shared_ptr<RenderCore::ICompiledPipelineLayout>& pipelineLayout,
        StringSection<> vs, StringSection<> ps)
	{
		return RenderCore::Metal::ShaderProgram(RenderCore::Metal::GetObjectFactory(), pipelineLayout, MakeShader(shaderSource, vs, "vs_*"), MakeShader(shaderSource, ps, "ps_*"));
	}
	
	std::shared_ptr<RenderCore::IResource> MetalTestHelper::CreateVB(IteratorRange<const void*> data)
	{
		using namespace RenderCore;
		// note -- inefficient use of HostVisibleSequentialWrite, not recommended for use outside of unit tests
		return _device->CreateResource(
			CreateDesc(
				BindFlag::VertexBuffer,
				AllocationRules::HostVisibleSequentialWrite,
				LinearBufferDesc::Create((unsigned)data.size())),
			"vertex-buffer",
			SubResourceInitData { data });
	}

	std::shared_ptr<RenderCore::IResource> MetalTestHelper::CreateIB(IteratorRange<const void*> data)
	{
		using namespace RenderCore;
		// note -- inefficient use of HostVisibleSequentialWrite, not recommended for use outside of unit tests
		return _device->CreateResource(
			CreateDesc(
				BindFlag::IndexBuffer,
				AllocationRules::HostVisibleSequentialWrite,
				LinearBufferDesc::Create((unsigned)data.size())),
			"index-buffer",
			SubResourceInitData { data });
	}

	std::shared_ptr<RenderCore::IResource> MetalTestHelper::CreateCB(IteratorRange<const void*> data)
	{
		using namespace RenderCore;
		// note -- inefficient use of HostVisibleSequentialWrite, not recommended for use outside of unit tests
		return _device->CreateResource(
			CreateDesc(
				BindFlag::ConstantBuffer,
				AllocationRules::HostVisibleSequentialWrite,
				LinearBufferDesc::Create((unsigned)data.size())),
			"constant-buffer",
			SubResourceInitData { data });
	}

	std::shared_ptr<RenderCore::ICompiledPipelineLayout> CreateDefaultPipelineLayout(RenderCore::IDevice& device)
	{
		using namespace RenderCore;
		RenderCore::DescriptorSetSignature sequencerSet {
			{DescriptorType::UniformBuffer},				// 0
			{DescriptorType::UniformBuffer},				// 1
			{DescriptorType::UniformBuffer},				// 2
			{DescriptorType::UniformBuffer},				// 3
			{DescriptorType::UniformBuffer},				// 4
			{DescriptorType::UniformBuffer},				// 5

			{DescriptorType::SampledTexture},				// 6
			{DescriptorType::SampledTexture},				// 7
			{DescriptorType::SampledTexture},				// 8
			{DescriptorType::SampledTexture},				// 9
			{DescriptorType::SampledTexture},				// 10

			{DescriptorType::Sampler},						// 11
			{DescriptorType::Sampler},						// 12
			{DescriptorType::Sampler},						// 13
			{DescriptorType::Sampler}						// 14
		};

		sequencerSet._fixedSamplers.resize(11, 0);
		sequencerSet._fixedSamplers.push_back(device.CreateSampler(SamplerDesc{FilterMode::Trilinear, AddressMode::Wrap, AddressMode::Wrap}));
		// sequencerSet._fixedSamplers.push_back(device.CreateSampler(SamplerDesc{FilterMode::Trilinear, AddressMode::Clamp, AddressMode::Clamp}));
		sequencerSet._fixedSamplers.push_back(device.CreateSampler(SamplerDesc{FilterMode::Bilinear, AddressMode::Clamp, AddressMode::Clamp}));
		sequencerSet._fixedSamplers.push_back(device.CreateSampler(SamplerDesc{FilterMode::Bilinear, AddressMode::Clamp, AddressMode::Clamp, AddressMode::Clamp, CompareOp::Never, SamplerDescFlags::UnnormalizedCoordinates}));
		sequencerSet._fixedSamplers.push_back(device.CreateSampler(SamplerDesc{FilterMode::Point, AddressMode::Clamp, AddressMode::Clamp}));

		RenderCore::DescriptorSetSignature materialSet {
			{DescriptorType::UniformBuffer},

			{DescriptorType::SampledTexture},
			{DescriptorType::SampledTexture},
			{DescriptorType::SampledTexture},
			{DescriptorType::SampledTexture},
			{DescriptorType::SampledTexture},

			{DescriptorType::Sampler}
		};

		RenderCore::DescriptorSetSignature numericSet {
			{DescriptorType::SampledTexture},				// 0
			{DescriptorType::SampledTexture},				// 1
			{DescriptorType::InputAttachment},				// 2

			{DescriptorType::UniformBuffer},				// 3
			{DescriptorType::UniformBuffer},				// 4

			{DescriptorType::Sampler},						// 5

			{DescriptorType::InputAttachment},				// 6
		};

		RenderCore::PipelineLayoutInitializer desc;
		desc.AppendDescriptorSet("Numeric", numericSet, RenderCore::PipelineType::Graphics);
		desc.AppendDescriptorSet("Sequencer", sequencerSet, RenderCore::PipelineType::Graphics);
		desc.AppendDescriptorSet("Material", materialSet, RenderCore::PipelineType::Graphics);
		desc.AppendPushConstants("LocalTransform", 64, ShaderStage::Vertex);
		return device.CreatePipelineLayout(desc, "unittest");
	}

	std::shared_ptr<RenderCore::LegacyRegisterBindingDesc> CreateDefaultLegacyRegisterBindingDesc()
	{
		using namespace RenderCore;
		using RegisterType = LegacyRegisterBindingDesc::RegisterType;
		using RegisterQualifier = LegacyRegisterBindingDesc::RegisterQualifier;
		using Entry = LegacyRegisterBindingDesc::Entry;
		auto result = std::make_shared<LegacyRegisterBindingDesc>();
		result->AppendEntry(
			RegisterType::ShaderResource, RegisterQualifier::None,
			Entry{0, 3, "Numeric"_h, 2, 0, 3});
		result->AppendEntry(
			RegisterType::ConstantBuffer, RegisterQualifier::None,
			Entry{3, 4, "Numeric"_h, 2, 3, 4});

		result->AppendEntry(
			RegisterType::Sampler, RegisterQualifier::None,
			Entry{16, 17, "Numeric"_h, 3, 4, 5});				// HLSL dummy sampler
		return result;
	}
}
