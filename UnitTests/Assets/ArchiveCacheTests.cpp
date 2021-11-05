// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "../UnitTestHelper.h"
#include "../../Assets/ArchiveCache.h"
#include "../../Assets/AssetUtils.h"
#include "../../Assets/IArtifact.h"
#include "../../Assets/ICompileOperation.h"
#include "../../Assets/DepVal.h"
#include "../../Assets/IFileSystem.h"
#include "../../ConsoleRig/AttachablePtr.h"
#include "../../ConsoleRig/GlobalServices.h"
#include <stdexcept>
#include <filesystem>
#include <future>
#include <chrono>
#include "catch2/catch_test_macros.hpp"
#include "catch2/catch_approx.hpp"

using namespace std::chrono_literals;

using namespace Catch::literals;
namespace UnitTests
{
	static const auto ChunkType_Metrics = ConstHash64<'Metr', 'ics'>::Value;
	static const auto ChunkType_Log = ConstHash64<'Log'>::Value;

	static ::Assets::ICompileOperation::SerializedArtifact s_artifactsObj1[] {
		::Assets::ICompileOperation::SerializedArtifact {
			Hash64("artifact-one"),
			1,
			"artifact-one",
			::Assets::AsBlob("artifact-one-contents") },
		::Assets::ICompileOperation::SerializedArtifact {
			Hash64("artifact-two"),
			5,
			"artifact-two",
			::Assets::AsBlob("artifact-two-contents") },
		::Assets::ICompileOperation::SerializedArtifact {
			ChunkType_Metrics,
			1,
			"artifact-info",
			::Assets::AsBlob("This is metrics associated with a collection of artifacts") },
		::Assets::ICompileOperation::SerializedArtifact {
			ChunkType_Log,
			1,
			"artifact-more-info",
			::Assets::AsBlob("This is a log file associated with the item") }
	};

	static ::Assets::ICompileOperation::SerializedArtifact s_artifactsObj2[] {
		::Assets::ICompileOperation::SerializedArtifact {
			Hash64("artifact-one"),
			1,
			"item-two-artifact-one",
			::Assets::AsBlob("item-two-artifact-one-contents") },
		::Assets::ICompileOperation::SerializedArtifact {
			Hash64("artifact-two"),
			5,
			"item-two-artifact-two",
			::Assets::AsBlob("item-two-artifact-two-contents") },
		::Assets::ICompileOperation::SerializedArtifact {
			ChunkType_Metrics,
			1,
			"item-two-artifact-info",
			::Assets::AsBlob("item-two-metrics") },
		::Assets::ICompileOperation::SerializedArtifact {
			ChunkType_Log,
			1,
			"item-two-artifact-more-info",
			::Assets::AsBlob("item-two-log") }
	};

	static ::Assets::ICompileOperation::SerializedArtifact s_artifactsObj2Replacement[] {
		::Assets::ICompileOperation::SerializedArtifact {
			Hash64("artifact-one"),
			1,
			"item-two-replacement-artifact-one",
			::Assets::AsBlob("item-two-replacement-artifact-one-contents") },
		::Assets::ICompileOperation::SerializedArtifact {
			Hash64("artifact-two"),
			5,
			"item-two-replacement-artifact-two",
			::Assets::AsBlob("item-two-replacement-artifact-two-contents") },
		::Assets::ICompileOperation::SerializedArtifact {
			ChunkType_Log,
			1,
			"item-two-replacement-artifact-more-info",
			::Assets::AsBlob("item-two-replacement-log") }
	};

	static ::Assets::DependentFileState s_depFileStatesObj1[] {
		::Assets::DependentFileState {
			MakeStringSection("imaginary-file-one"), 3ull
		},
		::Assets::DependentFileState {
			MakeStringSection("imaginary-file-two"), 5ull
		}
	};

	static ::Assets::DependentFileState s_depFileStatesObj2[] {
		::Assets::DependentFileState {
			MakeStringSection("imaginary-file-three"), 56ull
		},
		::Assets::DependentFileState {
			MakeStringSection("imaginary-file-four"), 72ull
		}
	};

