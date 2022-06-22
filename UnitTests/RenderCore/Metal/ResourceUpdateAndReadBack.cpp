// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "../../UnitTestHelper.h"
#include "MetalTestHelper.h"
#include "MetalTestShaders.h"
#include "../../../RenderCore/Metal/Shader.h"
#include "../../../RenderCore/Metal/InputLayout.h"
#include "../../../RenderCore/Metal/State.h"
#include "../../../RenderCore/Metal/PipelineLayout.h"
#include "../../../RenderCore/Metal/QueryPool.h"
#include "../../../RenderCore/Metal/DeviceContext.h"
#include "../../../RenderCore/Metal/Resource.h"
#include "../../../RenderCore/Metal/ObjectFactory.h"
#include "../../../RenderCore/OpenGLES/IDeviceOpenGLES.h"
#include "../../../RenderCore/Vulkan/IDeviceVulkan.h"
#include "../../../RenderCore/ResourceDesc.h"
#include "../../../RenderCore/BufferView.h"
#include "../../../RenderCore/IDevice.h"
#include "../../../Math/Vector.h"
#include "../../../Math/Transformations.h"
#include "../../../Utility/MemoryUtils.h"
#include "../../../Utility/HeapUtils.h"
#include "../../../Utility/BitUtils.h"
#include "catch2/catch_test_macros.hpp"
#include "catch2/catch_approx.hpp"
#include <map>
#include <deque>
#include <queue>
#include <future>
#include <random>

using namespace Catch::literals;

namespace UnitTests
{

	// See comments in ColorPackedForm below. We can't predict the exact value, so we have to test +/- 1.
	static bool ComponentsMatch(uint32_t c1, uint32_t c2) {
		return (c1 == c2 || c1+1 == c2 || c1 == c2+1);
	}

	static bool ColorsMatch(uint32_t c1, uint32_t c2) {
		unsigned char *p1 = reinterpret_cast<unsigned char *>(&c1);
		unsigned char *p2 = reinterpret_cast<unsigned char *>(&c2);
		return (
			ComponentsMatch(p1[0], p2[0]) &&
			ComponentsMatch(p1[1], p2[1]) &&
			ComponentsMatch(p1[2], p2[2]) &&
			ComponentsMatch(p1[3], p2[3])
		);
	}

////////////////////////////////////////////////////////////////////////////////////////////////////

	static unsigned vertices_vIdx[] = { 0, 1, 2, 3 };

	static RenderCore::InputElementDesc inputEleVIdx[] = {
		RenderCore::InputElementDesc { "vertexID", 0, RenderCore::Format::R32_SINT }
	};

	struct Values
	{
		float A = 0.f, B = 0.f, C = 0.f;
		unsigned dummy = 0;
		Float4 vA = Float4 { 0.f, 0.f, 0.f, 0.f };

		Values(const Float4& c) 
		{ 
			A = c[0]; B = c[1]; vA[0] = c[2]; vA[1] = c[3];
		}

		Values() {}

		// The way float32 colors get rounded when drawn to normalized U8 textures may differ between GFXAPIs. Metal documents that each component gets normalized and then rounded to nearest even, but this isn't actually what happens--while 0.3, 0.5, and 0.7 get rounded up to 77, 128, and 179 as you'd expect, 0.1 and 0.9 get rounded down to 25 and 229. So, rather than worry about rounding here, we just truncate, and then check +/-1 in the comparison.
		unsigned ColorPackedForm() const
		{
			return  (unsigned(A * 255.f) << 0)
				|   (unsigned(B * 255.f) <<  8)
				|   (unsigned(vA[0] * 255.f) <<  16)
				|   (unsigned(vA[1] * 255.f) << 24)
				;
		}
	};

	const RenderCore::ConstantBufferElementDesc ConstantBufferElementDesc_Values[] {
		RenderCore::ConstantBufferElementDesc { Hash64("A"), RenderCore::Format::R32_FLOAT, offsetof(Values, A) },
		RenderCore::ConstantBufferElementDesc { Hash64("B"), RenderCore::Format::R32_FLOAT, offsetof(Values, B) },
		RenderCore::ConstantBufferElementDesc { Hash64("C"), RenderCore::Format::R32_FLOAT, offsetof(Values, C) },
		RenderCore::ConstantBufferElementDesc { Hash64("vA"), RenderCore::Format::R32G32B32A32_FLOAT, offsetof(Values, vA) }
	};

////////////////////////////////////////////////////////////////////////////////////////////////////
			//    C O D E

