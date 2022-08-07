// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "../UnitTestHelper.h"
#include "../../Assets/CompileAndAsyncManager.h"
#include "../../Assets/ICompileOperation.h"
#include "../../Assets/IArtifact.h"
#include "../../Assets/IntermediateCompilers.h"
#include "../../Assets/IntermediatesStore.h"
#include "../../Assets/MemoryFile.h"
#include "../../Assets/ChunkFile.h"
#include "../../Assets/AssetTraits.h"
#include "../../Assets/InitializerPack.h"
#include "../../Assets/DeferredConstruction.h"
#include "../../Assets/AssetServices.h"
#include "../../ConsoleRig/GlobalServices.h"
#include "../../ConsoleRig/AttachablePtr.h"
#include "../../OSServices/AttachableLibrary.h"
#include "../../Utility/Streams/PathUtils.h"
#include "../../Utility/MemoryUtils.h"
#include "../../Utility/StringUtils.h"
#include <stdexcept>
#include <filesystem>
#include "thousandeyes/futures/DefaultExecutor.h"
#include "catch2/catch_test_macros.hpp"
#include "catch2/catch_approx.hpp"

#include "../../Assets/IFileSystem.h"
#include "../../Assets/MountingTree.h"


using namespace Catch::literals;
namespace UnitTests
{
	static constexpr uint64_t Type_UnitTestArtifact = ConstHash64<'unit', 'test', 'arti', 'fact'>::Value;
	static constexpr uint64_t Type_UnitTestExtraArtifact = ConstHash64<'unit', 'test', 'extr', 'a'>::Value;

	class TestCompileOperation : public ::Assets::ICompileOperation
	{
	public:
		std::string _initializer;
		virtual std::vector<TargetDesc>			GetTargets() const override
		{
			return {
				TargetDesc { Type_UnitTestArtifact, "unitary-artifact" }
			};
		}

		static unsigned s_serializeTargetCount;
		static unsigned s_constructionCount;

		static std::mutex s_pauseCompilationLock;

		virtual std::vector<SerializedArtifact>	SerializeTarget(unsigned idx) override
		{
			ScopedLock(s_pauseCompilationLock);

			assert(idx == 0);
			++s_serializeTargetCount;

			if (_initializer == "unit-test-asset-throw-from-serialize-target")
				Throw(std::runtime_error("Throw from serialize target requested"));

			// Blobs written here will become chunks in the output file
			std::vector<SerializedArtifact> result;
			{
				SerializedArtifact opRes;
				opRes._chunkTypeCode = Type_UnitTestArtifact;
				opRes._version = 1;
				opRes._name = "unitary-artifact";
				opRes._data = ::Assets::AsBlob("This is file data from TestCompileOperation for " + _initializer);
				result.emplace_back(std::move(opRes));
			}
			{
				SerializedArtifact opRes;
				opRes._chunkTypeCode = Type_UnitTestExtraArtifact;
				opRes._version = 1;
				opRes._name = "unitary-artifact-extra";
				opRes._data = ::Assets::AsBlob("This is extra file data");
				result.emplace_back(std::move(opRes));
			}

			// we can optionally write a "metrics" chunk. This doesn't actually get returned to the
			// client that initiated the compile. It's just gets written to the intermediate assets
			// store and is mostly used for debugging purposes
			{
				SerializedArtifact opRes;
				opRes._chunkTypeCode = ConstHash64<'Metr', 'ics'>::Value;
				opRes._version = 1;
				opRes._name = "unitary-artifact-metrics";
				opRes._data = ::Assets::AsBlob("This is file data from TestCompileOperation for " + _initializer);
				result.emplace_back(std::move(opRes));
			}
			return result;
		}

		virtual std::vector<::Assets::DependentFileState> GetDependencies() const override
		{
			// We can declare a non-existant file as one of our dependencies. This is like saying that 
			// the compile would be invalidated if this file appeared at a later time
			return {
				::Assets::DependentFileState{MakeStringSection("fake-file-state"), 0, ::Assets::DependentFileState::Status::DoesNotExist}
			};
		}

