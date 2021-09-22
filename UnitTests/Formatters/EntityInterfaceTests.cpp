// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "../../ToolsRig/EntityInterface.h"
#include "../../../Assets/IFileSystem.h"
#include "../../../Assets/OSFileSystem.h"
#include "../../../Assets/MountingTree.h"
#include "../../../Assets/MemoryFile.h"
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
		virtual FormatterBlob PeekNext() { return _fmttr->PeekNext(); }

		virtual bool TryBeginElement() { return _fmttr->TryBeginElement(); }
		virtual bool TryEndElement() { return _fmttr->TryEndElement(); }
		virtual bool TryKeyedItem(StringSection<>& name) { return _fmttr->TryKeyedItem(name); }
		virtual bool TryValue(StringSection<>& value) { return _fmttr->TryValue(value); }
		virtual bool TryCharacterData(StringSection<>& cdata) { return _fmttr->TryCharacterData(cdata); }

		virtual StreamLocation GetLocation() const { return _fmttr->GetLocation(); }
		virtual ::Assets::DependencyValidation GetDependencyValidation() const { return _cfgFile->GetDependencyValidation(); }

		InputStreamFormatterAdapter(
			std::shared_ptr<::Assets::ConfigFileContainer<Formatter>> cfgFile,
			StringSection<typename Formatter::value_type> internalSection)
		: _cfgFile(cfgFile)
		{
			_fmttr = _cfgFile->GetFormatter(internalSection);
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
			assert(_lock.is_locked());
			auto result = std::make_shared<::Assets::FuturePtr<IDynamicFormatter>>();
			::Assets::WhenAll(_srcFile).Then(
				[ip=internalPoint.AsString()](auto cfgFileContainer) {
					return std::make_shared<TextFormatterAdapter>(std::move(cfgFileContainer), ip);
				});
			return result;
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

		TextEntityDocument(std::string src) : _srcs(src)
		{
			_srcFile = ::Assets::MakeAsset<::Assets::ConfigFileContainer<>>(_src)
		}

	private:
		Threading::Mutex _lock;
		std::string _src;
		::Assets::FuturePtr<::Assets::ConfigFileContainer<>> _srcFile;
	};

	std::shared_ptr<IEntityDocument> CreateTextEntityDocument(StringSection<> src)
	{
		return std::make_shared<TextEntityDocument>(src);
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
		)--"),
		std::make_pair(
			"examplecfg2.dat",
			::Assets::AsBlob(R"--(
				ASequence=~
					6, 3, 5, 6
				=~
					value2=five
				SomeProperty=5
		)--"),		
		)
	};
	
	TEST_CASE( "EntityInterface-Mount", "[formatters]" )
	{
		auto globalServices = ConsoleRig::MakeAttachablePtr<ConsoleRig::GlobalServices>(GetStartupConfig());
		auto utdatamnt = ::Assets::MainFileSystem::GetMountingTree()->Mount("ut-data", ::Assets::CreateFileSystem_Memory(s_utData, s_defaultFilenameRules, ::Assets::FileSystemMemoryFlags::UseModuleModificationTime));

		auto mountingTree = EntityInterface::CreateMountingTree();
		mountingTree->MountDocument("cfg", CreateTextEntityDocument("ut-data/examplecfg1.dat"));

		SECTION("Read values through IDynamicFormatter") {
			// ensure that the first few values we read match what we expect from the input file
			auto fmttr = mountingTree->BeginFormatter("cfg");
			REQUIRE(RequireKeyedItem(*fmttr).AsString() == "SomeProperty");
			REQUIRE(RequireValue<unsigned>(*fmttr) == 1);
			REQUIRE(RequireKeyedItem(*fmttr).AsString() == "ASequence");
			RequireBeginElement(*fmttr);
			REQUIRE(RequireValue<unsigned>(*fmttr) == 1);
			REQUIRE(RequireValue<unsigned>(*fmttr) == 2);
			REQUIRE(RequireValue<unsigned>(*fmttr) == 3);
			REQUIRE(RequireValue<unsigned>(*fmttr) == 4);
			RequireEndElement(*fmttr);
		}
	}
}

