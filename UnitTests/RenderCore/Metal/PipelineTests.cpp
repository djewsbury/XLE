// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "MetalTestHelper.h"
#include "MetalTestShaders.h"
#include "../../../RenderCore/Metal/InputLayout.h"
#include "../../../RenderCore/Metal/DeviceContext.h"
#include "../../../RenderCore/Metal/TextureView.h"
#include "../../../RenderCore/Metal/ObjectFactory.h"
#include "../../../RenderCore/Vulkan/IDeviceVulkan.h"
#include "../../../RenderCore/Assets/PredefinedDescriptorSetLayout.h"
#include "../../../RenderCore/Format.h"
#include "../../../RenderCore/BufferView.h"
#include "../../../RenderCore/MinimalShaderSource.h"
#include "../../../RenderCore/RenderUtils.h"
#include "../../../RenderCore/IAnnotator.h"
#include "../../../Math/Vector.h"
#include "../../../Utility/MemoryUtils.h"
#include "../../../Assets/AssetUtils.h"
#include "catch2/catch_test_macros.hpp"
#include "catch2/catch_approx.hpp"

using namespace Catch::literals;
namespace UnitTests
{
	static const char* s_vs_descriptorSetTest = R"--(#version 400
		#extension GL_ARB_separate_shader_objects : enable
		#extension GL_ARB_shading_language_420pack : enable

		precision highp int;
		precision highp float;

		layout (std140, set=0, binding=0) uniform Set0Binding0
		{
			vec3 InputA;
			float InputB;
		} Set0Binding0_inst;

		layout (std140, set=1, binding=4) uniform Set1Binding4
		{
			vec3 InputA;
			float InputB;
		} Set1Binding4_inst;

		layout (std140, push_constant) uniform PushConstants0
		{
			vec3 InputA;
			float InputB;
		} PushConstants0_inst;

		layout (location=0) flat out int vs_success;

		int fakeMod(int lhs, int rhs)
		{
			// only valid for positive values
			float A = float(lhs) / float(rhs);
			return int((A - floor(A)) * float(rhs));
		}
			