	static void DrawClipSpaceQuad(
		MetalTestHelper& testHelper,
		RenderCore::Metal::DeviceContext& metalContext,
		RenderCore::Metal::GraphicsEncoder_ProgressivePipeline& encoder,
		RenderCore::Metal::ShaderProgram& shaderProgram,
		const Float2& topLeft, const Float2& bottomRight, unsigned color = 0xffffffff)
	{
		using namespace RenderCore;

		class VertexPC
		{
		public:
			Float4      _position;
			unsigned    _color;
		};

		const VertexPC vertices[] = {
			VertexPC { Float4 {     topLeft[0],     topLeft[1], 0.0f, 1.0f }, color },
			VertexPC { Float4 {     topLeft[0], bottomRight[1], 0.0f, 1.0f }, color },
			VertexPC { Float4 { bottomRight[0],     topLeft[1], 0.0f, 1.0f }, color },
			VertexPC { Float4 { bottomRight[0], bottomRight[1], 0.0f, 1.0f }, color }
		};

		const RenderCore::InputElementDesc inputElePC[] = {
			RenderCore::InputElementDesc { "position", 0, RenderCore::Format::R32G32B32A32_FLOAT },
			RenderCore::InputElementDesc { "color", 0, RenderCore::Format::R8G8B8A8_UNORM }
		};

		auto vertexBuffer0 = testHelper.CreateVB(MakeIteratorRange(vertices));

		Metal::BoundInputLayout inputLayout(MakeIteratorRange(inputElePC), shaderProgram);
		REQUIRE(inputLayout.AllAttributesBound());
		VertexBufferView vbvs[] = { vertexBuffer0.get() };

		encoder.Bind(MakeIteratorRange(vbvs), IndexBufferView{});
		encoder.Bind(inputLayout, Topology::TriangleStrip);
		encoder.Draw(4);           // Draw once, with CB contents initialized outside of the RPI
	}

	const Values testValue0 = Float4(0.1f, 0.2f, 0.95f, 1.0f);
	const Values testValue1 = Float4(0.9f, 0.4f, 0.3f, 1.0f);
	const Values testValue2 = Float4(0.5f, 0.85f, 0.6f, 1.0f);
	const Values testValue3 = Float4(0.7f, 0.8f, 0.75f, 1.0f);
	const Values testValueRedundant = Float4(0.65f, 0.33f, 0.42f, 1.0f);

	static void UpdateConstantBuffer(
		RenderCore::Metal::DeviceContext& metalContext, 
		RenderCore::IDevice& device,
		RenderCore::IResource& cbResource,
		IteratorRange<const void*> newData,
		bool unsynchronized)
	{
		using namespace RenderCore;
		if (unsynchronized) {
			Metal::ResourceMap map{metalContext, cbResource, Metal::ResourceMap::Mode::WriteDiscardPrevious};
			REQUIRE(newData.size() == map.GetData().size());
			std::memcpy(map.GetData().begin(), newData.begin(), std::min(newData.size(), map.GetData().size()));
		} else {
			auto stagingDesc = cbResource.GetDesc();
			stagingDesc._bindFlags = BindFlag::TransferSrc;
			stagingDesc._allocationRules = AllocationRules::HostVisibleSequentialWrite;
			XlCopyString(stagingDesc._name, "TempStaging");
			auto stagingRes = device.CreateResource(stagingDesc, newData);
			auto encoder = metalContext.BeginBlitEncoder();
			encoder.Copy(cbResource, *stagingRes);
		}
	}