		TestCompileOperation(::Assets::InitializerPack& initializer) : _initializer(initializer.GetInitializer<std::string>(0)) 
		{
			++s_constructionCount;
			if (_initializer == "unit-test-asset-throw-from-constructor")
				Throw(std::runtime_error("Throw from constructor requested"));
		}
	};

	std::mutex TestCompileOperation::s_pauseCompilationLock;

	unsigned TestCompileOperation::s_serializeTargetCount = 0;
	unsigned TestCompileOperation::s_constructionCount = 0;

	class TestChunkRequestsAsset
	{
	public:
		static const ::Assets::ArtifactRequest ChunkRequests[2];

		std::string _data0, _data1;
		TestChunkRequestsAsset(
			IteratorRange<::Assets::ArtifactRequestResult*> chunks, 
			const ::Assets::DependencyValidation& depVal)
		{
			_data0 = ::Assets::AsString(chunks[0]._sharedBlob);
			_data1 = ::Assets::AsString(chunks[1]._sharedBlob);
		}
	};

	const ::Assets::ArtifactRequest TestChunkRequestsAsset::ChunkRequests[]
    {
        ::Assets::ArtifactRequest { "unitary-artifact", Type_UnitTestArtifact, 1, ::Assets::ArtifactRequest::DataType::SharedBlob },
		::Assets::ArtifactRequest { "unitary-artifact-extra", Type_UnitTestExtraArtifact, 1, ::Assets::ArtifactRequest::DataType::SharedBlob },
    };

	static const char* GetConfigString()
	{
		#if defined(_DEBUG)
			#if TARGET_64BIT
				return "d64";
			#else
				return "d";
			#endif
		#else
			#if TARGET_64BIT
				return "r64";
			#else
				return "r";
			#endif
		#endif
	}

	static std::shared_ptr<::Assets::IArtifactCollection> GetExistingArtifact(::Assets::IIntermediateCompileMarker& marker, uint64_t typeCode)
	{
		auto artifactQuery = marker.GetArtifact(typeCode);
		if (artifactQuery.first)
			return artifactQuery.first;
		REQUIRE(artifactQuery.second.StallWhilePending(std::chrono::microseconds(1)).value() == ::Assets::AssetState::Ready);
		return artifactQuery.second.GetArtifactCollectionPtr();
	}

