// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "../Metal/MetalTestHelper.h"
#include "../../EmbeddedRes.h"
#include "../../UnitTestHelper.h"
#include "../../../BufferUploads/IBufferUploads.h"
#include "../../../BufferUploads/Metrics.h"
#include "../../../RenderCore/Assets/TextureLoaders.h"
#include "../../../RenderCore/ResourceDesc.h"
#include "../../../RenderCore/Format.h"
#include "../../../RenderCore/IDevice.h"
#include "../../../RenderCore/Metal/DeviceContext.h"
#include "../../../RenderCore/Metal/Resource.h"
#include "../../../ConsoleRig/GlobalServices.h"
#include "../../../ConsoleRig/AttachablePtr.h"
#include "../../../OSServices/Log.h"
#include "../../../Assets/IFileSystem.h"
#include "../../../Assets/MountingTree.h"
#include "../../../Utility/HeapUtils.h"
#include "thousandeyes/futures/then.h"
#include "thousandeyes/futures/DefaultExecutor.h"
#include "catch2/catch_test_macros.hpp"
#include "catch2/catch_approx.hpp"
#include <chrono>
#include <future>
#include <random>

using namespace Catch::literals;
using namespace std::chrono_literals;
namespace UnitTests
{
	class AsyncDataSource : public BufferUploads::IAsyncDataSource
	{
	public:
		std::future<RenderCore::ResourceDesc> GetDesc () override
		{
			return std::async(
				std::launch::async,
				[capturedDesc{_desc}]() {
					std::this_thread::sleep_for(500ms);
					return capturedDesc;
				});
		}

		std::future<void> PrepareData(IteratorRange<const SubResource*> subResources) override
		{
			std::vector<SubResource> subResourceDst_;
			subResourceDst_.insert(subResourceDst_.end(), subResources.begin(), subResources.end());
			return std::async(
				std::launch::async,
				[capturedDesc{_desc}, capturedData{_rawData}, subResourceDst{std::move(subResourceDst_)}]() {
					std::this_thread::sleep_for(1500ms);

					assert(subResourceDst[0]._destination.size() == capturedData.size() * sizeof(unsigned));
					std::copy(
						capturedData.begin(), capturedData.end(),
						(unsigned*)subResourceDst[0]._destination.begin());
				});
		}

		::Assets::DependencyValidation GetDependencyValidation() const override { return {}; }

		std::vector<unsigned> _rawData;
		RenderCore::ResourceDesc _desc;
	};

	TEST_CASE( "BufferUploads-TextureInitialization", "[rendercore_techniques]" )
	{
		using namespace RenderCore;
		auto globalServices = ConsoleRig::MakeAttachablePtr<ConsoleRig::GlobalServices>(GetStartupConfig());
		auto metalHelper = MakeTestHelper();
		auto bu = BufferUploads::CreateManager(*metalHelper->_device);

		std::vector<unsigned> rawData;
		rawData.resize(256*256, 0xff7fff7f);
		auto desc = CreateDesc(
			BindFlag::ShaderResource, TextureDesc::Plain2D(256, 256, Format::R8G8B8A8_UNORM),
			"bu-test-texture");

		SECTION("Prepared data packet")
		{
			
			auto dataPacket = BufferUploads::CreateBasicPacket(MakeIteratorRange(rawData), MakeTexturePitches(desc._textureDesc));
			auto transaction = bu->Transaction_Begin(desc, dataPacket);
			REQUIRE(transaction.IsValid());

			auto start = std::chrono::steady_clock::now();
			for (;;) {
				bu->Update(*metalHelper->_device->GetImmediateContext());
				auto status = transaction._future.wait_for(100ms);
				if (status == std::future_status::ready)
					break;

				if ((std::chrono::steady_clock::now() - start) > 5s)
					FAIL("Too much time has passed waiting for buffer uploads transaction to complete");
			}

			bu->Update(*metalHelper->_device->GetImmediateContext());

			auto finalResource = transaction._future.get().AsIndependentResource();
			REQUIRE(finalResource != nullptr);
			auto finalResourceDesc = finalResource->GetDesc();
			REQUIRE(finalResourceDesc._type == ResourceDesc::Type::Texture);
			REQUIRE(finalResourceDesc._textureDesc._width == desc._textureDesc._width);
			REQUIRE(finalResourceDesc._textureDesc._height == desc._textureDesc._height);
		}

		SECTION("Async construction")
		{
			auto dataSource = std::make_shared<AsyncDataSource>();
			dataSource->_desc = desc;
			dataSource->_rawData = rawData;

			auto transaction = bu->Transaction_Begin(dataSource);
			REQUIRE(transaction.IsValid());

			auto start = std::chrono::steady_clock::now();
			for (;;) {
				bu->Update(*metalHelper->_device->GetImmediateContext());
				auto status = transaction._future.wait_for(100ms);
				if (status == std::future_status::ready)
					break;

				if ((std::chrono::steady_clock::now() - start) > 5s)
					FAIL("Too much time has passed waiting for buffer uploads transaction to complete");
			}

			bu->Update(*metalHelper->_device->GetImmediateContext());

			auto finalResource = transaction._future.get().AsIndependentResource();
			REQUIRE(finalResource != nullptr);
			auto finalResourceDesc = finalResource->GetDesc();
			REQUIRE(finalResourceDesc._type == ResourceDesc::Type::Texture);
			REQUIRE(finalResourceDesc._textureDesc._width == desc._textureDesc._width);
			REQUIRE(finalResourceDesc._textureDesc._height == desc._textureDesc._height);
		}
	}

