// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "../UnitTestHelper.h"
#include "../../Tools/EntityInterface/EntityInterface.h"
#include "../../../Assets/IFileSystem.h"
#include "../../../Assets/OSFileSystem.h"
#include "../../../Assets/MountingTree.h"
#include "../../../Assets/MemoryFile.h"
#include "../../../Assets/ConfigFileContainer.h"
#include "../../../Assets/AssetFutureContinuation.h"
#include "../../../Assets/Assets.h"
#include "../../../ConsoleRig/GlobalServices.h"
#include <string>
#include <sstream>
#include "catch2/catch_test_macros.hpp"
#include "catch2/catch_approx.hpp"

namespace EntityInterface
{
	template<typename Formatter> 
		class TextFormatterAdapter : public IDynamicFormatter
	{
	public:
		virtual FormatterBlob PeekNext() { return _fmttr.PeekNext(); }

		virtual bool TryBeginElement() { return _fmttr.TryBeginElement(); }
		virtual bool TryEndElement() { return _fmttr.TryEndElement(); }
		virtual bool TryKeyedItem(StringSection<>& name) { return _fmttr.TryKeyedItem(name); }
		virtual bool TryValue(StringSection<>& value) { return _fmttr.TryValue(value); }
		virtual bool TryCharacterData(StringSection<>& cdata) { return _fmttr.TryCharacterData(cdata); }

		virtual StreamLocation GetLocation() const { return _fmttr.GetLocation(); }
		virtual ::Assets::DependencyValidation GetDependencyValidation() const { return _cfgFile->GetDependencyValidation(); }

		TextFormatterAdapter(
			std::shared_ptr<::Assets::ConfigFileContainer<Formatter>> cfgFile,
			StringSection<typename Formatter::value_type> internalSection)
		: _cfgFile(cfgFile)
		{
			if (!internalSection.IsEmpty()) {
				typename Formatter::value_type internalSectionCopy[internalSection.size()];
				auto* c2 = &internalSectionCopy[0];
				for (auto c:internalSection) *c2++ = (c=='/')?':':c;
				_fmttr = _cfgFile->GetFormatter(MakeStringSection(internalSectionCopy, internalSectionCopy+internalSection.size()));
			} else {
				_fmttr = _cfgFile->GetRootFormatter();
			}
		}
	private:
		std::shared_ptr<::Assets::ConfigFileContainer<Formatter>> _cfgFile;
		Formatter _fmttr;
	};

	class TextEntityDocument : public IEntityDocument
	{
	public:
		virtual ::Assets::PtrToFuturePtr<IDynamicFormatter> BeginFormatter(StringSection<> internalPoint)
		{
			using UnderlyingFormatter = InputStreamFormatter<>;
			auto result = std::make_shared<::Assets::FuturePtr<TextFormatterAdapter<UnderlyingFormatter>>>();
			::Assets::WhenAll(_srcFile).ThenConstructToFuture(
				*result,
				[ip=internalPoint.AsString()](auto cfgFileContainer) {
					return std::make_shared<TextFormatterAdapter<UnderlyingFormatter>>(std::move(cfgFileContainer), ip);
				});
			return std::reinterpret_pointer_cast<::Assets::FuturePtr<IDynamicFormatter>>(result);
		}

		virtual ::Assets::DependencyValidation GetDependencyValidation() const
		{
			return _srcFile->GetDependencyValidation();
		}

		virtual void Lock()
		{
			_lock.lock();
		}
		virtual void Unlock()
		{
			_lock.unlock();
		}

		TextEntityDocument(std::string src) : _src(src)
		{
			_srcFile = ::Assets::MakeAsset<::Assets::ConfigFileContainer<>>(_src);
		}

	private:
		Threading::Mutex _lock;
		std::string _src;
		::Assets::PtrToFuturePtr<::Assets::ConfigFileContainer<>> _srcFile;
	};

	std::shared_ptr<IEntityDocument> CreateTextEntityDocument(StringSection<> src)
	{
		return std::make_shared<TextEntityDocument>(src.AsString());
	}
}