	static std::map<unsigned, unsigned> _UpdateConstantBufferHelper(MetalTestHelper& testHelper, bool unsynchronized)
	{
		// -------------------------------------------------------------------------------------
		// Create a constant buffer and use it during rendering of several draw calls. Ensure
		// that the updates to the constant buffer affect rendering as expected
		// -------------------------------------------------------------------------------------
		using namespace RenderCore;

		auto threadContext = testHelper._device->GetImmediateContext();
		auto shaderProgram = testHelper.MakeShaderProgram(vsText_clipInput, psText_Uniforms);
		auto targetDesc = CreateDesc(
			BindFlag::RenderTarget | BindFlag::TransferSrc,
			TextureDesc::Plain2D(1024, 1024, Format::R8G8B8A8_UNORM),
			"temporary-out");

		std::shared_ptr<IResource> cbResource;
		if (unsynchronized) {
			cbResource = testHelper._device->CreateResource(
				CreateDesc(	
					BindFlag::ConstantBuffer, AllocationRules::HostVisibleSequentialWrite,
					LinearBufferDesc::Create(sizeof(Values)),
					"test-cbuffer"));
		} else {
			cbResource = testHelper._device->CreateResource(
				CreateDesc(	
					BindFlag::ConstantBuffer | BindFlag::TransferDst, 0,
					LinearBufferDesc::Create(sizeof(Values)),
					"test-cbuffer"));
		}

		auto& metalContext = *Metal::DeviceContext::Get(*threadContext);
		UpdateConstantBuffer(metalContext, *testHelper._device, *cbResource, MakeOpaqueIteratorRange(testValue0), unsynchronized);

		// ............. Setup BoundInputLayout & BoundUniforms ................................

		UniformsStreamInterface usi;
		usi.BindResourceView(0, Hash64("Values"), MakeIteratorRange(ConstantBufferElementDesc_Values));
		Metal::BoundUniforms uniforms { shaderProgram, usi };

		// ............. Start RPI .............................................................

		UnitTestFBHelper fbHelper(*testHelper._device, *threadContext, targetDesc, LoadStore::Retain);		// retain because we use it twice

		{
			auto rpi = fbHelper.BeginRenderPass(*threadContext);
			auto encoder = metalContext.BeginGraphicsEncoder_ProgressivePipeline(testHelper._pipelineLayout);
			encoder.Bind(shaderProgram);

			auto cbView = cbResource->CreateBufferView();
			IResourceView* views[] = { cbView.get() };
			UniformsStream uniformsStream;
			uniformsStream._resourceViews = MakeIteratorRange(views);
			uniforms.ApplyLooseUniforms(metalContext, encoder, uniformsStream);

			// CB values set prior to the rpi
			DrawClipSpaceQuad(testHelper, metalContext, encoder, shaderProgram, Float2(-1.0f, -1.0f), Float2( 0.0f, 0.0f));

			// CB values set in the middle of the rpi--illegal for synchronized
			if (unsynchronized) {
				UpdateConstantBuffer(metalContext, *testHelper._device, *cbResource, MakeOpaqueIteratorRange(testValue1), unsynchronized);
			} else {
				REQUIRE_THROWS([&]() {
					UpdateConstantBuffer(metalContext, *testHelper._device, *cbResource, MakeOpaqueIteratorRange(testValue1), unsynchronized);
				}());
			}
			DrawClipSpaceQuad(testHelper, metalContext, encoder, shaderProgram, Float2( 0.0f, -1.0f), Float2( 1.0f, 0.0f));

			// Set a value that will be unused, and then immediate reset with new data--still illegal for synchronized
			if (unsynchronized) {
				UpdateConstantBuffer(metalContext, *testHelper._device, *cbResource, MakeOpaqueIteratorRange(testValueRedundant), unsynchronized);
				UpdateConstantBuffer(metalContext, *testHelper._device, *cbResource, MakeOpaqueIteratorRange(testValue2), unsynchronized);
			}
			DrawClipSpaceQuad(testHelper, metalContext, encoder, shaderProgram, Float2(-1.0f,  0.0f), Float2( 0.0f, 1.0f));

			// Set a value to be used in the next render pass--still illegal for synchronized
			if (unsynchronized) {
				UpdateConstantBuffer(metalContext, *testHelper._device, *cbResource, MakeOpaqueIteratorRange(testValue3), unsynchronized);
			}
		}

		// Set a value to be used in the next render pass--the right place for unsynchronized
		if (!unsynchronized) {
			Metal::BarrierHelper(*threadContext).Add(*cbResource, {BindFlag::ConstantBuffer, ShaderStage::Pixel}, BindFlag::TransferDst);
			UpdateConstantBuffer(metalContext, *testHelper._device, *cbResource, MakeOpaqueIteratorRange(testValue3), unsynchronized);
			Metal::BarrierHelper(*threadContext).Add(*cbResource, BindFlag::TransferDst, {BindFlag::ConstantBuffer, ShaderStage::Pixel});
		}

		{
			auto rpi = fbHelper.BeginRenderPass(*threadContext); // RenderCore::LoadStore::Retain);
			auto encoder = metalContext.BeginGraphicsEncoder_ProgressivePipeline(testHelper._pipelineLayout);
			encoder.Bind(shaderProgram);

			auto cbView = cbResource->CreateBufferView();
			IResourceView* views[] = { cbView.get() };
			UniformsStream uniformsStream;
			uniformsStream._resourceViews = MakeIteratorRange(views);
			uniforms.ApplyLooseUniforms(metalContext, encoder, uniformsStream);

			// CB values set in the previous rpi
			DrawClipSpaceQuad(testHelper, metalContext, encoder, shaderProgram, Float2( 0.0f,  0.0f), Float2( 1.0f, 1.0f));
		}

		return fbHelper.GetFullColorBreakdown(*threadContext);
	}

