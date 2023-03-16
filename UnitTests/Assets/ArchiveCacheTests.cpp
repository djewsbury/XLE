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
#include "catch2/catch_test_macros.hpp"
#include "catch2/catch_approx.hpp"

using namespace Catch::literals;
using namespace Utility::Literals;

namespace UnitTests
{
	static const auto ChunkType_Metrics = ConstHash64Legacy<'Metr', 'ics'>::Value;
	static const auto ChunkType_Log = ConstHash64Legacy<'Log'>::Value;

	static ::Assets::SerializedArtifact s_artifactsObj1[] {
		::Assets::SerializedArtifact {
			"artifact-one"_h,
			1,
			"artifact-one",
			::Assets::AsBlob("artifact-one-contents") },
		::Assets::SerializedArtifact {
			"artifact-two"_h,
			5,
			"artifact-two",
			::Assets::AsBlob("artifact-two-contents") },
		::Assets::SerializedArtifact {
			ChunkType_Metrics,
			1,
			"artifact-info",
			::Assets::AsBlob("This is metrics associated with a collection of artifacts") },
		::Assets::SerializedArtifact {
			ChunkType_Log,
			1,
			"artifact-more-info",
			::Assets::AsBlob("This is a log file associated with the item") }
	};

	static ::Assets::SerializedArtifact s_artifactsObj2[] {
		::Assets::SerializedArtifact {
			"artifact-one"_h,
			1,
			"item-two-artifact-one",
			::Assets::AsBlob("item-two-artifact-one-contents") },
		::Assets::SerializedArtifact {
			"artifact-two"_h,
			5,
			"item-two-artifact-two",
			::Assets::AsBlob("item-two-artifact-two-contents") },
		::Assets::SerializedArtifact {
			ChunkType_Metrics,
			1,
			"item-two-artifact-info",
			::Assets::AsBlob("item-two-metrics") },
		::Assets::SerializedArtifact {
			ChunkType_Log,
			1,
			"item-two-artifact-more-info",
			::Assets::AsBlob("item-two-log") }
	};

	static ::Assets::SerializedArtifact s_artifactsObj2Replacement[] {
		::Assets::SerializedArtifact {
			"artifact-one"_h,
			1,
			"item-two-replacement-artifact-one",
			::Assets::AsBlob("item-two-replacement-artifact-one-contents") },
		::Assets::SerializedArtifact {
			"artifact-two"_h,
			5,
			"item-two-replacement-artifact-two",
			::Assets::AsBlob("item-two-replacement-artifact-two-contents") },
		::Assets::SerializedArtifact {
			ChunkType_Log,
			1,
			"item-two-replacement-artifact-more-info",
			::Assets::AsBlob("item-two-replacement-log") }
	};

	static ::Assets::DependentFileState s_depFileStatesObj1[] {
		::Assets::DependentFileState {
			MakeStringSection("imaginary-file-one"), 0ull, ::Assets::FileSnapshot::State::DoesNotExist
		},
		::Assets::DependentFileState {
			MakeStringSection("imaginary-file-two"), 0ull, ::Assets::FileSnapshot::State::DoesNotExist
		}
	};

	static ::Assets::DependentFileState s_depFileStatesObj2[] {
		::Assets::DependentFileState {
			MakeStringSection("imaginary-file-three"), 0ull, ::Assets::FileSnapshot::State::DoesNotExist
		},
		::Assets::DependentFileState {
			MakeStringSection("imaginary-file-four"), 0ull, ::Assets::FileSnapshot::State::DoesNotExist
		}
	};

	TEST_CASE( "ArchiveCacheTests-CommitAndRetrieve", "[assets]" )
	{
		auto globalServices = ConsoleRig::MakeAttachablePtr<ConsoleRig::GlobalServices>(GetStartupConfig());

		auto tempDirPath = std::filesystem::temp_directory_path() / "xle-unit-tests" / "ArchiveCacheTests";
		std::filesystem::create_directories(tempDirPath);
		std::filesystem::remove_all(tempDirPath);	// ensure we're starting from an empty temporary directory

		OSServices::LibVersionDesc dummyVersionDesc { "unit-test-version-str", "unit-test-build-date-string" };
		auto archiveFileName = (tempDirPath / "archive").string();
		{
			::Assets::ArchiveCacheSet cacheSet(::Assets::MainFileSystem::GetDefaultFileSystem(), dummyVersionDesc);
			auto archive = cacheSet.GetArchive(archiveFileName);

			constexpr uint64_t objectOneId = "ObjectOne"_h;
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
			
			constexpr ::Assets::ArtifactRequest requests[] {
				::Assets::ArtifactRequest { "--ignored--", "artifact-one"_h, 1, ::Assets::ArtifactRequest::DataType::SharedBlob },
				::Assets::ArtifactRequest { "--ignored--", "artifact-two"_h, 5, ::Assets::ArtifactRequest::DataType::Raw }
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

			constexpr uint64_t objectTwoId = "ObjectTwo"_h;
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

			constexpr ::Assets::ArtifactRequest requests2[] {
				::Assets::ArtifactRequest { "--ignored--", "artifact-one"_h, 1, ::Assets::ArtifactRequest::DataType::SharedBlob },
				::Assets::ArtifactRequest { "--ignored--", "artifact-two"_h, 5, ::Assets::ArtifactRequest::DataType::SharedBlob }
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
		}

		{
			// When we close and reopen the cache set, we should still be able to get out the same results
			::Assets::ArchiveCacheSet cacheSet(::Assets::MainFileSystem::GetDefaultFileSystem(), dummyVersionDesc);
			auto archive = cacheSet.GetArchive(archiveFileName);

			auto artifactCollection = archive->TryOpenFromCache("ObjectOne"_h);
			REQUIRE(artifactCollection);
			auto depVal = artifactCollection->GetDependencyValidation();
			REQUIRE(depVal);
			REQUIRE(depVal.GetValidationIndex() == 0);

			constexpr ::Assets::ArtifactRequest requests[] {
				::Assets::ArtifactRequest { "--ignored--", "artifact-one"_h, 1, ::Assets::ArtifactRequest::DataType::SharedBlob },
				::Assets::ArtifactRequest { "--ignored--", "artifact-two"_h, 5, ::Assets::ArtifactRequest::DataType::Raw }
			};
			auto resolvedRequests = artifactCollection->ResolveRequests(MakeIteratorRange(requests));
			REQUIRE(resolvedRequests.size() == 2);
			REQUIRE(resolvedRequests[0]._sharedBlob);
			REQUIRE(::Assets::AsString(resolvedRequests[0]._sharedBlob) == "artifact-one-contents");
			REQUIRE(resolvedRequests[1]._buffer);
			REQUIRE(resolvedRequests[1]._bufferSize);
		}
	}
}