	TEST_CASE( "BufferUploads-TextureFileLoading", "[rendercore_techniques]" )
	{
		using namespace RenderCore;
		auto globalServices = ConsoleRig::MakeAttachablePtr<ConsoleRig::GlobalServices>(GetStartupConfig());
		auto mnt0 = ::Assets::MainFileSystem::GetMountingTree()->Mount("xleres", UnitTests::CreateEmbeddedResFileSystem());

		auto metalHelper = MakeTestHelper();
		auto bu = BufferUploads::CreateManager(*metalHelper->_device);

		auto ddsLoader = RenderCore::Assets::CreateDDSTextureLoader();
		auto wicLoader = RenderCore::Assets::CreateWICTextureLoader();
		const char* texturesToTry[] {
			"xleres/DefaultResources/glosslut.dds",
			"xleres/DefaultResources/waternoise.png"
		};

		for (unsigned c=0; c<dimof(texturesToTry); ++c) {
			INFO("Loading: " + std::string{texturesToTry[c]});
			std::shared_ptr<BufferUploads::IAsyncDataSource> asyncSource;
			if (XlEndsWithI(MakeStringSection(texturesToTry[c]), MakeStringSection(".dds"))) {
				asyncSource = ddsLoader(texturesToTry[c], 0);
			} else {
				asyncSource = wicLoader(texturesToTry[c], RenderCore::Assets::TextureLoaderFlags::GenerateMipmaps);
			}

			auto transaction = bu->Transaction_Begin(asyncSource, BindFlag::ShaderResource | BindFlag::TransferSrc);
			REQUIRE(transaction.IsValid());

			auto start = std::chrono::steady_clock::now();
			for (;;) {
				bu->Update(*metalHelper->_device->GetImmediateContext());
				auto status = transaction._future.wait_for(100ms);
				if (status == std::future_status::ready)
					break;

				if ((std::chrono::steady_clock::now() - start) > 5s)
					FAIL("Too much time has passed waiting for buffer uploads future to become ready");
			}

			auto finalLocator = transaction._future.get();
			REQUIRE(!finalLocator.IsEmpty());
			REQUIRE(finalLocator.GetCompletionCommandList() != ~0u);
			start = std::chrono::steady_clock::now();
			while (!bu->IsComplete(finalLocator.GetCompletionCommandList())) {
				bu->Update(*metalHelper->_device->GetImmediateContext());
				std::this_thread::sleep_for(16ms);
				if ((std::chrono::steady_clock::now() - start) > 5s)
					FAIL("Too much time has passed waiting for buffer uploads transaction to complete");
			}

			auto finalResource = finalLocator.AsIndependentResource();
			REQUIRE(finalResource != nullptr);

			auto finalResourceDesc = finalResource->GetDesc();
			REQUIRE(finalResourceDesc._type == ResourceDesc::Type::Texture);

			// Copy to a destaging buffer and then read the data
			auto destagingDesc = finalResourceDesc;
			destagingDesc._bindFlags = BindFlag::TransferDst;
			destagingDesc._allocationRules = AllocationRules::HostVisibleRandomAccess;
			auto destaging = metalHelper->_device->CreateResource(destagingDesc);
			{
				auto blitEncoder = Metal::DeviceContext::Get(*metalHelper->_device->GetImmediateContext())->BeginBlitEncoder();
				blitEncoder.Copy(*destaging, *finalResource);
			}
			metalHelper->_device->GetImmediateContext()->CommitCommands(CommitCommandsFlags::WaitForCompletion);
			Metal::ResourceMap map(
				*Metal::DeviceContext::Get(*metalHelper->_device->GetImmediateContext()),
				*destaging, Metal::ResourceMap::Mode::Read);
			auto data = map.GetData(SubResourceId{});
			std::stringstream str;
			for (unsigned c=0; c<64; ++c)
				str << std::hex << (unsigned)((uint8_t*)data.begin())[c] << " ";
			INFO(str.str());
			REQUIRE(((unsigned*)data.begin())[0] != 0);     // let's just entire the first few bytes are not all zeroes
			REQUIRE(((unsigned*)data.begin())[1] != 0);
			REQUIRE(((unsigned*)data.begin())[2] != 0);
			REQUIRE(((unsigned*)data.begin())[3] != 0);
		}

		::Assets::MainFileSystem::GetMountingTree()->Unmount(mnt0);
	}