	TEST_CASE( "ResourceUpdateAndReadback-UpdateConstantBufferUnsynchronized", "[rendercore_metal]" )
	{
		using namespace RenderCore;

		auto testHelper = MakeTestHelper();

		auto* glesDevice = (IDeviceOpenGLES*)testHelper->_device->QueryInterface(typeid(IDeviceOpenGLES).hash_code());
		if (glesDevice) {
			if (!(glesDevice->GetFeatureSet() & Metal_OpenGLES::FeatureSet::GLES300)) {
				FAIL("Known issues running this code on non GLES300 OpenGL: unsynchronized writes are simulated with synchronized writes, so we don't get the expected results");
			}
		}

		auto breakdown = _UpdateConstantBufferHelper(*testHelper, true);

		// Since we're not synchronizing anywhere, and doing virtually no CPU work,
		// it's incredibly unlikely that anything in either render pass will get
		// drawn before the last update, so all four quadrants should have the
		// last value set, even though testValue0, 1, and 2 were the current
		// values at the times we actually issued the draws.
		REQUIRE(breakdown.size() == (size_t)1);
		for (auto i:breakdown) {
			auto color = i.first;
			REQUIRE(ColorsMatch(color, testValue3.ColorPackedForm()));
		}
	}

	TEST_CASE( "ResourceUpdateAndReadback-UpdateConstantBufferSynchronized", "[rendercore_metal]" )
	{
		auto testHelper = MakeTestHelper();

		auto breakdown = _UpdateConstantBufferHelper(*testHelper, false);

		// With synchronized writes that happen on render-pass boundaries, we're
		// expecting that the first three quadrants (in the first render pass)
		// will have testValue0, and the last quadrant (in the second) will have
		// testValue3.
		REQUIRE(breakdown.size() == (size_t)2);
		for (auto i:breakdown) {
			auto color = i.first;
			REQUIRE((ColorsMatch(color, testValue0.ColorPackedForm()) ||
							ColorsMatch(color, testValue3.ColorPackedForm())));
		}
	}

	TEST_CASE( "ResourceUpdateAndReadback-AllocationThrashing", "[rendercore_metal]" )
	{
		using namespace RenderCore;
		auto testHelper = MakeTestHelper();

		auto threadContext = testHelper->_device->GetImmediateContext();
		auto shaderProgram = testHelper->MakeShaderProgram(vsText_clipInput, psText_Uniforms);
		auto targetDesc = CreateDesc(
			BindFlag::RenderTarget | BindFlag::TransferSrc,
			TextureDesc::Plain2D(1024, 1024, Format::R8G8B8A8_UNORM),
			"temporary-out");

		auto& metalContext = *Metal::DeviceContext::Get(*threadContext);

		// ............. Setup BoundInputLayout & BoundUniforms ................................

		UniformsStreamInterface usi;
		usi.BindResourceView(0, Hash64("Values"), MakeIteratorRange(ConstantBufferElementDesc_Values));
		Metal::BoundUniforms uniforms { shaderProgram, usi };

		// ............. Start RPI .............................................................

		UnitTestFBHelper fbHelper(*testHelper->_device, *threadContext, targetDesc, LoadStore::Retain);

		uint8_t initData[32*1024];
		std::memset(initData, 0xff, sizeof(initData));

		// This is a thrash test to ensure that GPU resources are destroyed in a reasonable way 
		// Resources must be kept alive even after all client references on them have been dropped,
		// if the GPU still have commands that are either queued or currently processing that use
		// then. However after the GPU has finished with the frame the resource can be released.
		// In this test we simulate rendering a lot of frames and allocating resources during
		// those frames. 
		// If the deallocation of resources is not happening correctly, we will start to run
		// out of memory very quickly. This might also happen if the CPU runs too far ahead of 
		// the GPU, so this test also ensures that there are barriers against that as well.
		for (unsigned frameIdx=0; frameIdx<100; ++frameIdx) {
			// Create a large resource -- but ensure that we use it during the draw call for this "frame"
			std::shared_ptr<IResource> cbs[128];
			for (unsigned d=0; d<dimof(cbs); ++d) {
				cbs[d] = testHelper->_device->CreateResource(
					CreateDesc(	
						BindFlag::ConstantBuffer, AllocationRules::HostVisibleSequentialWrite,
						LinearBufferDesc::Create(sizeof(initData)),
						"test-cbuffer"));
				UpdateConstantBuffer(metalContext, *testHelper->_device, *cbs[d], MakeOpaqueIteratorRange(initData), true);
			}

			{
				auto rpi = fbHelper.BeginRenderPass(*threadContext);

				auto encoder = metalContext.BeginGraphicsEncoder_ProgressivePipeline(testHelper->_pipelineLayout);
				encoder.Bind(shaderProgram);

				for (unsigned d=0; d<dimof(cbs); ++d) {
					auto cbView = cbs[d]->CreateBufferView();
					IResourceView* views[] = { cbView.get() };
					UniformsStream uniformsStream;
					uniformsStream._resourceViews = MakeIteratorRange(views);
					uniforms.ApplyLooseUniforms(metalContext, encoder, uniformsStream);

					DrawClipSpaceQuad(*testHelper, metalContext, encoder, shaderProgram, Float2(-1.0f, -1.0f), Float2( 0.0f, 0.0f));
				}
			}

			// We must commit commands to get the GPU working
			threadContext->CommitCommands();
		}
	}

