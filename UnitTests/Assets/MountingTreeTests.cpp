// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "../../Assets/MountingTree.h"
#include "../../Assets/IFileSystem.h"
#include "../../Assets/NascentChunk.h"
#include "../../Assets/MemoryFile.h"
#include "../../Formatters/TextFormatter.h"
#include "../../Utility/Streams/PathUtils.h"
#include "../../Utility/Conversion.h"
#include <stdexcept>
#include "catch2/catch_test_macros.hpp"
#include "catch2/catch_approx.hpp"

using namespace Catch::literals;
namespace UnitTests
{
	static std::unordered_map<std::string, ::Assets::Blob> s_utData0 {
		std::make_pair(
			"exampleFileOne.file",
			::Assets::AsBlob(R"--(exampleFileOne-contents)--")),
		std::make_pair(
			"exampleFileTwo.file",
			::Assets::AsBlob(R"--(exampleFileTwo-contents)--")),
		std::make_pair(
			"internalFolder/exampleFileThree.file",
			::Assets::AsBlob(R"--(exampleFileThree-contents)--")),
		std::make_pair(
			"internalFolder/one/two/three/exampleFileFive.file",
			::Assets::AsBlob(R"--(exampleFileFive-contents)--"))
	};
	static std::unordered_map<std::string, ::Assets::Blob> s_utData1 {
		std::make_pair(
			"exampleFileFour.file",
			::Assets::AsBlob(R"--(exampleFileFour-contents)--")),
		std::make_pair(
			"one/two/three/exampleFileSix.file",
			::Assets::AsBlob(R"--(exampleFileSix-contents)--"))
	};