using namespace Catch::literals;
namespace UnitTests
{
	static std::unordered_map<std::string, ::Assets::Blob> s_utData {
		std::make_pair(
			"examplecfg1.dat",
			::Assets::AsBlob(R"--(
				SomeProperty=1
				ASequence=~
					1; 2; 3; 4
				=~
					value=one
					value2=two
				InternalPoint=~
					A=B; C=D
					SomethingInside=~
						E=F
		)--")),
		std::make_pair(
			"examplecfg2.dat",
			::Assets::AsBlob(R"--(
				ASequence=~
					6; 3; 5; 6
				=~
					value2=five
				SomeProperty=5
		)--"))
	};

	template<typename Type, typename CharType>
		Type RequireValue(InputStreamFormatter<CharType>& formatter)
	{
		StringSection<> stringValue;
		if (!formatter.TryValue(stringValue))
			Throw(std::runtime_error("Unexpected blob while looking for value in text formatter"));

		auto casted = ImpliedTyping::ParseFullMatch<Type>(stringValue);
		if (!casted)
			Throw(std::runtime_error("Could not convert value to the required type in text formatter"));

		return casted.value();
	}

	template<typename Type>
		Type RequireValue(EntityInterface::IDynamicFormatter& formatter)
	{
		StringSection<> stringValue;
		if (!formatter.TryValue(stringValue))
			Throw(std::runtime_error("Unexpected blob while looking for value in text formatter"));

		auto casted = ImpliedTyping::ParseFullMatch<Type>(stringValue);
		if (!casted)
			Throw(std::runtime_error("Could not convert value to the required type in text formatter"));

		return casted.value();
	}

	static void RequireBlobsFromCfg1(EntityInterface::IDynamicFormatter& fmttr)
	{
		REQUIRE(RequireKeyedItem(fmttr).AsString() == "SomeProperty");
		REQUIRE(RequireValue<unsigned>(fmttr) == 1);
		REQUIRE(RequireKeyedItem(fmttr).AsString() == "ASequence");
		RequireBeginElement(fmttr);
		REQUIRE(RequireValue<unsigned>(fmttr) == 1);
		REQUIRE(RequireValue<unsigned>(fmttr) == 2);
		REQUIRE(RequireValue<unsigned>(fmttr) == 3);
		REQUIRE(RequireValue<unsigned>(fmttr) == 4);
		RequireEndElement(fmttr);
		SkipValueOrElement(fmttr);		// skip unnamed element
		REQUIRE(RequireKeyedItem(fmttr).AsString() == "InternalPoint");
		SkipValueOrElement(fmttr);		// skip InternalPoint
	}

	static void RequireBlobsFromCfg2(EntityInterface::IDynamicFormatter& fmttr)
	{
		REQUIRE(RequireKeyedItem(fmttr).AsString() == "ASequence");
		RequireBeginElement(fmttr);
		REQUIRE(RequireValue<unsigned>(fmttr) == 6);
		REQUIRE(RequireValue<unsigned>(fmttr) == 3);
		REQUIRE(RequireValue<unsigned>(fmttr) == 5);
		REQUIRE(RequireValue<unsigned>(fmttr) == 6);
		RequireEndElement(fmttr);
		RequireBeginElement(fmttr);
		REQUIRE(RequireKeyedItem(fmttr).AsString() == "value2");
		REQUIRE(Utility::RequireValue(fmttr).AsString() == "five");
		RequireEndElement(fmttr);
		REQUIRE(RequireKeyedItem(fmttr).AsString() == "SomeProperty");
		REQUIRE(RequireValue<unsigned>(fmttr) == 5);
	}

	template<typename Type>
		static Type RequireActualize(::Assets::Future<Type>& future)
	{
		future.StallWhilePending();
		REQUIRE(future.GetAssetState() == ::Assets::AssetState::Ready);
		return future.Actualize();
	}
	
	TEST_CASE( "EntityInterface-Mount", "[formatters]" )
	{
		auto globalServices = ConsoleRig::MakeAttachablePtr<ConsoleRig::GlobalServices>(GetStartupConfig());
		auto utdatamnt = ::Assets::MainFileSystem::GetMountingTree()->Mount("ut-data", ::Assets::CreateFileSystem_Memory(s_utData, s_defaultFilenameRules, ::Assets::FileSystemMemoryFlags::UseModuleModificationTime));

		auto mountingTree = EntityInterface::CreateMountingTree(EntityInterface::MountingTreeFlags::LogMountPoints);
		auto cfg1Document = EntityInterface::CreateTextEntityDocument("ut-data/examplecfg1.dat");
		mountingTree->MountDocument("cfg", cfg1Document);

		//
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
		// The mounting tree has to handle two types of overlapping
		// So, for example if we have the mounts:
		//		cfg -> TextEntityDocument A
		//		cfg/one -> TextEntityDocument B
		//		cfg/one/two -> TextEntityDocument C
		//
		// If we call BeginFormatter("cfg"), BeginFormatter("cfg/one") or BeginFormatter("cfg/one/two"), in case
		// we will iterate through all 3 documents.
		// In the middle case, BeginFormatter("cfg/one"):
		//		TextEntityDocument A is partially visible (we see only a internal subset)
		//		TextEntityDocument B is unchanged from reading it directly
		//		TextEntityDocument C is entirely visible, but embedded within a "virtual" element called "two"
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
		//

		SECTION("Read values through IDynamicFormatter") {
			// ensure that the first few values we read match what we expect from the input file
			auto fmttr = RequireActualize(*mountingTree->BeginFormatter("cfg"));
			RequireBlobsFromCfg1(*fmttr);
			REQUIRE(fmttr->PeekNext() == FormatterBlob::None);
		}

		SECTION("Internal section in IDynamicFormatter") {
			// Begin a formatter from a start point within a document
			// Ie, "InternalPoint" is just an element within a document, but we'll treat it as the start point for the formatter
			auto fmttr = RequireActualize(*cfg1Document->BeginFormatter("InternalPoint"));
			REQUIRE(RequireKeyedItem(*fmttr).AsString() == "A");
			REQUIRE(Utility::RequireValue(*fmttr).AsString() == "B");
			REQUIRE(RequireKeyedItem(*fmttr).AsString() == "C");
			REQUIRE(Utility::RequireValue(*fmttr).AsString() == "D");
			REQUIRE(RequireKeyedItem(*fmttr).AsString() == "SomethingInside");
			RequireBeginElement(*fmttr);
			REQUIRE(RequireKeyedItem(*fmttr).AsString() == "E");
			REQUIRE(Utility::RequireValue(*fmttr).AsString() == "F");
			RequireEndElement(*fmttr);
			REQUIRE(fmttr->PeekNext() == FormatterBlob::None);		// "None" here, rather than EndElement, because we're emulating a subfile with the internal point
		}

		SECTION("Deep internal section in IDynamicFormatter") {
			// Begin a formatter from a start point within a document
			// this time, we're 2 sections deap
			auto fmttr = RequireActualize(*cfg1Document->BeginFormatter("InternalPoint/SomethingInside"));
			REQUIRE(RequireKeyedItem(*fmttr).AsString() == "E");
			REQUIRE(Utility::RequireValue(*fmttr).AsString() == "F");
			REQUIRE(fmttr->PeekNext() == FormatterBlob::None);		// "None" here, rather than EndElement, because we're emulating a subfile with the internal point
		}

		SECTION("Simple external section in IDynamicFormatter") {
			// Begin a formatter from a start point that isn't actually within a document, but
			// a document is mounted somewhere below.
			// In other words, we have to make a few virtual elements that will surround the
			// document (in this case, one called "one" and one called "two")
			auto cfg2Document = EntityInterface::CreateTextEntityDocument("ut-data/examplecfg2.dat");
			auto mnt = mountingTree->MountDocument("mountPt/one/two", cfg2Document);

			auto fmttrFuture = mountingTree->BeginFormatter("mountPt");
			fmttrFuture->StallWhilePending();
			auto fmttr = fmttrFuture->Actualize();

			REQUIRE(RequireKeyedItem(*fmttr).AsString() == "one");
			RequireBeginElement(*fmttr);
			REQUIRE(RequireKeyedItem(*fmttr).AsString() == "two");
			RequireBeginElement(*fmttr);
			RequireBlobsFromCfg2(*fmttr);
			RequireEndElement(*fmttr);
			RequireEndElement(*fmttr);
			REQUIRE(fmttr->PeekNext() == FormatterBlob::None);

			auto str = ::Assets::AsString(fmttrFuture->GetActualizationLog());
			REQUIRE(str == "[mountPt/one/two/] internal:  external: one/two\n");

			mountingTree->UnmountDocument(mnt);
		}

		SECTION("Multi overlapping documents") {
			auto cfg2Document = EntityInterface::CreateTextEntityDocument("ut-data/examplecfg2.dat");

			mountingTree->MountDocument("overlap", cfg1Document);
			mountingTree->MountDocument("overlap/one", cfg2Document);
			mountingTree->MountDocument("overlap/one/two", cfg1Document);

			auto fmttr0Future = mountingTree->BeginFormatter("overlap");
			auto fmttr1Future = mountingTree->BeginFormatter("overlap/one");
			auto fmttr2Future = mountingTree->BeginFormatter("overlap/one/two");

			fmttr0Future->StallWhilePending();
			fmttr1Future->StallWhilePending();
			fmttr2Future->StallWhilePending();
			auto str0 = ::Assets::AsString(fmttr0Future->GetActualizationLog());
			auto str1 = ::Assets::AsString(fmttr1Future->GetActualizationLog());
			auto str2 = ::Assets::AsString(fmttr2Future->GetActualizationLog());

			REQUIRE(str0 == "[overlap/] internal:  external: \n[overlap/one/] internal:  external: one\n[overlap/one/two/] internal:  external: one/two\n");
			REQUIRE(str1 == "[overlap/] internal: one external: \n[overlap/one/] internal:  external: \n[overlap/one/two/] internal:  external: two\n");
			REQUIRE(str2 == "[overlap/] internal: one/two external: \n[overlap/one/] internal: two external: \n[overlap/one/two/] internal:  external: \n");
		}

		// overlapped documents  
		SECTION("Simple overlapped text documents") {
			auto cfg2Document = EntityInterface::CreateTextEntityDocument("ut-data/examplecfg2.dat");
			mountingTree->MountDocument("cfg", cfg2Document);

			auto fmttrFuture = mountingTree->BeginFormatter("cfg");
			fmttrFuture->StallWhilePending();
			auto fmttr = fmttrFuture->Actualize();

			// blobs from the first cfg come first
			RequireBlobsFromCfg1(*fmttr);

			// followed by blobs from the second
			RequireBlobsFromCfg2(*fmttr);
			REQUIRE(fmttr->PeekNext() == FormatterBlob::None);
		}
	}

	// StreamLocation should tell us something about the document we're reading
	// SkipValueOrElement might need to be specialized in IDynamicFormatter
}