	class BackgroundTextureUploader
	{
	public:
		std::future<std::shared_ptr<RenderCore::IResource>> Queue(
			const RenderCore::ResourceDesc& desc,
			RenderCore::BindFlag::Enum finalResourceState);
		void Tick();

		BackgroundTextureUploader(std::shared_ptr<RenderCore::IDevice> device);
		~BackgroundTextureUploader();
	private:
		std::condition_variable _newlyQueued;
		std::mutex _queueLock;
		std::atomic<unsigned> _frameIdx;
		std::thread _workerThread;
		std::atomic<bool> _quit;

		struct QueuedUpload
		{
			RenderCore::ResourceDesc _desc;
			RenderCore::BindFlag::Enum _finalResourceState;
			std::promise<std::shared_ptr<RenderCore::IResource>> _promise;
		};
		std::queue<QueuedUpload> _itemsToUpload;

		struct StagingBufferMan
		{
			CircularHeap _stagingBufferHeap;
			std::shared_ptr<RenderCore::IResource> _stagingBuffer;

			std::shared_ptr<RenderCore::IResource> CreateAndTransferData(
				RenderCore::IThreadContext& threadContext,
				const RenderCore::ResourceDesc& desc,
				RenderCore::BindFlag::Enum finalResourceState);

			struct AllocationPendingRelease
			{
				RenderCore::Metal_Vulkan::IAsyncTracker::Marker _releaseMarker;
				unsigned _pendingNewFront = ~0u;
			};
			std::vector<AllocationPendingRelease> _allocationsPendingRelease;

			void UpdateConsumerMarker(RenderCore::IThreadContext& threadContext);
		};
		StagingBufferMan _stagingBufferMan;
	};

	std::future<std::shared_ptr<RenderCore::IResource>> BackgroundTextureUploader::Queue(const RenderCore::ResourceDesc& desc, RenderCore::BindFlag::Enum finalResourceState)
	{
		ScopedLock(_queueLock);
		QueuedUpload upload;
		upload._desc = desc;
		upload._finalResourceState = finalResourceState;
		auto result = upload._promise.get_future();
		_itemsToUpload.push(std::move(upload));
		_newlyQueued.notify_one();
		return result;
	}

	void BackgroundTextureUploader::Tick()
	{
		++_frameIdx;
		_newlyQueued.notify_all();
	}

	static void UpdateFinalResourceFromStaging(
		RenderCore::IThreadContext& threadContext,
		RenderCore::IResource& finalResource, const RenderCore::ResourceDesc& destinationDesc, 
		RenderCore::IResource& stagingResource, unsigned stagingResourceBegin, unsigned stagingResourceSize)
	{
		// no layout changes -- we're expecting the caller to have already shifted the resource layouts
		// into something valid

		using namespace RenderCore;
		auto& metalContext = *Metal::DeviceContext::Get(threadContext);
		if (destinationDesc._type == ResourceDesc::Type::Texture) {
			auto lodLevelCount = destinationDesc._textureDesc._mipCount ? (unsigned)destinationDesc._textureDesc._mipCount : 1;
			auto arrayLayerCount = destinationDesc._textureDesc._arrayCount ? (unsigned)destinationDesc._textureDesc._arrayCount : 1;
			auto blitEncoder = metalContext.BeginBlitEncoder();
			blitEncoder.Copy(
				CopyPartial_Dest{finalResource},
				CopyPartial_Src{stagingResource, stagingResourceBegin, stagingResourceBegin+stagingResourceSize, lodLevelCount, arrayLayerCount});
		} else {
			assert(destinationDesc._type == ResourceDesc::Type::LinearBuffer);
			assert(destinationDesc._linearBufferDesc._sizeInBytes <= stagingResource.GetDesc()._linearBufferDesc._sizeInBytes);

			auto blitEncoder = metalContext.BeginBlitEncoder();
			blitEncoder.Copy(
				CopyPartial_Dest{finalResource},
				CopyPartial_Src{stagingResource, stagingResourceBegin, stagingResourceBegin+stagingResourceSize});
		}
	}