	TEST_CASE( "AssetCompilers-BasicCompilers", "[assets]" )
	{
		//
		// IntermediateCompilers provides a mechanism for running pre-processing operations on
		// data files in order to prepare them for the final format 
		// 
		UnitTest_SetWorkingDirectory();
		auto globalServices = ConsoleRig::MakeAttachablePtr<ConsoleRig::GlobalServices>(GetStartupConfig());
		
		auto compilers = ::Assets::CreateIntermediateCompilers(nullptr);

		SECTION("Register/Deregister")
		{
			uint64_t outputTypes[] = { Type_UnitTestArtifact };
			auto registration = compilers->RegisterCompiler(
				"UnitTestCompiler",
				"UnitTestCompiler",
				ConsoleRig::GetLibVersionDesc(),
				{},
				[](auto initializers) {
					assert(!initializers.IsEmpty());
					return std::make_shared<TestCompileOperation>(initializers);
				});

			compilers->AssociateRequest(
				registration,
				MakeIteratorRange(outputTypes),
				"unit-test-asset-.*");

			auto marker = compilers->Prepare(Type_UnitTestArtifact, ::Assets::InitializerPack { "unit-test-asset-one" });
			REQUIRE(marker != nullptr);
			auto artifactQuery = marker->GetArtifact(Type_UnitTestArtifact);
			REQUIRE(artifactQuery.first == nullptr);

			auto compile = artifactQuery.second;
			REQUIRE(compile.Valid());

			compile.StallWhilePending();
			REQUIRE(compile.GetAssetState() == ::Assets::AssetState::Ready);

			SECTION("SuccessfulResolveRequests")
			{
				//
				// Resolve artifacts via an explicit call to ResolveRequests
				//
				::Assets::ArtifactRequest requests[] {
					::Assets::ArtifactRequest {
						"unitary-artifact", Type_UnitTestArtifact, 1,
						::Assets::ArtifactRequest::DataType::SharedBlob
					},
					::Assets::ArtifactRequest {
						"unitary-artifact-extra", Type_UnitTestExtraArtifact, 1,
						::Assets::ArtifactRequest::DataType::SharedBlob
					}
				};
				auto artifacts = compile.GetArtifactCollection().ResolveRequests(MakeIteratorRange(requests));
				REQUIRE(artifacts.size() == 2);
				REQUIRE(::Assets::AsString(artifacts[0]._sharedBlob) == "This is file data from TestCompileOperation for unit-test-asset-one");
				REQUIRE(::Assets::AsString(artifacts[1]._sharedBlob) == "This is extra file data");

				//
				// Resolve artifacts implicitly via calling ::Assets::AutoConstructAsset
				// The ChunkRequests array within TestChunkRequestsAsset is used to bind input artifacts
				//
				auto implicitlyConstructed = ::Assets::AutoConstructAsset<std::unique_ptr<TestChunkRequestsAsset>>(compile.GetArtifactCollection());
				REQUIRE(implicitlyConstructed->_data0 == "This is file data from TestCompileOperation for unit-test-asset-one");
				REQUIRE(implicitlyConstructed->_data1 == "This is extra file data");
			}

			SECTION("FailedResolveRequests")
			{
				REQUIRE_THROWS([&]() {
					// Fails because the version number requested doesn't match what's provided
					// (this is case requested version number is higher)
					::Assets::ArtifactRequest requests[] {
						::Assets::ArtifactRequest {
							"unitary-artifact", Type_UnitTestArtifact, 2,
							::Assets::ArtifactRequest::DataType::SharedBlob
						}
					};
					compile.GetArtifactCollection().ResolveRequests(MakeIteratorRange(requests));
				}());
				REQUIRE_THROWS([&]() {
					// Fails because the type code requested doesn't match (note name ignored)
					::Assets::ArtifactRequest requests[] {
						::Assets::ArtifactRequest {
							"unitary-artifact", Type_UnitTestArtifact + 5, 1,
							::Assets::ArtifactRequest::DataType::SharedBlob
						}
					};
					compile.GetArtifactCollection().ResolveRequests(MakeIteratorRange(requests));
				}());
				REQUIRE_THROWS([&]() {
					// Fails because the same type code is repeated multiple times in the request
					::Assets::ArtifactRequest requests[] {
						::Assets::ArtifactRequest {
							"unitary-artifact", Type_UnitTestArtifact, 1,
							::Assets::ArtifactRequest::DataType::SharedBlob
						},
						::Assets::ArtifactRequest {
							"unitary-artifact-two", Type_UnitTestArtifact, 1,
							::Assets::ArtifactRequest::DataType::SharedBlob
						}
					};
					compile.GetArtifactCollection().ResolveRequests(MakeIteratorRange(requests));
				}());
			}

			compilers->DeregisterCompiler(registration);
		}

		SECTION("Compiler marker management")
		{
			uint64_t outputTypes[] = { Type_UnitTestArtifact };
			auto registration = compilers->RegisterCompiler(
				"UnitTestCompiler",
				"UnitTestCompiler",
				ConsoleRig::GetLibVersionDesc(),
				{},
				[](auto initializers) {
					assert(!initializers.IsEmpty() == 1);
					return std::make_shared<TestCompileOperation>(initializers);
				});

			compilers->AssociateRequest(
				registration,
				MakeIteratorRange(outputTypes),
				"unit-test-asset-.*");

			auto initializer0 = "unit-test-asset-one";
			auto initializer1 = "unit-test-asset-two";
			auto marker0 = compilers->Prepare(Type_UnitTestArtifact, ::Assets::InitializerPack { initializer0 });
			auto marker1 = compilers->Prepare(Type_UnitTestArtifact, ::Assets::InitializerPack { initializer0 });
			auto marker2 = compilers->Prepare(Type_UnitTestArtifact, ::Assets::InitializerPack { initializer1 });
			REQUIRE(marker0 == marker1);
			REQUIRE(marker0 != marker2);

			compilers->DeregisterCompiler(registration);
		}
	}