	TEST_CASE( "MountingTree-UnderlyingInterface", "[assets]" )
	{
		auto memoryFS0 = ::Assets::CreateFileSystem_Memory(s_utData0);
		auto memoryFS1 = ::Assets::CreateFileSystem_Memory(s_utData1);

		SECTION("Mount&Unmount")
		{
			auto mountingTree = std::make_shared<::Assets::MountingTree>(s_defaultFilenameRules);

			// Ensure we can mount and then unmount a FS in the tree
			auto mountID0 = mountingTree->Mount("ut-data", memoryFS0);
			REQUIRE(mountingTree->GetMountedFileSystem(mountID0) == memoryFS0.get());
			REQUIRE(mountingTree->GetMountPoint(mountID0) == "ut-data/");
			mountingTree->Unmount(mountID0);
			REQUIRE(mountingTree->GetMountedFileSystem(mountID0) == nullptr);
			REQUIRE(mountingTree->GetMountPoint(mountID0).empty());

			// More complicated mounting points & mounting the same FS more than once
			auto mountID1 = mountingTree->Mount("/ut-data", memoryFS0);
			auto mountID2 = mountingTree->Mount("ut-data/subFolder0/", memoryFS0);
			auto mountID3 = mountingTree->Mount("/ut-data\\subFolder1\\subFolder2", memoryFS0);
			auto mountID4 = mountingTree->Mount("", memoryFS0);
			REQUIRE(mountingTree->GetMountedFileSystem(mountID1) == memoryFS0.get());
			REQUIRE(mountingTree->GetMountedFileSystem(mountID2) == memoryFS0.get());
			REQUIRE(mountingTree->GetMountedFileSystem(mountID3) == memoryFS0.get());
			REQUIRE(mountingTree->GetMountedFileSystem(mountID4) == memoryFS0.get());
			REQUIRE(mountingTree->GetMountPoint(mountID1) == "ut-data/");
			REQUIRE(mountingTree->GetMountPoint(mountID2) == "ut-data/subFolder0/");
			REQUIRE(mountingTree->GetMountPoint(mountID3) == "ut-data/subFolder1/subFolder2/");
			REQUIRE(mountingTree->GetMountPoint(mountID4) == "");
			mountingTree->Unmount(mountID1);
			mountingTree->Unmount(mountID2);
			mountingTree->Unmount(mountID3);
			mountingTree->Unmount(mountID4);
		}

		SECTION("Lookup candidate objects")
		{
			auto mountingTree = std::make_shared<::Assets::MountingTree>(s_defaultFilenameRules);
			auto mnt1 = mountingTree->Mount("ut-data", memoryFS0);

			{
				auto lookup = mountingTree->Lookup("ut-data/exampleFileOne.file");
				REQUIRE(lookup.IsGood());
				::Assets::MountingTree::CandidateObject obj;
				auto lookupResult = lookup.TryGetNext(obj);
				REQUIRE(lookupResult == ::Assets::MountingTree::EnumerableLookup::Result::Success);
				REQUIRE(obj._fileSystem == memoryFS0);
				REQUIRE(obj._mountPoint == "ut-data/");
				lookupResult = lookup.TryGetNext(obj);
				REQUIRE(lookupResult == ::Assets::MountingTree::EnumerableLookup::Result::NoCandidates);

				lookup = mountingTree->Lookup("ut-data/missing-file.file");
				REQUIRE(lookup.IsGood());
				REQUIRE(lookup.TryGetNext(obj) == ::Assets::MountingTree::EnumerableLookup::Result::NoCandidates);
			}

			auto mnt2 = mountingTree->Mount("ut-data/internalFolder", memoryFS1);

			{
				// FS name is "internalFolder/exampleFileThree.file", but mount is "ut-data"
				auto lookup = mountingTree->Lookup("ut-data/internalFolder/exampleFileThree.file");
				REQUIRE(lookup.IsGood());
				::Assets::MountingTree::CandidateObject obj;
				REQUIRE(lookup.TryGetNext(obj) == ::Assets::MountingTree::EnumerableLookup::Result::Success);

				// FS name is "exampleFileFour.file", but mount is "ut-data/internalFolder"
				lookup = mountingTree->Lookup("ut-data/internalFolder/exampleFileFour.file");
				REQUIRE(lookup.IsGood());
				REQUIRE(lookup.TryGetNext(obj) == ::Assets::MountingTree::EnumerableLookup::Result::Success);

				// a little deeper down in the filesystem tree
				lookup = mountingTree->Lookup("ut-data/internalFolder/one/two/three/exampleFileFive.file");
				REQUIRE(lookup.IsGood());
				REQUIRE(lookup.TryGetNext(obj) == ::Assets::MountingTree::EnumerableLookup::Result::Success);
				REQUIRE(obj._mountPoint == "ut-data/");

				lookup = mountingTree->Lookup("ut-data/internalFolder/one/two/three/exampleFileSix.file");
				REQUIRE(lookup.IsGood());
				REQUIRE(lookup.TryGetNext(obj) == ::Assets::MountingTree::EnumerableLookup::Result::Success);
				REQUIRE(obj._mountPoint == "ut-data/internalFolder/");
			}

			mountingTree->Unmount(mnt1);

			{
				// lookup should fail after unmount
				auto lookup = mountingTree->Lookup("ut-data/exampleFileOne.file");
				REQUIRE(lookup.IsGood());
				::Assets::MountingTree::CandidateObject obj;
				REQUIRE(lookup.TryGetNext(obj) == ::Assets::MountingTree::EnumerableLookup::Result::NoCandidates);
			}

			mnt1 = mountingTree->Mount("", memoryFS0);

			{
				// mounting at root should also work
				auto lookup = mountingTree->Lookup("exampleFileOne.file");
				REQUIRE(lookup.IsGood());
				::Assets::MountingTree::CandidateObject obj;
				REQUIRE(lookup.TryGetNext(obj) == ::Assets::MountingTree::EnumerableLookup::Result::Success);
			}

			mountingTree->Unmount(mnt1);
			mountingTree->Unmount(mnt2);
		}

		SECTION("Lookup awkward paths")
		{
			auto mountingTree = std::make_shared<::Assets::MountingTree>(s_defaultFilenameRules);
			auto mnt1 = mountingTree->Mount("ut-data", memoryFS0);
			auto mnt2 = mountingTree->Mount("ut-data/internalFolder", memoryFS1);

			::Assets::MountingTree::CandidateObject obj;
			SECTION("utf8") {
				// "./" at the end of the of the mounting path
				auto lookup = mountingTree->Lookup("ut-data/././internalFolder/exampleFileThree.file");
				REQUIRE(lookup.IsGood());
				REQUIRE(lookup.TryGetNext(obj) == ::Assets::MountingTree::EnumerableLookup::Result::Success);
				REQUIRE(obj._mountPoint == "ut-data/");

				// "./" at the very start
				lookup = mountingTree->Lookup("./ut-data/internalFolder/exampleFileThree.file");
				REQUIRE(lookup.IsGood());
				REQUIRE(!lookup.IsFullyQualifiedPath());
				REQUIRE(lookup.TryGetNext(obj) == ::Assets::MountingTree::EnumerableLookup::Result::Success);
				REQUIRE(obj._mountPoint == "ut-data/");

				// "./" in the middle of the mounting name
				lookup = mountingTree->Lookup("ut-data/./internalFolder/exampleFileFour.file");
				REQUIRE(lookup.IsGood());
				REQUIRE(lookup.TryGetNext(obj) == ::Assets::MountingTree::EnumerableLookup::Result::Success);
				REQUIRE(obj._mountPoint == "ut-data/internalFolder/");

				// "../" to drop back to an earlier directory (effectively ignoring the "internalFolder/" part)
				lookup = mountingTree->Lookup("ut-data/internalFolder/../exampleFileOne.file");
				REQUIRE(lookup.IsGood());
				REQUIRE(lookup.TryGetNext(obj) == ::Assets::MountingTree::EnumerableLookup::Result::Success);
				REQUIRE(obj._mountPoint == "ut-data/");

				// "./" or "../" within the mounted filesystem part may or may not work depending on the 
				// filesystem implementation
				lookup = mountingTree->Lookup("ut-data/././internalFolder/one/two/../../one/two/./three/./././exampleFileFive.file");
				REQUIRE(lookup.IsGood());
				REQUIRE(lookup.TryGetNext(obj) == ::Assets::MountingTree::EnumerableLookup::Result::Success);
				REQUIRE(obj._mountPoint == "ut-data/");

				// When the mounting tree "absolute path mode" is set to MountingTree, the absolute
				// root is the root of the mounting tree (not the root of the raw FS)
				lookup = mountingTree->Lookup("/ut-data/internalFolder/exampleFileThree.file");
				REQUIRE(lookup.IsGood());
				REQUIRE(lookup.IsFullyQualifiedPath());
				REQUIRE(lookup.TryGetNext(obj) == ::Assets::MountingTree::EnumerableLookup::Result::Success);
				REQUIRE(obj._mountPoint == "ut-data/");
			}

			SECTION("utf16") {
				// "./" at the end of the of the mounting path
				auto lookup = mountingTree->Lookup(u"ut-data/././internalFolder/exampleFileThree.file");
				REQUIRE(lookup.IsGood());
				REQUIRE(lookup.TryGetNext(obj) == ::Assets::MountingTree::EnumerableLookup::Result::Success);
				REQUIRE(obj._mountPoint == "ut-data/");

				// "./" at the very start
				lookup = mountingTree->Lookup(u"./ut-data/internalFolder/exampleFileThree.file");
				REQUIRE(lookup.IsGood());
				REQUIRE(!lookup.IsFullyQualifiedPath());
				REQUIRE(lookup.TryGetNext(obj) == ::Assets::MountingTree::EnumerableLookup::Result::Success);
				REQUIRE(obj._mountPoint == "ut-data/");

				// "./" in the middle of the mounting name
				lookup = mountingTree->Lookup(u"ut-data/./internalFolder/exampleFileFour.file");
				REQUIRE(lookup.IsGood());
				REQUIRE(lookup.TryGetNext(obj) == ::Assets::MountingTree::EnumerableLookup::Result::Success);
				REQUIRE(obj._mountPoint == "ut-data/internalFolder/");

				// "../" to drop back to an earlier directory (effectively ignoring the "internalFolder/" part)
				lookup = mountingTree->Lookup(u"ut-data/internalFolder/../exampleFileOne.file");
				REQUIRE(lookup.IsGood());
				REQUIRE(lookup.TryGetNext(obj) == ::Assets::MountingTree::EnumerableLookup::Result::Success);
				REQUIRE(obj._mountPoint == "ut-data/");

				// "./" or "../" within the mounted filesystem part may or may not work depending on the 
				// filesystem implementation
				lookup = mountingTree->Lookup(u"ut-data/././internalFolder/one/two/../../one/two/./three/./././exampleFileFive.file");
				REQUIRE(lookup.IsGood());
				REQUIRE(lookup.TryGetNext(obj) == ::Assets::MountingTree::EnumerableLookup::Result::Success);
				REQUIRE(obj._mountPoint == "ut-data/");

				// When the mounting tree "absolute path mode" is set to MountingTree, the absolute
				// root is the root of the mounting tree (not the root of the raw FS)
				lookup = mountingTree->Lookup(u"/ut-data/internalFolder/exampleFileThree.file");
				REQUIRE(lookup.IsGood());
				REQUIRE(lookup.IsFullyQualifiedPath());
				REQUIRE(lookup.TryGetNext(obj) == ::Assets::MountingTree::EnumerableLookup::Result::Success);
				REQUIRE(obj._mountPoint == "ut-data/");
			}

			mountingTree->Unmount(mnt2);
			mountingTree->Unmount(mnt1);
		}
	}