	static void FillWithRandomData(uint32_t rngSeed, IteratorRange<void*> dstRange)
	{
		std::mt19937 rng(rngSeed);
		for (unsigned& dst:dstRange.Cast<unsigned*>())
			dst = rng();
	}

	class RandomNoiseGenerator : public BufferUploads::IAsyncDataSource
	{
	public:
		std::future<RenderCore::ResourceDesc> GetDesc () override
		{
			std::promise<RenderCore::ResourceDesc> result;
			result.set_value(_desc);
			return result.get_future();
		}

		std::future<void> PrepareData(IteratorRange<const SubResource*> subResources) override
		{
			std::vector<SubResource> subResourceDst_;
			subResourceDst_.insert(subResourceDst_.end(), subResources.begin(), subResources.end());
			return std::async(
				std::launch::async,
				[rngSeed = _rngSeed, subResourceDst{std::move(subResourceDst_)}]() {
					std::mt19937 rng(rngSeed);
					for (const auto&subRes:subResourceDst)
						FillWithRandomData(rng(), subRes._destination);
				});
		}

		::Assets::DependencyValidation GetDependencyValidation() const override { return {}; }

		RenderCore::ResourceDesc _desc;
		uint32_t _rngSeed;

		RandomNoiseGenerator(unsigned width, unsigned height, uint32_t rngSeed)
		: _rngSeed(rngSeed)
		{
			_desc = RenderCore::CreateDesc(
				0, RenderCore::TextureDesc::Plain2D(width, height, RenderCore::Format::R8G8B8A8_UNORM),
				"rng");
		}
	};

	struct TransactionTestHelper
	{
		struct Marker
		{
			std::future<BufferUploads::ResourceLocator> _future;
			BufferUploads::ResourceLocator _retrieved;
		};
		std::vector<Marker> _liveTransactions;
		unsigned _incrementalTransactionCounter = 0;

		void RemoveCompletedTransactions(BufferUploads::IManager& manager)
		{
			// Note -- some of the transactions may fail (eg, out of the device space). However, they
			// are stil considered complete and we should be able to continue on
			auto i = std::remove_if(
				_liveTransactions.begin(), _liveTransactions.end(),
				[&manager](auto& transaction) {
					if (transaction._retrieved.IsEmpty()) {
						auto futureState = transaction._future.wait_for(0s);
						if (futureState == std::future_status::timeout)
							return false;
						transaction._retrieved = transaction._future.get();
					}
					return manager.IsComplete(transaction._retrieved.GetCompletionCommandList());
				});
			_liveTransactions.erase(i, _liveTransactions.end());
		}

		void AddTransaction(BufferUploads::TransactionMarker&& marker)
		{
			REQUIRE(marker.IsValid());
			_liveTransactions.push_back(Marker{std::move(marker._future), BufferUploads::ResourceLocator{}});
			++_incrementalTransactionCounter;
		}