	static ::Assets::CompilerRegistration RegisterUnitTestCompiler(::Assets::IIntermediateCompilers& compilers)
	{
		uint64_t outputTypes[] = { Type_UnitTestArtifact };
		::Assets::CompilerRegistration registration{
			compilers,
			"UnitTestCompiler",
			"UnitTestCompiler",
			ConsoleRig::GetLibVersionDesc(),
			{},
			[](auto initializers) {
				assert(!initializers.IsEmpty());
				return std::make_shared<TestCompileOperation>(initializers);
			}};

		compilers.AssociateRequest(
			registration.RegistrationId(),
			MakeIteratorRange(outputTypes),
			"unit-test-asset-.*");
		return registration;
	}

	TEST_CASE( "AssetCompilers-IntermediatesStore", "[assets]" )
	{
		// note -- consider decoupling Assets::IntermediatesStore from the main file system, so
		// we don't have to boot this here
		/*auto mountingTree = std::make_shared<::Assets::MountingTree>(s_defaultFilenameRules);
		// _utDataMount = _mountingTree->Mount("ut-data", ::Assets::CreateFileSystem_Memory(s_utData));
		::Assets::MainFileSystem::Init(mountingTree, nullptr);*/

		UnitTest_SetWorkingDirectory();
		auto globalServices = ConsoleRig::MakeAttachablePtr<ConsoleRig::GlobalServices>(GetStartupConfig());
		::Assets::MainFileSystem::GetMountingTree()->SetAbsolutePathMode(Assets::MountingTree::AbsolutePathMode::RawOS);

		auto tempDirPath = std::filesystem::temp_directory_path() / "xle-unit-tests";
		std::filesystem::remove_all(tempDirPath);	// ensure we're starting from an empty temporary directory
		std::filesystem::create_directories(tempDirPath);

		auto intermediateStore = std::make_shared<::Assets::IntermediatesStore>(
			::Assets::MainFileSystem::GetDefaultFileSystem(),
			tempDirPath.string().c_str(),
			ConsoleRig::GetLibVersionDesc()._versionString,
			GetConfigString());
		auto compilers = ::Assets::CreateIntermediateCompilers(intermediateStore);
		auto registration = RegisterUnitTestCompiler(*compilers);

		::Assets::ArtifactRequest requests[] {
			::Assets::ArtifactRequest {
				"unitary-artifact", Type_UnitTestArtifact, 1,
				::Assets::ArtifactRequest::DataType::SharedBlob
			},
			::Assets::ArtifactRequest {
				"unitary-artifact-extra", Type_UnitTestExtraArtifact, 1,
				::Assets::ArtifactRequest::DataType::SharedBlob
			}
		};

		SECTION("Cache compile result")
		{
			unsigned initialSerializeTargetCount = TestCompileOperation::s_serializeTargetCount;
				
			auto initializer = "unit-test-asset-one";
			auto marker = compilers->Prepare(Type_UnitTestArtifact, ::Assets::InitializerPack { initializer } );
			REQUIRE(marker != nullptr);
			auto artifactQuery = marker->GetArtifact(Type_UnitTestArtifact);
			REQUIRE(artifactQuery.first == nullptr);

			auto compile = artifactQuery.second;
			REQUIRE(compile.Valid());

			compile.StallWhilePending();
			REQUIRE(compile.GetAssetState() == ::Assets::AssetState::Ready);
			
			auto artifacts = compile.GetArtifactCollection().ResolveRequests(MakeIteratorRange(requests));
			REQUIRE(artifacts.size() == 2);
			REQUIRE(::Assets::AsString(artifacts[0]._sharedBlob) == "This is file data from TestCompileOperation for unit-test-asset-one");
			REQUIRE(::Assets::AsString(artifacts[1]._sharedBlob) == "This is extra file data");
			REQUIRE(TestCompileOperation::s_serializeTargetCount == initialSerializeTargetCount+1);

			// Now GetArtifact() on the same marker should give us something immediately
			auto existingAsset = GetExistingArtifact(*marker, Type_UnitTestArtifact);
			REQUIRE(existingAsset != nullptr);
			REQUIRE(existingAsset->GetDependencyValidation().GetValidationIndex() == 0);	// still clean
			artifacts = existingAsset->ResolveRequests(MakeIteratorRange(requests));
			REQUIRE(::Assets::AsString(artifacts[0]._sharedBlob) == "This is file data from TestCompileOperation for unit-test-asset-one");
			REQUIRE(::Assets::AsString(artifacts[1]._sharedBlob) == "This is extra file data");
			REQUIRE(TestCompileOperation::s_serializeTargetCount == initialSerializeTargetCount+1);

			// We can also go all the way back to the Prepare() function and expect an existing asset this time
			compile = {};
			marker = nullptr;
			marker = compilers->Prepare(Type_UnitTestArtifact, ::Assets::InitializerPack { initializer } );
			REQUIRE(marker != nullptr);
			existingAsset = GetExistingArtifact(*marker, Type_UnitTestArtifact);
			REQUIRE(existingAsset != nullptr);
			REQUIRE(existingAsset->GetDependencyValidation().GetValidationIndex() == 0);	// still clean
			artifacts = existingAsset->ResolveRequests(MakeIteratorRange(requests));
			REQUIRE(::Assets::AsString(artifacts[0]._sharedBlob) == "This is file data from TestCompileOperation for unit-test-asset-one");
			REQUIRE(::Assets::AsString(artifacts[1]._sharedBlob) == "This is extra file data");
			REQUIRE(TestCompileOperation::s_serializeTargetCount == initialSerializeTargetCount+1);
		}
	}