	template<typename CharType>
		uint64_t H(const utf8* input, const FilenameRules& rules)
	{
		auto str = Conversion::Convert<std::basic_string<CharType>>(std::basic_string<utf8>(input));
		return HashFilenameAndPath(MakeStringSection(str), rules);
	}

	template<typename CharType>
		static void HashAwkwardPaths(const FilenameRules& rules)
	{
		// In many cases different input strings will generate the same output hash
		// this is done to try to make sure that different strings that point to the
		// same object will have the same hash (to the extent that we have enough context to know that)
		auto hashCurrentA = H<CharType>("./", rules);
		auto hashCurrentB = H<CharType>("path-i/..", rules);
		auto hashCurrentC = H<CharType>("path-i/../", rules);
		auto hashCurrentD = H<CharType>(".", rules);
		auto hashCurrentE = H<CharType>("././././.", rules);
		auto hashCurrentF = H<CharType>("./././", rules);
		REQUIRE(hashCurrentA == hashCurrentB);
		REQUIRE(hashCurrentA == hashCurrentC);
		REQUIRE(hashCurrentA == hashCurrentD);
		REQUIRE(hashCurrentA == hashCurrentE);
		REQUIRE(hashCurrentA == hashCurrentF);

		auto hashEmpty = H<CharType>("", rules);
		REQUIRE(hashCurrentA != hashEmpty);
		
		auto hashFilesystemRoot = H<CharType>("/", rules);
		REQUIRE(hashCurrentA != hashFilesystemRoot);
		REQUIRE(hashEmpty != hashFilesystemRoot);

		auto hashBackOneA = H<CharType>("../", rules);
		auto hashBackOneB = H<CharType>("../something/..", rules);
		REQUIRE(hashBackOneA != hashBackOneB);
		REQUIRE(hashBackOneA != hashEmpty);
		REQUIRE(hashBackOneA != hashFilesystemRoot);
		REQUIRE(hashBackOneA != hashCurrentA);

		auto hashBackPathComboA = H<CharType>("path-one/path-two/path-three", rules);
		auto hashBackPathComboB = H<CharType>("path-one/path-i/../path-two/path-three/path-four/..", rules);
		auto hashBackPathComboC = H<CharType>("path-one/path-i/path-i2/../../path-two/path-three/path-four/..", rules);
		auto hashBackPathComboD = H<CharType>("path-one/path-two/path-three/", rules);
		auto hashBackPathComboE = H<CharType>("path-one/path-i/../path-two/path-three/path-four/../", rules);
		REQUIRE(hashBackPathComboA == hashBackPathComboB);
		REQUIRE(hashBackPathComboA == hashBackPathComboC);
		REQUIRE(hashBackPathComboA == hashBackPathComboD);
		REQUIRE(hashBackPathComboA == hashBackPathComboE);

		auto hashNegativeDomainA = H<CharType>("../../something", rules);
		auto hashNegativeDomainB = H<CharType>("ignored/ignored/../../alsoignored/../../../something", rules);
		REQUIRE(hashNegativeDomainA == hashNegativeDomainB);
		REQUIRE(hashNegativeDomainA != H<CharType>("something", rules));

		auto hashNegativeDomainC = H<CharType>("../../something/something/returned", rules);
		auto hashNegativeDomainD = H<CharType>("returned", rules);
		REQUIRE(hashNegativeDomainC != hashNegativeDomainD);

		REQUIRE(H<CharType>("something", rules) == H<CharType>("./something", rules));
		REQUIRE(H<CharType>("something", rules) != H<CharType>(".something", rules));

		REQUIRE(H<CharType>("somedir/./somedir2", rules) == H<CharType>("somedir/somedir2", rules));

		auto hashExtraneousEndA = H<CharType>("somedir/", rules);
		auto hashExtraneousEndB = H<CharType>("somedir/.", rules);
		auto hashExtraneousEndC = H<CharType>("somedir/./././", rules);
		auto hashExtraneousEndD = H<CharType>("somedir/./././.", rules);
		auto hashExtraneousEndE = H<CharType>("somedir", rules);
		REQUIRE(hashExtraneousEndA == hashExtraneousEndB);
		REQUIRE(hashExtraneousEndA == hashExtraneousEndC);
		REQUIRE(hashExtraneousEndA == hashExtraneousEndD);
		REQUIRE(hashExtraneousEndA == hashExtraneousEndE);

		REQUIRE(H<CharType>("one/two", rules) == H<CharType>("one//\\/two", rules));
		REQUIRE(H<CharType>("../../negative-paths", rules) != H<CharType>("/../../negative-paths", rules));
		REQUIRE(H<CharType>("../../something/something/returned", rules) != H<CharType>("returned", rules));
		REQUIRE(H<CharType>("/filename", rules) != H<CharType>("filename", rules));
		REQUIRE(H<CharType>("/../../filename", rules) != H<CharType>("../../filename", rules));
	}

	TEST_CASE( "HashFilenameAndPath", "[assets]" )
	{
		// todo -- check 64 deep lookup

		FilenameRules rules0('/', true);
		FilenameRules rules1('/', false);

		HashAwkwardPaths<utf8>(rules0);
		HashAwkwardPaths<utf16>(rules0);
		HashAwkwardPaths<utf8>(rules1);
		HashAwkwardPaths<utf16>(rules1);

		// Hashes should be the same regardless of whether the input is utf8 or utf16
		REQUIRE(H<utf8>("ignored/ignored/../../alsoignored/../../../something", rules0) == H<utf16>("ignored/ignored/../../alsoignored/../../../something", rules0));
		REQUIRE(H<utf8>("ignored/ignored/../../alsoignored/../../../something", rules1) == H<utf16>("ignored/ignored/../../alsoignored/../../../something", rules1));

		REQUIRE(H<utf8>("internalFolder/exampleFileThree.file", rules0) == H<utf16>("internalFolder/exampleFileThree.file", rules0));
	}
}