	TEST_CASE( "ArchiveCacheTests-CommitAndRetrieve", "[assets]" )
	{
		UnitTest_SetWorkingDirectory();
		auto globalServices = ConsoleRig::MakeAttachablePtr<ConsoleRig::GlobalServices>(GetStartupConfig());

		auto tempDirPath = std::filesystem::temp_directory_path() / "xle-unit-tests";
		std::filesystem::remove_all(tempDirPath);	// ensure we're starting from an empty temporary directory
		std::filesystem::create_directories(tempDirPath);

		ConsoleRig::LibVersionDesc dummyVersionDesc { "unit-test-version-str", "unit-test-build-date-string" };
		auto archiveFileName = (tempDirPath / "ArchiveCacheTests" / "archive").string();
		{
			::Assets::ArchiveCacheSet cacheSet(::Assets::MainFileSystem::GetDefaultFileSystem(), dummyVersionDesc);
			auto archive = cacheSet.GetArchive(archiveFileName);

			uint64_t objectOneId = Hash64("ObjectOne");
			archive->Commit(
				objectOneId, "Object",
				MakeIteratorRange(s_artifactsObj1),
				::Assets::AssetState::Ready,
				MakeIteratorRange(s_depFileStatesObj1));

			auto artifactCollection = archive->TryOpenFromCache(objectOneId);
			REQUIRE(artifactCollection);
			auto depVal = artifactCollection->GetDependencyValidation();
			REQUIRE(depVal);
			REQUIRE(depVal.GetValidationIndex() == 0);
			
			::Assets::ArtifactRequest requests[] {
				::Assets::ArtifactRequest { "--ignored--", Hash64("artifact-one"), 1, ::Assets::ArtifactRequest::DataType::SharedBlob },
				::Assets::ArtifactRequest { "--ignored--", Hash64("artifact-two"), 5, ::Assets::ArtifactRequest::DataType::Raw }
			};
			auto resolvedRequests = artifactCollection->ResolveRequests(MakeIteratorRange(requests));
			REQUIRE(resolvedRequests.size() == 2);
			REQUIRE(resolvedRequests[0]._sharedBlob);
			REQUIRE(::Assets::AsString(resolvedRequests[0]._sharedBlob) == "artifact-one-contents");
			REQUIRE(resolvedRequests[1]._buffer);
			REQUIRE(resolvedRequests[1]._bufferSize);

			cacheSet.FlushToDisk();

			// This is should still succeed; but now we're reading from disk, rather than the cached blobs
			auto reattemptResolve = artifactCollection->ResolveRequests(MakeIteratorRange(requests));
			REQUIRE(reattemptResolve.size() == 2);
			REQUIRE(reattemptResolve[0]._sharedBlob);
			REQUIRE(::Assets::AsString(reattemptResolve[0]._sharedBlob) == "artifact-one-contents");
			REQUIRE(reattemptResolve[1]._buffer);
			REQUIRE(reattemptResolve[1]._bufferSize);

			uint64_t objectTwoId = Hash64("ObjectTwo");
			archive->Commit(
				objectTwoId, "ObjectTwo",
				MakeIteratorRange(s_artifactsObj2),
				::Assets::AssetState::Ready,
				MakeIteratorRange(s_depFileStatesObj2));

			artifactCollection = archive->TryOpenFromCache(objectTwoId);
			REQUIRE(artifactCollection);

			cacheSet.FlushToDisk();

			archive->Commit(
				objectTwoId, "ObjectTwo",
				MakeIteratorRange(s_artifactsObj2Replacement),
				::Assets::AssetState::Ready,
				MakeIteratorRange(s_depFileStatesObj2));

			REQUIRE_THROWS(
				[&]() {
					// We should throw if we attempt to use the artifactCollection that was created before the last
					// commit (on the same object).
					// Since there's been a commit after the TryOpenFromCache, the artifact collection is considered
					// stale
					auto resolvedRequests = artifactCollection->ResolveRequests(MakeIteratorRange(requests));
					(void)resolvedRequests;
				}());

			::Assets::ArtifactRequest requests2[] {
				::Assets::ArtifactRequest { "--ignored--", Hash64("artifact-one"), 1, ::Assets::ArtifactRequest::DataType::SharedBlob },
				::Assets::ArtifactRequest { "--ignored--", Hash64("artifact-two"), 5, ::Assets::ArtifactRequest::DataType::SharedBlob }
			};

			artifactCollection = archive->TryOpenFromCache(objectTwoId);
			REQUIRE(artifactCollection);
			resolvedRequests = artifactCollection->ResolveRequests(MakeIteratorRange(requests2));
			REQUIRE(resolvedRequests.size() == 2);
			REQUIRE(resolvedRequests[0]._sharedBlob);
			REQUIRE(::Assets::AsString(resolvedRequests[0]._sharedBlob) == "item-two-replacement-artifact-one-contents");
			REQUIRE(resolvedRequests[1]._sharedBlob);
			REQUIRE(::Assets::AsString(resolvedRequests[1]._sharedBlob) == "item-two-replacement-artifact-two-contents");

			cacheSet.FlushToDisk();

			// Commit ObjectOne again, except this time with no dependency
			// information. This sets us up to reload just below. Since our dependencies
			// are fake files (ie, they don't actually exist anywhere), the asset gets
			// considered invalidated when it's loaded
			archive->Commit(
				objectOneId, "Object",
				MakeIteratorRange(s_artifactsObj1),
				::Assets::AssetState::Ready,
				{});
		}

		{
			// When we close and reopen the cache set, we should still be able to get out the same results
			::Assets::ArchiveCacheSet cacheSet(::Assets::MainFileSystem::GetDefaultFileSystem(), dummyVersionDesc);
			auto archive = cacheSet.GetArchive(archiveFileName);

			auto artifactCollection = archive->TryOpenFromCache(Hash64("ObjectOne"));
			REQUIRE(artifactCollection);
			auto depVal = artifactCollection->GetDependencyValidation();
			REQUIRE(depVal);
			REQUIRE(depVal.GetValidationIndex() == 0);

			::Assets::ArtifactRequest requests[] {
				::Assets::ArtifactRequest { "--ignored--", Hash64("artifact-one"), 1, ::Assets::ArtifactRequest::DataType::SharedBlob },
				::Assets::ArtifactRequest { "--ignored--", Hash64("artifact-two"), 5, ::Assets::ArtifactRequest::DataType::Raw }
			};
			auto resolvedRequests = artifactCollection->ResolveRequests(MakeIteratorRange(requests));
			REQUIRE(resolvedRequests.size() == 2);
			REQUIRE(resolvedRequests[0]._sharedBlob);
			REQUIRE(::Assets::AsString(resolvedRequests[0]._sharedBlob) == "artifact-one-contents");
			REQUIRE(resolvedRequests[1]._buffer);
			REQUIRE(resolvedRequests[1]._bufferSize);
		}
	}