	TEST_CASE( "AssetCompilers-HandlingCompilationFailures", "[assets]" )
	{
		UnitTest_SetWorkingDirectory();
		auto globalServices = ConsoleRig::MakeAttachablePtr<ConsoleRig::GlobalServices>(GetStartupConfig());
		::Assets::MainFileSystem::GetMountingTree()->SetAbsolutePathMode(Assets::MountingTree::AbsolutePathMode::RawOS);

		auto tempDirPath = std::filesystem::temp_directory_path() / "xle-unit-tests";
		std::filesystem::remove_all(tempDirPath);	// ensure we're starting from an empty temporary directory
		std::filesystem::create_directories(tempDirPath);

		auto intermediateStore = std::make_shared<::Assets::IntermediatesStore>(
			::Assets::MainFileSystem::GetDefaultFileSystem(),
			tempDirPath.string().c_str(),
			ConsoleRig::GetLibVersionDesc()._versionString,
			GetConfigString());
		auto compilers = ::Assets::CreateIntermediateCompilers(intermediateStore);
		auto registration = RegisterUnitTestCompiler(*compilers);

		SECTION("Exceptions from ICompileOperation")
		{
			{
				auto marker = compilers->Prepare(Type_UnitTestArtifact, ::Assets::InitializerPack { "unit-test-asset-throw-from-constructor" } );
				REQUIRE(marker != nullptr);
				auto artifactQuery = marker->GetArtifact(Type_UnitTestArtifact);
				REQUIRE(artifactQuery.first == nullptr);

				// If the ICompileOperation throws from a constructor, then GetArtifactCollection
				// will throw when we try to access it. This is because the compiler infrastructure
				// gets no information about the compile targets, etc, when the ICompileOperation
				// constructor can not be completed
				auto compile = artifactQuery.second;
				REQUIRE(compile.Valid());
				compile.StallWhilePending();
				REQUIRE(compile.GetAssetState() == ::Assets::AssetState::Invalid);
				REQUIRE_THROWS(compile.GetArtifactCollection());
			}

			{
				auto marker = compilers->Prepare(Type_UnitTestArtifact, ::Assets::InitializerPack { "unit-test-asset-throw-from-serialize-target" } );
				REQUIRE(marker != nullptr);
				auto artifactQuery = marker->GetArtifact(Type_UnitTestArtifact);
				REQUIRE(artifactQuery.first == nullptr);

				// Note that the future gets set to "ready" state, but the ArtifactCollection gets "invalid" state in this case. This is because
				// the future can contain multiple ArtifactCollection, which can each have separate states
				auto compile = artifactQuery.second;
				REQUIRE(compile.Valid());
				compile.StallWhilePending();
				REQUIRE(compile.GetAssetState() == ::Assets::AssetState::Ready);
				REQUIRE(compile.GetArtifactCollection().GetAssetState() == ::Assets::AssetState::Invalid);
				auto log = ::Assets::AsString(::Assets::GetErrorMessage(compile.GetArtifactCollection()));
				REQUIRE(XlFindString(MakeStringSection(log), "Throw from serialize target requested"));
				REQUIRE(compile.GetArtifactCollection().GetDependencyValidation());
			}
		}

		SECTION("Retrieve exception information from intermediates store")
		{
			// Trigger a failed compilation as before, but this time do
			// it twice, to ensure that the second time also gets the same result
			auto initialCompileCount = TestCompileOperation::s_constructionCount;
			for (unsigned c=0; c<2; ++c) {
				auto marker = compilers->Prepare(Type_UnitTestArtifact, ::Assets::InitializerPack { "unit-test-asset-throw-from-serialize-target" } );
				auto artifactQuery = marker->GetArtifact(Type_UnitTestArtifact);
				if (artifactQuery.first) {
					REQUIRE(artifactQuery.first->GetAssetState() == ::Assets::AssetState::Invalid);
					auto log = ::Assets::AsString(::Assets::GetErrorMessage(*artifactQuery.first));
					REQUIRE(XlFindString(MakeStringSection(log), "Throw from serialize target requested"));
					continue;	
				}
				auto compile = artifactQuery.second;
				compile.StallWhilePending();
				REQUIRE(compile.GetAssetState() == ::Assets::AssetState::Ready);
				REQUIRE(compile.GetArtifactCollection().GetAssetState() == ::Assets::AssetState::Invalid);
				auto log = ::Assets::AsString(::Assets::GetErrorMessage(compile.GetArtifactCollection()));
				REQUIRE(XlFindString(MakeStringSection(log), "Throw from serialize target requested"));
			}
			REQUIRE(TestCompileOperation::s_constructionCount == initialCompileCount+1);

			// reboot the compilers system entirely to ensure all internal caches are destroyed
			intermediateStore->FlushToDisk();
			registration = {};
			compilers.reset();
			compilers = ::Assets::CreateIntermediateCompilers(intermediateStore);
			registration = RegisterUnitTestCompiler(*compilers);

			auto marker = compilers->Prepare(Type_UnitTestArtifact, ::Assets::InitializerPack { "unit-test-asset-throw-from-serialize-target" } );
			auto existing = GetExistingArtifact(*marker, Type_UnitTestArtifact);
			REQUIRE(existing);
			REQUIRE(existing->GetDependencyValidation().GetValidationIndex() == 0);
			REQUIRE(existing->GetAssetState() == ::Assets::AssetState::Invalid);
			auto log = ::Assets::AsString(::Assets::GetErrorMessage(*existing));
			REQUIRE(XlFindString(MakeStringSection(log), "Throw from serialize target requested"));

			// still expecting no new compiles started. We should be getting this error log from the intermediates store
			REQUIRE(TestCompileOperation::s_constructionCount == initialCompileCount+1);
		}
	}