	static RenderCore::Metal_Vulkan::IAsyncTracker::Marker GetProducerMarker(RenderCore::IThreadContext& threadContext)
	{
		using namespace RenderCore;
		auto* vulkanDevice = (IDeviceVulkan*)threadContext.GetDevice()->QueryInterface(typeid(IDeviceVulkan).hash_code());
		if (!vulkanDevice)
			Throw(std::runtime_error("Expecting Vulkan device"));
		return vulkanDevice->GetAsyncTracker()->GetProducerMarker();
	}

	static RenderCore::Metal_Vulkan::IAsyncTracker::Marker GetConsumerMarker(RenderCore::IThreadContext& threadContext)
	{
		using namespace RenderCore;
		auto* vulkanDevice = (IDeviceVulkan*)threadContext.GetDevice()->QueryInterface(typeid(IDeviceVulkan).hash_code());
		if (!vulkanDevice)
			Throw(std::runtime_error("Expecting Vulkan device"));
		return vulkanDevice->GetAsyncTracker()->GetConsumerMarker();
	}

	static unsigned CalculateBufferOffsetAlignment(const RenderCore::ResourceDesc& desc)
	{
		using namespace RenderCore;
		auto& objectFactory = Metal::GetObjectFactory();
		unsigned alignment = 1u;
		#if GFXAPI_TARGET == GFXAPI_VULKAN
			alignment = std::max(alignment, (unsigned)objectFactory.GetPhysicalDeviceProperties().limits.optimalBufferCopyOffsetAlignment);
		#endif
		if (desc._type == ResourceDesc::Type::Texture) {
			auto compressionParam = GetCompressionParameters(desc._textureDesc._format);
			if (compressionParam._blockWidth != 1) {
				alignment = std::max(alignment, compressionParam._blockBytes);
			} else {
				// non-blocked format -- alignment requirement is a multiple of the texel size
				alignment = std::max(alignment, BitsPerPixel(desc._textureDesc._format)/8u);
			}
		}
		return alignment;
	}

	auto BackgroundTextureUploader::StagingBufferMan::CreateAndTransferData(
		RenderCore::IThreadContext& threadContext,
		const RenderCore::ResourceDesc& desc,
		RenderCore::BindFlag::Enum finalResourceState) -> std::shared_ptr<RenderCore::IResource>
	{
		using namespace RenderCore;
		auto byteCount = ByteCount(desc);
		unsigned alignment = CalculateBufferOffsetAlignment(desc);
		assert(finalResourceState);

		unsigned stagingAllocation = 0, stagingSize = 0;
		auto modifiedDesc = desc;
		if (!(modifiedDesc._allocationRules & (AllocationRules::HostVisibleRandomAccess|AllocationRules::HostVisibleSequentialWrite))) {
			modifiedDesc._bindFlags |= BindFlag::TransferDst;

			stagingAllocation = _stagingBufferHeap.AllocateBack(byteCount, alignment);
			if (stagingAllocation == ~0u) return {};
			stagingSize = byteCount;
		}
		
		auto resource = threadContext.GetDevice()->CreateResource(modifiedDesc);
		if (Metal::ResourceMap::CanMap(*threadContext.GetDevice(), *resource, Metal::ResourceMap::Mode::WriteDiscardPrevious)) {
			if (stagingAllocation) {
				// didn't need to make this allocation after all
				_stagingBufferHeap.UndoLastAllocation(stagingSize);
				stagingAllocation = stagingSize = 0;
			}

			Metal::ResourceMap mapping { *threadContext.GetDevice(), *resource, Metal::ResourceMap::Mode::WriteDiscardPrevious };
			std::memset(mapping.GetData().begin(), 0x3d, byteCount);
			Metal::BarrierHelper(threadContext).Add(*resource, Metal::BarrierResourceUsage::Preinitialized(), finalResourceState);
			// immediately usable (at least by cmdlist not already submitted)
		} else {
			assert(stagingSize);

			{
				Metal::ResourceMap mapping { *threadContext.GetDevice(), *_stagingBuffer, Metal::ResourceMap::Mode::WriteDiscardPrevious, stagingAllocation, stagingSize };
				auto uploadRange = mapping.GetData();
				uint8_t iterator = 0;
				for (auto& i:uploadRange.Cast<uint8_t*>()) 
					i = iterator++;
				mapping.FlushCache();
			}

			// During the transfer, the images must be in either TransferSrcOptimal, TransferDstOptimal or General
			Metal::BarrierHelper(threadContext).Add(*resource, Metal::BarrierResourceUsage::NoState(), BindFlag::TransferDst);
			checked_cast<RenderCore::Metal_Vulkan::Resource*>(resource.get())->ChangeSteadyState(BindFlag::TransferDst);

			UpdateFinalResourceFromStaging(
				threadContext,
				*resource, desc,
				*_stagingBuffer, stagingAllocation, stagingSize);

			Metal::BarrierHelper(threadContext).Add(*resource, BindFlag::TransferDst, finalResourceState);
			checked_cast<RenderCore::Metal_Vulkan::Resource*>(resource.get())->ChangeSteadyState(finalResourceState);

			auto producerMarker = GetProducerMarker(threadContext);
			_allocationsPendingRelease.push_back({producerMarker, stagingAllocation+stagingSize});
		}

		auto finalContainingGuid = resource->GetGUID();
		Metal::DeviceContext::Get(threadContext)->GetActiveCommandList().MakeResourcesVisible({&finalContainingGuid, &finalContainingGuid+1});

		return resource;
	}