		void main()
		{
			vec2 t = vec2(
				(fakeMod(gl_VertexIndex, 2) == 1) ? 0.0 :  1.0,
				(fakeMod(gl_VertexIndex/2, 2) == 1) ? 0.0 :  1.0);
			gl_Position = vec4(t.x *  2.0 - 1.0, t.y * -2.0 + 1.0, 0.0, 1.0);

			bool success = 
				(Set0Binding0_inst.InputA.x == 1.0)
				&& (Set0Binding0_inst.InputB == 5.0)
				&& (Set1Binding4_inst.InputA.x == 7.0)
				&& (Set1Binding4_inst.InputB == 9.0)
				&& (PushConstants0_inst.InputA.x == 13.0)
				&& (PushConstants0_inst.InputB == 16.0)
				;
			vs_success = int(success);
		}
	)--";

	static const char* s_ps_descriptorSetTest = R"--(#version 430
		#extension GL_ARB_separate_shader_objects : enable
		#extension GL_ARB_shading_language_420pack : enable

		precision highp int;
		precision highp float;

		// "Storage buffer" bound using DescriptorType::UnorderedAccessBuffer
		layout (set=1, binding=5) readonly buffer Set1Binding5
		{
			int someIntegers[];
		} Set1Binding5_inst;

		// "Storage texture" bound using DescriptorType::UnorderedAccessTexture
		layout (set=1, binding=6, rgba8ui) readonly uniform highp uimage2D Set1Binding6;		

		// Vulkan is particular about push constants. We must ensure that the
		// byte offset for our fragment shader push constants comes after
		// the range already allocated for the vertex shader constants
		layout (std140, push_constant) uniform PushConstants1
		{
			vec3 vsBuffer_InputA; float vsBuffer_InputB;

			vec3 InputA;
			float InputB;
		} PushConstants1_inst;

		layout (location=0) flat in int vs_success;
		layout (location=0) out vec4 main_out_color;
		void main()
		{
			bool success = 
				Set1Binding5_inst.someIntegers[0] == 34
				&& Set1Binding5_inst.someIntegers[45] == 48
				&& Set1Binding5_inst.someIntegers[54] == 13
				&& imageLoad(Set1Binding6, ivec2(2,2)) == uvec4(12,33,23,8)
				&& (PushConstants1_inst.InputA.x == 14.0)
				&& (PushConstants1_inst.InputB == 19.0)
				;

			if (success && vs_success != 0) {
				main_out_color = vec4(0, 1, 0, 1);
			} else {
				main_out_color = vec4(1, 0, 0, 1);
			}
		}
	)--";

	struct TestBufferType { Float3 InputA; float InputB; };

	static std::shared_ptr<RenderCore::ICompiledPipelineLayout> CreateCustomPipelineLayout(RenderCore::IDevice& device)
	{
		using namespace RenderCore;
		RenderCore::DescriptorSetSignature set0 {
			{DescriptorType::ConstantBuffer},
			{DescriptorType::ConstantBuffer},
			{DescriptorType::ConstantBuffer},
			{DescriptorType::ConstantBuffer},
			{DescriptorType::ConstantBuffer},
		};

		RenderCore::DescriptorSetSignature set1 {
			{DescriptorType::ConstantBuffer},
			{DescriptorType::ConstantBuffer},
			{DescriptorType::ConstantBuffer},
			{DescriptorType::ConstantBuffer},
			{DescriptorType::ConstantBuffer},

			{DescriptorType::UnorderedAccessBuffer},
			{DescriptorType::UnorderedAccessTexture}
		};

		RenderCore::PipelineLayoutDesc desc;
		desc.AppendDescriptorSet("Set0", set0);
		desc.AppendDescriptorSet("Set1", set1);
		desc.AppendPushConstants("PushConstants0", sizeof(TestBufferType), ShaderStage::Vertex);
		desc.AppendPushConstants("PushConstants1", sizeof(TestBufferType), ShaderStage::Pixel);
		return device.CreatePipelineLayout(desc);
	}

	static RenderCore::ResourceDesc AsStagingDesc(const RenderCore::ResourceDesc& desc)
	{
		auto stagingDesc = desc;
		stagingDesc._bindFlags = RenderCore::BindFlag::TransferSrc;
		stagingDesc._cpuAccess = RenderCore::CPUAccess::Write;
		stagingDesc._gpuAccess = 0;
		return stagingDesc;
	}

	static std::shared_ptr<RenderCore::IResource> CreateTestStorageTexture(RenderCore::IDevice& device, RenderCore::IThreadContext& threadContext)
	{
		using namespace RenderCore;
		auto desc = CreateDesc(
			BindFlag::UnorderedAccess | BindFlag::TransferDst, 0, GPUAccess::Read,
			TextureDesc::Plain2D(8, 8, Format::R8G8B8A8_UINT),
			"test-storage-texture");
		auto result = device.CreateResource(desc);

		// fill with data via a staging texture
		// Vulkan really doesn't like initializing UnorderedAccess with preinitialized data, even if we use
		// linear tiling. We must do an explicit initialization via a staging texture
		auto staging = device.CreateResource(AsStagingDesc(desc));
		{
			Metal::ResourceMap map(
				*Metal::DeviceContext::Get(threadContext),
				*checked_cast<Metal::Resource*>(staging.get()), 
				Metal::ResourceMap::Mode::WriteDiscardPrevious);
			std::memset(map.GetData().begin(), 0, map.GetData().size());
			map.GetData().Cast<unsigned*>()[2*8+2] = (8u << 24u) | (23u << 16u) | (33u << 8u) | (12u);
		}
		Metal::Copy(
			*Metal::DeviceContext::Get(threadContext),
			*checked_cast<Metal::Resource*>(result.get()), *checked_cast<Metal::Resource*>(staging.get()), Metal::Internal::ImageLayout::General);

		return result;
	}

	static std::shared_ptr<RenderCore::IResource> CreateTestStorageBuffer(RenderCore::IDevice& device)
	{
		struct StorageBufferContents
		{
			int _values[64];
		} contents;
		XlZeroMemory(contents);
		contents._values[0] = 34;
		contents._values[45] = 48;
		contents._values[54] = 13;

		using namespace RenderCore;
		auto desc = CreateDesc(
			BindFlag::UnorderedAccess, 0, GPUAccess::Read,
			LinearBufferDesc::Create(sizeof(contents)),
			"test-storage-buffer");

		return device.CreateResource(desc, MakeOpaqueIteratorRange(contents).Cast<const void*>());
	}

	TEST_CASE( "Pipeline-DescriptorSetBinding", "[rendercore_metal]" )
	{
		using namespace RenderCore;
		auto testHelper = MakeTestHelper();
		auto threadContext = testHelper->_device->GetImmediateContext();
		auto targetDesc = CreateDesc(
			BindFlag::RenderTarget | BindFlag::TransferSrc, 0, GPUAccess::Write,
			TextureDesc::Plain2D(256, 256, Format::R8G8B8A8_UNORM),
			"temporary-out");
		UnitTestFBHelper fbHelper(*testHelper->_device, targetDesc);
		auto testStorageTexture = CreateTestStorageTexture(*testHelper->_device, *threadContext);
		auto testStorageBuffer = CreateTestStorageBuffer(*testHelper->_device);
		
		std::shared_ptr<RenderCore::ShaderService::IShaderSource> customShaderSource;

		auto* vulkanDevice  = (RenderCore::IDeviceVulkan*)testHelper->_device->QueryInterface(typeid(RenderCore::IDeviceVulkan).hash_code());
		if (vulkanDevice) {
			// Vulkan allows for multiple ways for compiling shaders. The tests currently use a HLSL to GLSL to SPIRV 
			// cross compilation approach
			RenderCore::VulkanCompilerConfiguration cfg;
			cfg._shaderMode = RenderCore::VulkanShaderMode::GLSLToSPIRV;
		 	auto shaderCompiler = vulkanDevice->CreateShaderCompiler(cfg);
			 customShaderSource = std::make_shared<RenderCore::MinimalShaderSource>(shaderCompiler);
		} else {
			Throw(std::runtime_error("This test only implemented for Vulkan"));
		}

		auto pipelineLayout = CreateCustomPipelineLayout(*testHelper->_device);

		TestBufferType set0binding0 { Float3(1, 1, 1), 5 };
		TestBufferType set1binding4 { Float3(7, 7, 7), 9 };
		TestBufferType pushConstants0 { Float3(13, 13, 13), 16 };
		TestBufferType pushConstants1 { Float3(14, 14, 14), 19 };

		////////////////////////////////////////////////////////////////////////////////////////
		{
			auto& metalContext = *Metal::DeviceContext::Get(*threadContext);
			auto rpi = fbHelper.BeginRenderPass(*threadContext);
			auto encoder = metalContext.BeginGraphicsEncoder_ProgressivePipeline(pipelineLayout);

			auto shaderProgram = MakeShaderProgram(customShaderSource, pipelineLayout, s_vs_descriptorSetTest, s_ps_descriptorSetTest);
			encoder.Bind(shaderProgram);

			UniformsStreamInterface looseUniforms;
			looseUniforms.BindBufferView(0, Hash64("Set0Binding0"));
			looseUniforms.BindBufferView(1, Hash64("Set1Binding4"));
			looseUniforms.BindBufferView(2, Hash64("PushConstants0"));
			looseUniforms.BindBufferView(3, Hash64("PushConstants1"));
			looseUniforms.BindBufferView(4, Hash64("Set1Binding5"));		// this is the storage buffer / unordered access buffer
			looseUniforms.BindTextureView(0, Hash64("Set1Binding6"));		// this is the storage texture / unordered access texture
			Metal::BoundUniforms uniforms(shaderProgram, {}, looseUniforms);

			ConstantBufferView cbvs[] {
				ConstantBufferView { MakeSharedPkt(set0binding0) },
				ConstantBufferView { MakeSharedPkt(set1binding4) },
				ConstantBufferView { MakeSharedPkt(pushConstants0) },
				ConstantBufferView { MakeSharedPkt(pushConstants1) },
				ConstantBufferView { testStorageBuffer }
			};
			Metal::TextureView textView { Metal::GetObjectFactory(), testStorageTexture };
			Metal::TextureView* textureViews[] { &textView };
			UniformsStream uniformsStream;
			uniformsStream._bufferViews = MakeIteratorRange(cbvs);
			uniformsStream._textureViews = UniformsStream::MakeResources(MakeIteratorRange(textureViews));
			uniforms.ApplyLooseUniforms(metalContext, encoder, uniformsStream);

			Metal::BoundInputLayout inputLayout(IteratorRange<const InputElementDesc*>{}, shaderProgram);
			REQUIRE(inputLayout.AllAttributesBound());
			encoder.Bind(inputLayout, Topology::TriangleStrip);
			encoder.Draw(4);
		}

		auto data = fbHelper.GetMainTarget()->ReadBack(*threadContext);
		auto pixels = MakeIteratorRange((unsigned*)AsPointer(data.begin()), (unsigned*)AsPointer(data.end()));
		REQUIRE(pixels[0] == 0xff00ff00);		// shader writes green on success, red on failure
		////////////////////////////////////////////////////////////////////////////////////////

		// potential tests:
		//		- mismatches (shader vs descriptor set, bindings vs descriptor set types)
	}

	TEST_CASE( "Pipeline-NumericInterface", "[rendercore_metal]" )
	{
		using namespace RenderCore;
		auto testHelper = MakeTestHelper();
		auto threadContext = testHelper->_device->GetImmediateContext();
		auto targetDesc = CreateDesc(
			BindFlag::RenderTarget | BindFlag::TransferSrc, 0, GPUAccess::Write,
			TextureDesc::Plain2D(256, 256, Format::R8G8B8A8_UNORM),
			"temporary-out");
		UnitTestFBHelper fbHelper(*testHelper->_device, targetDesc);

		TestBufferType pushConstants0 { Float3(1, 0, 1), 8 };
		auto testConstantBuffer = testHelper->CreateCB(MakeOpaqueIteratorRange(pushConstants0));

		// Initialize 2 textures with some data to read
		std::shared_ptr<IResource> tex0, tex1;
		{
			auto desc = CreateDesc(
				BindFlag::ShaderResource | BindFlag::TransferDst, 0, GPUAccess::Read,
				TextureDesc::Plain2D(8, 8, Format::R8G8B8A8_UINT),
				"test-storage-texture-0");
			tex0 = testHelper->_device->CreateResource(desc);
			XlCopyString(desc._name, "test-storage-texture-1");
			tex1 = testHelper->_device->CreateResource(desc);

			auto staging = testHelper->_device->CreateResource(AsStagingDesc(desc));
			auto& metalContext = *Metal::DeviceContext::Get(*threadContext);
			{
				Metal::ResourceMap map(metalContext, *checked_cast<Metal::Resource*>(staging.get()), Metal::ResourceMap::Mode::WriteDiscardPrevious);
				std::memset(map.GetData().begin(), 0, map.GetData().size());
				map.GetData().Cast<unsigned*>()[3*8+3] = (9u << 24u) | (5u << 16u) | (3u << 8u) | (7u);
			}
			Metal::Internal::CaptureForBind tex0Cap(metalContext, *tex0, BindFlag::TransferDst);
			Metal::Copy(metalContext, *checked_cast<Metal::Resource*>(tex0.get()), *checked_cast<Metal::Resource*>(staging.get()));

			{
				Metal::ResourceMap map(metalContext, *checked_cast<Metal::Resource*>(staging.get()), Metal::ResourceMap::Mode::WriteDiscardPrevious);
				std::memset(map.GetData().begin(), 0, map.GetData().size());
				map.GetData().Cast<unsigned*>()[4*8+4] = (23u << 24u) | (99u << 16u) | (45u << 8u) | (10u);
			}
			Metal::Internal::CaptureForBind tex1Cap(metalContext, *tex1, BindFlag::TransferDst);
			Metal::Copy(metalContext, *checked_cast<Metal::Resource*>(tex1.get()), *checked_cast<Metal::Resource*>(staging.get()), Metal::Internal::ImageLayout::ShaderReadOnlyOptimal);
		}

		////////////////////////////////////////////////////////////////////////////////////////
		{
			auto& metalContext = *Metal::DeviceContext::Get(*threadContext);
			auto rpi = fbHelper.BeginRenderPass(*threadContext);
			auto encoder = metalContext.BeginGraphicsEncoder_ProgressivePipeline(testHelper->_pipelineLayout);

			auto shaderProgram = testHelper->MakeShaderProgram(vsText_FullViewport, psText_LegacyBindings);
			encoder.Bind(shaderProgram);

			Metal::NumericUniformsInterface numericInterface(
				Metal::GetObjectFactory(),
				*testHelper->_pipelineLayout,
				*testHelper->_defaultLegacyBindings);

			Metal::TextureView tv0 { tex0 };
			Metal::TextureView tv1 { tex1 };
			ConstantBufferView cbs[] { testConstantBuffer };

			numericInterface.Bind(MakeResourceList(5, tv0, tv1));
			numericInterface.Bind(9, MakeIteratorRange(cbs));
			numericInterface.Apply(metalContext, encoder);

			Metal::BoundInputLayout inputLayout(IteratorRange<const InputElementDesc*>{}, shaderProgram);
			REQUIRE(inputLayout.AllAttributesBound());
			encoder.Bind(inputLayout, Topology::TriangleStrip);
			encoder.Draw(4);
		}

		auto data = fbHelper.GetMainTarget()->ReadBack(*threadContext);
		auto pixels = MakeIteratorRange((unsigned*)AsPointer(data.begin()), (unsigned*)AsPointer(data.end()));
		REQUIRE(pixels[0] == 0xff00ff00);		// shader writes green on success, red on failure
		////////////////////////////////////////////////////////////////////////////////////////
	}

}