	class TestChunkRequestsAssetWithCompileProcessType : public TestChunkRequestsAsset
	{
	public:
		static const uint64_t CompileProcessType = Type_UnitTestArtifact;
		using TestChunkRequestsAsset::TestChunkRequestsAsset;
	};

	TEST_CASE( "AssetCompilers-ImplicitCompilation", "[assets]" )
	{
		UnitTest_SetWorkingDirectory();
		auto globalServices = ConsoleRig::MakeAttachablePtr<ConsoleRig::GlobalServices>(GetStartupConfig());

		auto& compilers = ::Assets::Services::GetAsyncMan().GetIntermediateCompilers();
		uint64_t outputTypes[] = { Type_UnitTestArtifact };
		auto registration = compilers.RegisterCompiler(
			"UnitTestCompiler",
			"UnitTestCompiler",
			ConsoleRig::GetLibVersionDesc(),
			{},
			[](auto initializers) {
				assert(!initializers.IsEmpty());
				return std::make_shared<TestCompileOperation>(initializers);
			});

		compilers.AssociateRequest(
			registration,
			MakeIteratorRange(outputTypes),
			"unit-test-asset-.*");

		// start a compile via ConstructToMarkerPtr, and ensure that it gets propagated to the registered compiler and generates
		// the asset as expected
		{
			auto futureByPtr = ::Assets::ConstructToMarkerPtr<TestChunkRequestsAssetWithCompileProcessType>("unit-test-asset-implicit");
			futureByPtr->StallWhilePending();
			auto actualPtr = futureByPtr->Actualize();
			REQUIRE(actualPtr != nullptr);
		}
	}

