// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "MetalTestHelper.h"
#include "../../../RenderCore/Metal/FrameBuffer.h"
#include "../../../RenderCore/Metal/DeviceContext.h"
#include "../../../RenderCore/Metal/ObjectFactory.h"
#include "../../../RenderCore/Metal/Resource.h"
#include "../../../RenderCore/IDevice.h"
#include "../../../RenderCore/IThreadContext.h"
#include "../../../RenderCore/IAnnotator.h"
#include "../../../RenderCore/OpenGLES/IDeviceOpenGLES.h"
#include "../../../RenderCore/Vulkan/IDeviceVulkan.h"
#include "../../../RenderCore/MinimalShaderSource.h"
#include "../../../RenderCore/ResourceUtils.h"
#include "../../../Assets/AssetUtils.h"
#include "../../../Assets/DepVal.h"
#include "../../../Utility/Streams/StreamFormatter.h"
#include "../../../Utility/Streams/StreamDOM.h"
#include "../../../Utility/Streams/SerializationUtils.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "../../../Foreign/stb/stb_image_write.h"
#include <filesystem>

#if GFXAPI_TARGET == GFXAPI_DX11
	#include "../../../RenderCore/Metal/State.h"
#endif

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

		_device = RenderCore::CreateDevice(api);

		// For GLES, we must initialize the root context to something. Since we're not going to be
		// rendering to window for unit tests, we will never create a PresentationChain (during which the
		// device implicitly initializes the root context in the normal flow)
		auto* glesDevice = (RenderCore::IDeviceOpenGLES*)_device->QueryInterface(typeid(RenderCore::IDeviceOpenGLES).hash_code());
		if (glesDevice)
			glesDevice->InitializeRootContextHeadless();

		_device->GetImmediateContext();	// on Vulkan this will cause an early "second stage init"
		
		_defaultLegacyBindings = CreateDefaultLegacyRegisterBindingDesc();
		_pipelineLayout = CreateDefaultPipelineLayout(*_device);

		_shaderCompiler = CreateDefaultShaderCompiler(*_device, *_defaultLegacyBindings);
		_shaderService = std::make_unique<RenderCore::ShaderService>();
		_shaderSource = std::make_shared<RenderCore::MinimalShaderSource>(_shaderCompiler);
		_shaderService->SetShaderSource(_shaderSource);
	}

	MetalTestHelper::MetalTestHelper(const std::shared_ptr<RenderCore::IDevice>& device)
	{
		_device = device;

		_shaderService = std::make_unique<RenderCore::ShaderService>();
		_shaderCompiler = _device->CreateShaderCompiler();
		_shaderSource = std::make_shared<RenderCore::MinimalShaderSource>(_shaderCompiler);
		_shaderService->SetShaderSource(_shaderSource);
	}

	MetalTestHelper::~MetalTestHelper()
	{
		_pipelineLayout.reset();
		_shaderSource.reset();
		_shaderService.reset();
		_device.reset();
	}

	void MetalTestHelper::BeginFrameCapture()
	{
		_device->GetImmediateContext()->GetAnnotator().BeginFrameCapture();
	}
	void MetalTestHelper::EndFrameCapture()
	{
		_device->GetImmediateContext()->GetAnnotator().EndFrameCapture();
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
		auto* vulkanDevice  = (RenderCore::IDeviceVulkan*)device.QueryInterface(typeid(RenderCore::IDeviceVulkan).hash_code());
		if (vulkanDevice) {
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

//////////////////////////////////////////////////////////////////////////////////////////////////////////////


	class UnitTestFBHelper::Pimpl : public RenderCore::INamedAttachments
	{
	public:
		std::shared_ptr<RenderCore::IResource> _mainTarget;
		RenderCore::ResourceDesc _originalMainTargetDesc;
		std::shared_ptr<RenderCore::Metal::FrameBuffer> _fb;
		RenderCore::FrameBufferDesc _fbDesc;

		RenderCore::IResourcePtr GetResource(
			RenderCore::AttachmentName resName, 
			const RenderCore::AttachmentDesc& requestDesc,
			const RenderCore::FrameBufferProperties& props) const override
		{
			assert(resName == 0);
			// the "requestDesc" is passed in here so that we can validate it. We're expecting
			// it to match up to the desc that was provided in the FrameBufferDesc
			assert(requestDesc._format == _originalMainTargetDesc._textureDesc._format);
			return _mainTarget;
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

	auto UnitTestFBHelper::BeginRenderPass(RenderCore::IThreadContext& threadContext) -> std::shared_ptr<IRenderPassToken>
	{
		auto devContext = RenderCore::Metal::DeviceContext::Get(threadContext);
		devContext->BeginRenderPass(*_pimpl->_fb);
		return std::make_shared<RenderPassToken>(devContext, _pimpl->_fb);
	}

	std::map<unsigned, unsigned> UnitTestFBHelper::GetFullColorBreakdown(RenderCore::IThreadContext& threadContext)
	{
		std::map<unsigned, unsigned> result;

		auto data = _pimpl->_mainTarget->ReadBackSynchronized(threadContext);

		assert(data.size() == (size_t)RenderCore::ByteCount(_pimpl->_mainTarget->GetDesc()));
		auto pixels = MakeIteratorRange((unsigned*)AsPointer(data.begin()), (unsigned*)AsPointer(data.end()));
		for (auto p:pixels) ++result[p];

		return result;
	}

	void UnitTestFBHelper::SaveImage(RenderCore::IThreadContext& threadContext, StringSection<> filename) const
	{
		UnitTests::SaveImage(threadContext, *_pimpl->_mainTarget, filename);
	}

	void SaveImage(RenderCore::IThreadContext& threadContext, RenderCore::IResource& resource, StringSection<> filename)
	{
		auto desc = resource.GetDesc();
		if (RenderCore::GetCompressionType(desc._textureDesc._format) != RenderCore::FormatCompressionType::None
			|| RenderCore::GetComponentPrecision(desc._textureDesc._format) != 8)
			Throw(std::runtime_error("Cannot output image in compressed or high precision format"));
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
		
		auto data = resource.ReadBackSynchronized(threadContext);
		
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
			data.size() / desc._textureDesc._height);
		assert(res != 0);
	}

	const std::shared_ptr<RenderCore::IResource> UnitTestFBHelper::GetMainTarget() const
	{
		return _pimpl->_mainTarget;
	}

	const RenderCore::FrameBufferDesc& UnitTestFBHelper::GetDesc() const
	{
		return _pimpl->_fbDesc;
	}

	UnitTestFBHelper::UnitTestFBHelper(
		RenderCore::IDevice& device, 
		RenderCore::IThreadContext& threadContext,
		const RenderCore::ResourceDesc& mainFBDesc,
		RenderCore::LoadStore beginLoadStore)
	{
		using namespace RenderCore;
		_pimpl = std::make_unique<UnitTestFBHelper::Pimpl>();

		// Create a resource that matches the given desc, and then also create
		// a framebuffer with a single subpass rendering into that resource;
		// std::vector<uint8_t> initBuffer(RenderCore::ByteCount(mainFBDesc), 0xdd);
		// SubResourceInitData initData { MakeIteratorRange(initBuffer), MakeTexturePitches(mainFBDesc._textureDesc) };
		SubResourceInitData initData{};
		_pimpl->_mainTarget = device.CreateResource(mainFBDesc, initData);
		_pimpl->_originalMainTargetDesc = mainFBDesc;

		AttachmentDesc mainAttachment { mainFBDesc._textureDesc._format };
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

		Metal::CompleteInitialization(*Metal::DeviceContext::Get(threadContext), {_pimpl->_mainTarget.get()});
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
		if (_pimpl->_slotBindings.size() <= descriptorSetSlot)
			_pimpl->_slotBindings.resize(descriptorSetSlot+1, {});

		_pimpl->_slotBindings[descriptorSetSlot]._type = RenderCore::DescriptorSetInitializer::BindType::ResourceView;
		_pimpl->_slotBindings[descriptorSetSlot]._uniformsStreamIdx = (unsigned)_pimpl->_resources.size();
		_pimpl->_resources.push_back(res);
	}
	
	void DescriptorSetHelper::Bind(unsigned descriptorSetSlot, const std::shared_ptr<RenderCore::ISampler>& sampler)
	{
		if (_pimpl->_slotBindings.size() <= descriptorSetSlot)
			_pimpl->_slotBindings.resize(descriptorSetSlot+1, {});

		_pimpl->_slotBindings[descriptorSetSlot]._type = RenderCore::DescriptorSetInitializer::BindType::Sampler;
		_pimpl->_slotBindings[descriptorSetSlot]._uniformsStreamIdx = (unsigned)_pimpl->_samplers.size();
		_pimpl->_samplers.push_back(sampler);
	}

	std::shared_ptr<RenderCore::IDescriptorSet> DescriptorSetHelper::CreateDescriptorSet(
		RenderCore::IDevice& device,
		const RenderCore::DescriptorSetSignature& signature)
	{
		RenderCore::DescriptorSetInitializer init;
		init._slotBindings = _pimpl->_slotBindings;
		init._signature = &signature;

		RenderCore::IResourceView* resViews[_pimpl->_resources.size()];
		RenderCore::ISampler* samplers[_pimpl->_samplers.size()];
		for (unsigned c=0; c<_pimpl->_resources.size(); ++c) resViews[c] = _pimpl->_resources[c].get();
		for (unsigned c=0; c<_pimpl->_samplers.size(); ++c) samplers[c] = _pimpl->_samplers[c].get();

		init._bindItems._resourceViews = MakeIteratorRange(resViews, resViews+_pimpl->_resources.size());
		init._bindItems._samplers = MakeIteratorRange(samplers, samplers+_pimpl->_samplers.size());

		return device.CreateDescriptorSet(init);
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
		return _device->CreateResource(
			CreateDesc(
				BindFlag::VertexBuffer, 0, GPUAccess::Read,
				LinearBufferDesc::Create((unsigned)data.size()),
				"vertex-buffer"),
			SubResourceInitData { data });
	}

	std::shared_ptr<RenderCore::IResource> MetalTestHelper::CreateIB(IteratorRange<const void*> data)
	{
		using namespace RenderCore;
		return _device->CreateResource(
			CreateDesc(
				BindFlag::IndexBuffer, 0, GPUAccess::Read,
				LinearBufferDesc::Create((unsigned)data.size()),
				"index-buffer"),
			SubResourceInitData { data });
	}

	std::shared_ptr<RenderCore::IResource> MetalTestHelper::CreateCB(IteratorRange<const void*> data)
	{
		using namespace RenderCore;
		return _device->CreateResource(
			CreateDesc(
				BindFlag::ConstantBuffer, 0, GPUAccess::Read,
				LinearBufferDesc::Create((unsigned)data.size()),
				"constant-buffer"),
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
			{DescriptorType::SampledTexture},				// 11
			{DescriptorType::SampledTexture},				// 12

			{DescriptorType::Sampler},						// 13
			{DescriptorType::Sampler},						// 14
			{DescriptorType::Sampler},						// 15
			{DescriptorType::Sampler}						// 16
		};

		sequencerSet._fixedSamplers.resize(13, 0);
		sequencerSet._fixedSamplers.push_back(device.CreateSampler(SamplerDesc{FilterMode::Trilinear, AddressMode::Wrap, AddressMode::Wrap}));
		// sequencerSet._fixedSamplers.push_back(device.CreateSampler(SamplerDesc{FilterMode::Trilinear, AddressMode::Clamp, AddressMode::Clamp}));
		sequencerSet._fixedSamplers.push_back(device.CreateSampler(SamplerDesc{FilterMode::Bilinear, AddressMode::Clamp, AddressMode::Clamp}));
		sequencerSet._fixedSamplers.push_back(device.CreateSampler(SamplerDesc{FilterMode::Bilinear, AddressMode::Clamp, AddressMode::Clamp, CompareOp::Never, SamplerDescFlags::UnnormalizedCoordinates}));
		sequencerSet._fixedSamplers.push_back(device.CreateSampler(SamplerDesc{FilterMode::Point, AddressMode::Clamp, AddressMode::Clamp}));

		RenderCore::DescriptorSetSignature materialSet {
			{DescriptorType::UniformBuffer},
			{DescriptorType::UniformBuffer},
			{DescriptorType::UniformBuffer},

			{DescriptorType::SampledTexture},
			{DescriptorType::SampledTexture},
			{DescriptorType::SampledTexture},
			{DescriptorType::SampledTexture},
			{DescriptorType::SampledTexture},
			{DescriptorType::SampledTexture},
			{DescriptorType::SampledTexture},
			{DescriptorType::SampledTexture},

			{DescriptorType::UnorderedAccessBuffer},

			{DescriptorType::Sampler}
		};

		RenderCore::DescriptorSetSignature drawSet {
			{DescriptorType::UniformBuffer},
			{DescriptorType::SampledTexture}
		};

		RenderCore::DescriptorSetSignature numericSet {
			{DescriptorType::SampledTexture},				// 0
			{DescriptorType::SampledTexture},				// 1
			{DescriptorType::SampledTexture},				// 2
			{DescriptorType::SampledTexture},				// 3
			{DescriptorType::SampledTexture},				// 4
			{DescriptorType::SampledTexture},				// 5
			{DescriptorType::SampledTexture},				// 6
			{DescriptorType::SampledTexture},				// 7
			{DescriptorType::SampledTexture},				// 8
			{DescriptorType::SampledTexture},				// 9
			{DescriptorType::SampledTexture},				// 10
			{DescriptorType::SampledTexture},				// 11
			{DescriptorType::SampledTexture},				// 12
			{DescriptorType::SampledTexture},				// 13
			{DescriptorType::SampledTexture},				// 14

			{DescriptorType::Sampler},						// 15
			{DescriptorType::Sampler},						// 16
			{DescriptorType::Sampler},						// 17

			{DescriptorType::UniformBuffer},				// 18
			{DescriptorType::UniformBuffer},				// 19
			{DescriptorType::UniformBuffer},				// 20
			{DescriptorType::UniformBuffer},				// 21

			{DescriptorType::Sampler}						// 22
		};

		RenderCore::PipelineLayoutInitializer desc;
		desc.AppendDescriptorSet("Sequencer", sequencerSet, RenderCore::PipelineType::Graphics);
		desc.AppendDescriptorSet("Material", materialSet, RenderCore::PipelineType::Graphics);
		desc.AppendDescriptorSet("Draw", drawSet, RenderCore::PipelineType::Graphics);
		desc.AppendDescriptorSet("Numeric", numericSet, RenderCore::PipelineType::Graphics);
		return device.CreatePipelineLayout(desc);
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
			Entry{16, 23, Hash64("Sequencer"), 0, 6, 13});
		result->AppendEntry(
			RegisterType::Sampler, RegisterQualifier::None,
			Entry{0, 4, Hash64("Sequencer"), 0, 13, 17});
		result->AppendEntry(
			RegisterType::ConstantBuffer, RegisterQualifier::None,
			Entry{7, 13, Hash64("Sequencer"), 0, 0, 6});

		result->AppendEntry(
			RegisterType::ShaderResource, RegisterQualifier::None,
			Entry{0, 15, Hash64("Numeric"), 3, 0, 15});
		result->AppendEntry(
			RegisterType::ConstantBuffer, RegisterQualifier::None,
			Entry{0, 4, Hash64("Numeric"), 3, 18, 22});
		result->AppendEntry(
			RegisterType::Sampler, RegisterQualifier::None,
			Entry{16, 17, Hash64("Numeric"), 3, 22, 23});

		result->AppendEntry(
			RegisterType::ShaderResource, RegisterQualifier::None,
			Entry{23, 31, Hash64("Material"), 1, 3, 11});
		result->AppendEntry(
			RegisterType::ConstantBuffer, RegisterQualifier::None,
			Entry{4, 7, Hash64("Material"), 1, 0, 3});
		result->AppendEntry(
			RegisterType::Sampler, RegisterQualifier::None,
			Entry{7, 8, Hash64("Material"), 1, 12, 13});

		result->AppendEntry(
			RegisterType::ShaderResource, RegisterQualifier::None,
			Entry{15, 16, Hash64("Draw"), 2, 1, 2});
		result->AppendEntry(
			RegisterType::ConstantBuffer, RegisterQualifier::None,
			Entry{13, 14, Hash64("Draw"), 2, 0, 1});
		result->AppendEntry(
			RegisterType::Sampler, RegisterQualifier::None,
			Entry{8, 10, Hash64("Draw"), 2, 2, 4});
		return result;
	}
}