		void Report(BufferUploads::IManager& bu)
		{
			Log(Verbose) << bu.PopMetrics() << std::endl;
			Log(Verbose) << bu.CalculatePoolMetrics() << std::endl;
			Log(Verbose) << "Live Transactions: " << _liveTransactions.size() << std::endl;
			Log(Verbose) << "Incremental Transactions: " << _incrementalTransactionCounter << std::endl;
		}
	};

	TEST_CASE( "BufferUploads-TextureConstructionThrash", "[rendercore_techniques]" )
	{
		using namespace RenderCore;
		auto globalServices = ConsoleRig::MakeAttachablePtr<ConsoleRig::GlobalServices>(GetStartupConfig());
		auto metalHelper = MakeTestHelper();
		auto bu = BufferUploads::CreateManager(*metalHelper->_device);

		const unsigned steadyPoint = 384;
		TransactionTestHelper transactionHelper;
		transactionHelper._liveTransactions.reserve(steadyPoint);

		std::mt19937 rng(0);
		unsigned loopCounter = 0;
		auto startTime = std::chrono::steady_clock::now();
		for (;;) {
			unsigned texturesToSpawn = (unsigned)std::sqrt(steadyPoint - transactionHelper._liveTransactions.size());
			for (unsigned t=0; t<texturesToSpawn; ++t) {
				auto asyncSource = std::make_shared<RandomNoiseGenerator>(
					1 << std::uniform_int_distribution<>(5, 10)(rng),
					1 << std::uniform_int_distribution<>(5, 10)(rng),
					rng());
				transactionHelper.AddTransaction(bu->Transaction_Begin(asyncSource, BindFlag::ShaderResource));
			}

			bu->Update(*metalHelper->_device->GetImmediateContext());
			metalHelper->_device->GetImmediateContext()->CommitCommands();
			transactionHelper.RemoveCompletedTransactions(*bu);
			std::this_thread::sleep_for(16ms);
			
			loopCounter++;
			if ((loopCounter%60) == 0) {
				transactionHelper.Report(*bu);
				// Only every finish immediately after a report
				if ((std::chrono::steady_clock::now() - startTime) > 20s)
					break;
			}
		}
	}

	TEST_CASE( "BufferUploads-LinearBufferAllocation", "[rendercore_techniques]" )
	{
		using namespace RenderCore;
		auto globalServices = ConsoleRig::MakeAttachablePtr<ConsoleRig::GlobalServices>(GetStartupConfig());
		auto metalHelper = MakeTestHelper();
		auto bu = BufferUploads::CreateManager(*metalHelper->_device);

		const unsigned steadyPoint = 384;
		TransactionTestHelper transactionHelper;
		transactionHelper._liveTransactions.reserve(steadyPoint);
		
		std::mt19937 rng(0);
		unsigned loopCounter = 0;

		auto startTime = std::chrono::steady_clock::now();
		for (;;) {
			unsigned buffersToSpawn = (unsigned)std::sqrt(steadyPoint - transactionHelper._liveTransactions.size());
			for (unsigned t=0; t<buffersToSpawn; ++t) {
				auto size = 1024 * std::uniform_int_distribution<>(8, 64)(rng);
				auto pkt = BufferUploads::CreateEmptyLinearBufferPacket(size);
				FillWithRandomData(rng(), pkt->GetData());
				auto desc = CreateDesc(BindFlag::IndexBuffer, LinearBufferDesc::Create(size, size), "rng-buffer");
				if (std::uniform_int_distribution<>(0, 1)(rng) == 0)
					desc._bindFlags = BindFlag::VertexBuffer;
				// desc._allocationRules |= AllocationRules::Batched | AllocationRules::Pooled;
				
				transactionHelper.AddTransaction(bu->Transaction_Begin(desc, pkt));
			}

			bu->Update(*metalHelper->_device->GetImmediateContext());
			transactionHelper.RemoveCompletedTransactions(*bu);
			std::this_thread::sleep_for(16ms);
			metalHelper->_device->GetImmediateContext()->CommitCommands();

			loopCounter++;
			if ((loopCounter%60) == 0) {
				transactionHelper.Report(*bu);
				// Only every finish immediately after a report
				if ((std::chrono::steady_clock::now() - startTime) > 20s)
					break;
			}
		}
	}