	TEST_CASE( "AssetCompilers-OperationContextTracking", "[assets]" )
	{
		UnitTest_SetWorkingDirectory();
		auto cfg = GetStartupConfig();
		cfg._inMemoryOnlyIntermediates = true;
		auto globalServices = ConsoleRig::MakeAttachablePtr<ConsoleRig::GlobalServices>(cfg);

		auto& compilers = ::Assets::Services::GetAsyncMan().GetIntermediateCompilers();
		uint64_t outputTypes[] = { Type_UnitTestArtifact };
		auto registration = compilers.RegisterCompiler(
			"UnitTestCompiler",
			"UnitTestCompiler",
			ConsoleRig::GetLibVersionDesc(),
			{},
			[](auto initializers) {
				assert(!initializers.IsEmpty());
				return std::make_shared<TestCompileOperation>(initializers);
			});

		compilers.AssociateRequest(
			registration,
			MakeIteratorRange(outputTypes),
			"unit-test-asset-.*");

		auto opContext = std::make_shared<::Assets::OperationContext>();

		{
			std::unique_lock<std::mutex> pauseLock { TestCompileOperation::s_pauseCompilationLock };

			// begin a compile (it don't complete yet because of the pause lock)
			auto futureByPtr = ::Assets::ConstructToMarkerPtr<TestChunkRequestsAssetWithCompileProcessType>(opContext, "unit-test-asset-op-context");
			
			// now that we have begun the compile, we should get an active operation in the operation context shortly. We don't know exactly
			// when, because it's queued a background pool. But the compilation itself is paused, so it should appear eventually
			while (opContext->GetActiveOperations().empty()) {}

			pauseLock = {};
			futureByPtr->StallWhilePending();
			auto actualPtr = futureByPtr->Actualize();
			REQUIRE(actualPtr != nullptr);

			REQUIRE(opContext->GetActiveOperations().empty());		// op context should be empty again now the compile is done
		}
	}

	struct TypeWithComplexMembers
	{
	public:
		std::vector<uint64_t> _integers;
		std::unordered_map<std::string, std::string> _stringMap;

		uint64_t GetHash() const
		{
			uint64_t result = Hash64(_integers.data(), PtrAdd(_integers.data(), _integers.size() * sizeof(uint64_t)));
			for (const auto&p:_stringMap) {
				result = Hash64(p.first, result);
				result = Hash64(p.second, result);
			}
			return result;
		}
	};

	TEST_CASE( "AssetCompilers-InitializerPack", "[assets]" )
	{
		TypeWithComplexMembers complexInitializer;
		complexInitializer._integers = { 45, 75, 23 };
		complexInitializer._stringMap = { std::make_pair("key", "value") };
		auto initializerPack = ::Assets::InitializerPack{
			std::string{"SomeName"},
			"String0",
			MakeStringSection("String1"),
			34,
			complexInitializer};

		REQUIRE(initializerPack.GetInitializer<std::string>(0) == "SomeName");
		REQUIRE(initializerPack.GetInitializer<std::string>(1) == "String0");
		REQUIRE(initializerPack.GetInitializer<std::string>(2) == "String1");
		// unfortunately it's extremely intolerant of integer types
		// There's no casting; you have to request exactly the type that was provided
		REQUIRE(initializerPack.GetInitializer<int>(3) == 34);	
		REQUIRE(&initializerPack.GetInitializer<TypeWithComplexMembers>(4) != &complexInitializer);	// we get a copy, not a reference
		const auto& storedComplexType = initializerPack.GetInitializer<TypeWithComplexMembers>(4);
		REQUIRE(storedComplexType._integers == complexInitializer._integers);
		REQUIRE(storedComplexType._stringMap == complexInitializer._stringMap);

		auto name = initializerPack.ArchivableName();
		REQUIRE(name == "SomeName-String0-String1-34-" + std::to_string(complexInitializer.GetHash()));
		auto hash = initializerPack.ArchivableHash();
		REQUIRE(hash == 10240750523902726346ull);
	}
}