	void BackgroundTextureUploader::StagingBufferMan::UpdateConsumerMarker(RenderCore::IThreadContext& threadContext)
	{
		auto consumerMarker = GetConsumerMarker(threadContext);
		while (!_allocationsPendingRelease.empty() && _allocationsPendingRelease.front()._releaseMarker <= consumerMarker) {
			assert(_allocationsPendingRelease.front()._pendingNewFront != ~0u);
			_stagingBufferHeap.ResetFront(_allocationsPendingRelease.front()._pendingNewFront);
			_allocationsPendingRelease.erase(_allocationsPendingRelease.begin());
		}
	}

	BackgroundTextureUploader::BackgroundTextureUploader(std::shared_ptr<RenderCore::IDevice> device)
	: _quit(false)
	{
		using namespace RenderCore;
		struct ItemsOnCmdList
		{
			std::promise<std::shared_ptr<RenderCore::IResource>> _promise;
			std::shared_ptr<RenderCore::IResource> _resource;
		};

		const unsigned stagingHeapSize = 32*1024*1024;
		_stagingBufferMan._stagingBufferHeap = CircularHeap(stagingHeapSize);
		_stagingBufferMan._stagingBuffer = device->CreateResource(
			CreateDesc(
				BindFlag::TransferSrc, AllocationRules::HostVisibleSequentialWrite | AllocationRules::PermanentlyMapped | AllocationRules::DisableAutoCacheCoherency,
				LinearBufferDesc::Create(stagingHeapSize),
				"main-staging-buffer"));

		_workerThread = std::thread{[device, this]() {
			auto bkThreadContext = device->CreateDeferredContext();
			std::optional<unsigned> oldestItem;
			std::vector<ItemsOnCmdList> itemsOnCmdList;

			std::optional<QueuedUpload> frontItem;
			bool pendingPopFrontItem = false;
			while (true) {
				{
					std::unique_lock<std::mutex> lk(_queueLock);
					if (pendingPopFrontItem) {
						_itemsToUpload.pop();		// pop the one that was completed last loop
						pendingPopFrontItem = false;
					} else if (frontItem) {
						_itemsToUpload.front() = std::move(*frontItem);
					}
					if (_itemsToUpload.empty())
						_newlyQueued.wait(lk);

					if (_quit)
						break;

					frontItem = {};
					if (!_itemsToUpload.empty())
						frontItem = std::move(_itemsToUpload.front());
				}

				_stagingBufferMan.UpdateConsumerMarker(*bkThreadContext);

				if (frontItem) {
					// process this upload request
					auto res = _stagingBufferMan.CreateAndTransferData(*bkThreadContext, frontItem->_desc, frontItem->_finalResourceState);
					if (!res) {
						// commit all to try to release staging allocations
						bkThreadContext->CommitCommands();
						for (auto& i:itemsOnCmdList)
							i._promise.set_value(i._resource);
						itemsOnCmdList.clear();
						oldestItem = {};
						if (_stagingBufferMan._allocationsPendingRelease.empty()) {
							// no more allocations to release -- can't complete this one
							frontItem->_promise.set_exception(std::make_exception_ptr(std::runtime_error("Resource requires more space than is available in the entire staging buffer")));
							pendingPopFrontItem = true;
						}
						continue;		// no space now -- wrap around and try again
					}

					itemsOnCmdList.emplace_back(ItemsOnCmdList{std::move(frontItem->_promise), std::move(res)});
					pendingPopFrontItem = true;		// pop it next time we have the lock
					if (!oldestItem.has_value()) oldestItem = _frameIdx;
				}

				const unsigned frameThreshold = 5;
				if (oldestItem.has_value() && (_frameIdx - oldestItem.value()) >= frameThreshold) {
					bkThreadContext->CommitCommands();
					// fulfill the promises for everything on this cmd list
					for (auto& i:itemsOnCmdList)
						i._promise.set_value(i._resource);
					itemsOnCmdList.clear();
					oldestItem = {};
				}
			}

			for (auto& i:itemsOnCmdList)
				i._promise.set_exception(std::make_exception_ptr(std::runtime_error("Shutdown before upload completed")));

			// note -- not releasing alocations in allocationsPendingRelease
		}};
	}