	TEST_CASE( "General-StandardFutures", "[assets]" )
	{
		// creating a future after the promise is fullfilled
		// sharing a future after the original future has been queried
		// is there a shared_ptr within the promise?
		// querying the future with wait_for multiple times?
		// moving a promise with a future attached
		// cost of wait_for

		struct PromisedType
		{
			std::shared_ptr<void> _asset;
			::Assets::Blob _actualizationLog;
		};
		std::promise<PromisedType> promise;
		auto future = promise.get_future();

		promise.set_value(PromisedType{});
		REQUIRE(future.wait_for(0s) == std::future_status::ready);
		auto gotValue = future.get();

		// we can't query or wait for the future after query
		REQUIRE_THROWS([&]() {future.wait_for(0s);}());
		REQUIRE_THROWS([&]() {future.get();}());

		// we can share after query, but we end up with a useless shared_future<>
		auto sharedAfterQuery = future.share();
		REQUIRE_THROWS([&]() {sharedAfterQuery.wait_for(0s);}());
		REQUIRE_THROWS([&]() {sharedAfterQuery.get();}());

		// we can't get a second future from promise
		// and we can't call set_value() a second time
		REQUIRE_THROWS([&]() {promise.get_future();}());
		REQUIRE_THROWS([&]() {promise.set_value(PromisedType{});}());

		// however we can reset and reuse the same promise
		promise = {};
		promise.set_value(PromisedType{});

		// get future from promise after fullfilling it
		auto secondFuture = promise.get_future();
		REQUIRE(secondFuture.wait_for(0s) == std::future_status::ready);
		gotValue = secondFuture.get();

		// shared future hyjinks
		std::promise<PromisedType> promiseForSharedFuture;
		promiseForSharedFuture.set_value(PromisedType{});
		auto sharedFuture = promiseForSharedFuture.get_future().share();
		REQUIRE(sharedFuture.wait_for(0s) == std::future_status::ready);
		gotValue = sharedFuture.get();

		// waiting for and calling get() on a shared_future is valid even after the
		// first query
		REQUIRE(sharedFuture.wait_for(0s) == std::future_status::ready);
		gotValue = sharedFuture.get();

		std::shared_future<PromisedType> secondSharedFuture = sharedFuture;
		REQUIRE(secondSharedFuture.wait_for(0s) == std::future_status::ready);
		gotValue = secondSharedFuture.get();

		// copy constructor off the copied future
		std::shared_future<PromisedType> thirdSharedFuture = secondSharedFuture;
		REQUIRE(thirdSharedFuture.wait_for(0s) == std::future_status::ready);
		gotValue = thirdSharedFuture.get();

		// waiting for the original shared future still valid
		REQUIRE(sharedFuture.wait_for(0s) == std::future_status::ready);
		gotValue = sharedFuture.get();

		// does a promise loose contact with it's futures after it's moved?
		promise = {};
		auto futureToExplode = promise.get_future();
		REQUIRE(futureToExplode.wait_for(0s) == std::future_status::timeout);
		std::promise<PromisedType> moveDstPromise = std::move(promise);

		// we can't use a promise that was just used as a move src
		REQUIRE_THROWS([&]() {promise.set_value(PromisedType{});}());
		
		REQUIRE(futureToExplode.wait_for(0s) == std::future_status::timeout);
		moveDstPromise.set_value(PromisedType{});
		REQUIRE(futureToExplode.wait_for(0s) == std::future_status::ready);

		// Internally std::promise<> holds a pointer to another object. In the VS libraries, it's called _Associated_state
		// This contains a mutex and condition variable. The promised type is stored within the same heap block
		// calling wait_for() always invokes a mutex lock/unlock and std::condition_variable::wait_for combo 
	}
}