	TEST_CASE( "BufferUploads-SimpleBackgroundCmdList", "[rendercore_techniques]" )
	{
		// Emulate the kind of behaviour that buffer uploads does, just in a very
		// simple and controlled way. This can help us isolate issues between the metal
		// layer and with buffer uploads
		using namespace RenderCore;
		auto globalServices = ConsoleRig::MakeAttachablePtr<ConsoleRig::GlobalServices>(GetStartupConfig());
		auto metalHelper = MakeTestHelper();
		std::shared_ptr<IThreadContext> backgroundContext = metalHelper->_device->CreateDeferredContext();

		std::mt19937 rng(0);
		
		struct Queue
		{
			struct Item
			{
				std::vector<std::shared_ptr<IResource>> _finalResources;
				std::vector<std::shared_ptr<IResource>> _stagingResources;
				std::shared_ptr<Metal::CommandList> _cmdList;
			};
			std::deque<Item> _items;
			Threading::Mutex _lock;
		};
		Queue queue;

		auto startTime = std::chrono::steady_clock::now();
		for (;;) {
			std::thread thread(
				[backgroundContext, dev = metalHelper->_device, seed = rng(), &queue]() {
					auto& metalContext = *Metal::DeviceContext::Get(*backgroundContext);
					auto finalResourceDesc = CreateDesc(
						BindFlag::ShaderResource | BindFlag::TransferDst, TextureDesc::Plain2D(256, 256, Format::R8G8B8A8_UNORM),
						"bu-test-texture");

					auto stagingResourceDesc = CreateDesc(
						BindFlag::TransferSrc, AllocationRules::HostVisibleSequentialWrite, TextureDesc::Plain2D(256, 256, Format::R8G8B8A8_UNORM),
						"bu-test-staging");

					Queue::Item item;
					std::mt19937 rng(seed);

					for (unsigned c=0; c<8; ++c) {
						auto stagingResource = dev->CreateResource(stagingResourceDesc);
						auto finalResource = dev->CreateResource(finalResourceDesc);

						Metal::ResourceMap map{metalContext, *stagingResource, Metal::ResourceMap::Mode::WriteDiscardPrevious};
						FillWithRandomData(rng(), map.GetData());

						Metal::Internal::CaptureForBind cap0{metalContext, *stagingResource, BindFlag::TransferSrc};
						Metal::Internal::CaptureForBind cap1{metalContext, *finalResource, BindFlag::TransferDst};
						auto blitEncoder = metalContext.BeginBlitEncoder();
						blitEncoder.Copy(*finalResource, *stagingResource);

						item._finalResources.push_back(std::move(finalResource));
						item._stagingResources.push_back(std::move(stagingResource));
					}
					
					item._cmdList = metalContext.ResolveCommandList();

					ScopedLock(queue._lock);
					queue._items.push_back(std::move(item));
				});

			thread.join();

			ScopedLock(queue._lock);
			while (queue._items.size() >= 2) {
				auto& immediateContext = *Metal::DeviceContext::Get(*metalHelper->_device->GetImmediateContext());
				immediateContext.ExecuteCommandList(std::move(*queue._items.begin()->_cmdList));
				queue._items.erase(queue._items.begin());
			}

			metalHelper->_device->GetImmediateContext()->CommitCommands();
			std::this_thread::sleep_for(16ms);

			if ((std::chrono::steady_clock::now() - startTime) > 20s)
				break;
		}
	}