	BackgroundTextureUploader::~BackgroundTextureUploader()
	{
		_quit = true;
		_newlyQueued.notify_all();
		_workerThread.join();
	}

	TEST_CASE( "ResourceUpdateAndReadback-StagingTexturePattern", "[rendercore_metal]" )
	{
		using namespace RenderCore;
		auto testHelper = MakeTestHelper();
		auto threadContext = testHelper->_device->GetImmediateContext();
		auto shaderProgram = testHelper->MakeShaderProgram(vsText_clipInput, psText_TextureBinding);
		auto targetDesc = CreateDesc(
			BindFlag::RenderTarget | BindFlag::TransferSrc,
			TextureDesc::Plain2D(1024, 1024, Format::R8G8B8A8_UNORM),
			"temporary-out");

		UnitTestFBHelper fbHelper(*testHelper->_device, *threadContext, targetDesc, LoadStore::Clear);

		UniformsStreamInterface usi;
		usi.BindResourceView(0, Hash64("Texture"));
		usi.BindSampler(0, Hash64("Texture_sampler"));
		Metal::BoundUniforms uniforms { shaderProgram, usi };
		auto sampler = testHelper->_device->CreateSampler({});

		BackgroundTextureUploader uploads{testHelper->_device};

		std::vector<std::future<std::shared_ptr<IResource>>> futureResources;
		std::vector<std::shared_ptr<IResource>> completedResources;

		testHelper->BeginFrameCapture();

		const unsigned uploadCount = 100;
		std::mt19937_64 rng{4629462984};
		for (unsigned c=0; c<uploadCount; ++c) {
			std::future<std::shared_ptr<RenderCore::IResource>> future;
			if (std::uniform_int_distribution<>(0, 3)(rng) >= 1) {
				auto dims = std::uniform_int_distribution<>(3, 11)(rng);
				future = uploads.Queue(
					CreateDesc(BindFlag::ShaderResource, TextureDesc::Plain2D(1<<dims, 1<<dims, Format::R8G8B8A8_UNORM_SRGB, dims+1), "upload-test"),
					BindFlag::ShaderResource);
			} else {
				auto bufferSize = std::uniform_int_distribution<>(8*1024, 256*1024)(rng);
				future = uploads.Queue(CreateDesc(BindFlag::VertexBuffer, LinearBufferDesc::Create(bufferSize), "upload-test"), BindFlag::VertexBuffer);
			}
			futureResources.emplace_back(std::move(future));

			if ((c % 4) == 0) {
				while (!futureResources.empty() && futureResources.front().wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
					completedResources.push_back(futureResources.front().get());
					futureResources.erase(futureResources.begin());
				}

				// emulate drawing a frame
				{
					auto rpi = fbHelper.BeginRenderPass(*threadContext);
					auto& metalContext = *Metal::DeviceContext::Get(*threadContext);
					auto encoder = metalContext.BeginGraphicsEncoder_ProgressivePipeline(testHelper->_pipelineLayout);
					encoder.Bind(shaderProgram);

					if (!completedResources.empty()) {
						auto srv = completedResources.front()->CreateTextureView();
						IResourceView* views[] = { srv.get() };
						ISampler* samplers[] = { sampler.get() };
						UniformsStream uniformsStream;
						uniformsStream._resourceViews = MakeIteratorRange(views);
						uniformsStream._samplers = MakeIteratorRange(samplers);
						uniforms.ApplyLooseUniforms(metalContext, encoder, uniformsStream);

						DrawClipSpaceQuad(*testHelper, metalContext, encoder, shaderProgram, Float2(-1.0f, -1.0f), Float2( 1.0f, 1.0f));
					}
				}

				// we need to keep using CommitCommands on the immediate context to ensure that the producer/consumer markers are updated on vulkan
				threadContext->CommitCommands();
				uploads.Tick();
				std::this_thread::sleep_for(std::chrono::milliseconds(16));
			}
		}

		int c=0;
		(void)c;
		for (auto& f:futureResources) {
			while (f.wait_for(std::chrono::seconds(1)) != std::future_status::ready) {
				// keep things ticking over
				threadContext->CommitCommands();
				uploads.Tick();
			}
			f.get();
		}

		testHelper->EndFrameCapture();
	}

}
