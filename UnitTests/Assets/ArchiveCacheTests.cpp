// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "../UnitTestHelper.h"
#include "../../Assets/ArchiveCache.h"
#include "../../Assets/AssetUtils.h"
#include "../../Assets/IArtifact.h"
#include "../../Assets/ICompileOperation.h"
#include "../../Assets/DepVal.h"
#include "../../ConsoleRig/AttachablePtr.h"
#include "../../ConsoleRig/GlobalServices.h"
#include <stdexcept>
#include <filesystem>
#include "catch2/catch_test_macros.hpp"
#include "catch2/catch_approx.hpp"


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
			::Assets::ArchiveCacheSet cacheSet(dummyVersionDesc);
			auto archive = cacheSet.GetArchive(archiveFileName);

			uint64_t objectOneId = Hash64("ObjectOne");
			archive->Commit(
				objectOneId, "Object",
				MakeIteratorRange(s_artifactsObj1),
				MakeIteratorRange(s_depFileStatesObj1));

			auto artifactCollection = archive->TryOpenFromCache(objectOneId);
			REQUIRE(artifactCollection);
			auto depVal = artifactCollection->GetDependencyValidation();
			REQUIRE(depVal);
			REQUIRE(depVal->GetValidationIndex() == 0);
			
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
				MakeIteratorRange(s_depFileStatesObj2));

			artifactCollection = archive->TryOpenFromCache(objectTwoId);
			REQUIRE(artifactCollection);

			cacheSet.FlushToDisk();

			archive->Commit(
				objectTwoId, "ObjectTwo",
				MakeIteratorRange(s_artifactsObj2Replacement),
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
				{});
		}

		{
			// When we close and reopen the cache set, we should still be able to get out the same results
			::Assets::ArchiveCacheSet cacheSet(dummyVersionDesc);
			auto archive = cacheSet.GetArchive(archiveFileName);

			auto artifactCollection = archive->TryOpenFromCache(Hash64("ObjectOne"));
			REQUIRE(artifactCollection);
			auto depVal = artifactCollection->GetDependencyValidation();
			REQUIRE(depVal);
			REQUIRE(depVal->GetValidationIndex() == 0);

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
}