	TEST_CASE( "BufferUploads-Heap", "[rendercore_techniques]" )
	{
		struct HeapTest_Allocation 
		{
			unsigned _start, _size;
			unsigned _begin, _end;
			signed _refCount;
		};

		SECTION("SimpleSpanningHeap")
		{
				//      Verify the heap functionality by running through many test allocations/deallocations
			SimpleSpanningHeap heap(0x8000);
			std::vector<std::pair<unsigned,unsigned>> allocations;
			for (unsigned c=0; c<10000; ++c) {
				if ((rand()%3)==0) {
					if (allocations.size()) {
						unsigned originalSpace = heap.CalculateAvailableSpace();
						std::vector<std::pair<unsigned,unsigned>>::iterator i = allocations.begin() + (rand()%allocations.size());
						bool result = heap.Deallocate(i->first, i->second);
						REQUIRE(result);
						if (result) {
							unsigned newSize = heap.CalculateAvailableSpace();
							REQUIRE(abs(int(newSize - (originalSpace+i->second)))<=16);
						}
						allocations.erase(i);
					}
				} else {
					unsigned originalSpace = heap.CalculateAvailableSpace();
					unsigned size = rand()%0x800 + 0x1;
					unsigned allocation = heap.Allocate(size);
					if (allocation != ~unsigned(0x0)) {
						allocations.push_back(std::make_pair(allocation, size));
						unsigned newSize = heap.CalculateAvailableSpace();
						REQUIRE(abs(int(newSize - (originalSpace-size)))<=16);
					}
				}

				SimpleSpanningHeap dupe = heap;
				dupe.PerformReposition(dupe.CalculateHeapCompression());
			}

			for (std::vector<std::pair<unsigned,unsigned>>::iterator i=allocations.begin(); i!=allocations.end(); ++i) {
				heap.Deallocate(i->first, i->second);
			}

				//      ...Defrag test...
			for (unsigned c=0; c<100; ++c) {
				unsigned originalSpace = heap.CalculateAvailableSpace();
				unsigned size = rand()%0x800 + 0x1;
				unsigned allocation = heap.Allocate(size);
				if (allocation != ~unsigned(0x0)) {
					allocations.push_back(std::make_pair(allocation, size));
					unsigned newSize = heap.CalculateAvailableSpace();
					REQUIRE(abs(int(newSize - (originalSpace-size)))<=16);
				}
			}
			heap.PerformReposition(heap.CalculateHeapCompression());
		}

		SECTION("ReferenceCountingLayer")
		{
			using namespace BufferUploads;
			ReferenceCountingLayer layer(0x8000);

			layer.AddRef(0x1c0, 0x10);
			layer.AddRef(0x1d0, 0x10);
			layer.AddRef(0x100, 0x100);
			layer.Release(0x100, 0x100);
			layer.Release(0x1c0, 0x10);
			layer.Release(0x1d0, 0x10);

			std::vector<HeapTest_Allocation> allocations;
			HeapTest_Allocation starterAllocation;
			starterAllocation._start = 0;
			starterAllocation._size = 0x8000;
			starterAllocation._refCount = 1;
			starterAllocation._begin = ReferenceCountingLayer::ToInternalSize(starterAllocation._start);
			starterAllocation._end = ReferenceCountingLayer::ToInternalSize(starterAllocation._start) + ReferenceCountingLayer::ToInternalSize(ReferenceCountingLayer::AlignSize(starterAllocation._size));
			layer.AddRef(starterAllocation._start, starterAllocation._size);
			allocations.push_back(starterAllocation);
			for (unsigned c=0; c<100000; ++c) {
				size_t valueStart = layer.Validate();
				if ((rand()%3)==0) {
					HeapTest_Allocation& a = allocations[rand()%allocations.size()];
					if (a._refCount) {
						--a._refCount;
						layer.Release(a._start, a._size);
						size_t valueEnd = layer.Validate();
						REQUIRE(valueStart == valueEnd+(a._end-a._begin));
					}
				} else {
					HeapTest_Allocation& a = allocations[rand()%allocations.size()];
					if (a._size > 128) {
						// ReferenceCountingLayer originalLayer = layer;
						HeapTest_Allocation all;
						all._start = rand()%(a._size-32);
						all._size = std::min(a._size-all._start, 32+rand()%(a._size-64));
						all._refCount = 1;
						all._begin = ReferenceCountingLayer::ToInternalSize(all._start);
						all._end = ReferenceCountingLayer::ToInternalSize(all._start) + ReferenceCountingLayer::ToInternalSize(ReferenceCountingLayer::AlignSize(all._size));
						allocations.push_back(all);
						layer.AddRef(all._start, all._size);
						size_t valueEnd = layer.Validate();
						REQUIRE(valueEnd == valueStart+(all._end-all._begin));
						// if (valueEnd != valueStart+(all._end-all._begin)) {
						//     originalLayer.AddRef(all._start, all._size);
						// }
					}
				}
			}

			for (std::vector<HeapTest_Allocation>::iterator i=allocations.begin(); i!=allocations.end(); ++i) {
				while (i->_refCount--) {
					layer.Release(i->_start, i->_size);
				}
			}

			layer.Validate();
			REQUIRE(layer.GetEntryCount() == 0);
		}
	}
}

