// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "../../Formatters/BinaryFormatter.h"
#include <string>
#include <sstream>
#include "catch2/catch_test_macros.hpp"
#include "catch2/catch_approx.hpp"

using namespace Catch::literals;
namespace UnitTests
{
    TEST_CASE( "BinarySchemata-BasicParsing", "[formatters]" )
    {
        //
        //  We're just going to try parsing some example definitions in the binary schemata language,
        //  and ensure that we can successfully run a binary formatter using this input
        //
        const char* testBlock = R"(
		block TemplatedType {
			uint16 InternalMember0[4];
			uint8 InternalMember1;
		};

		block template(typename T) TemplatedType2 {
			float32 InternalMember2;
			T InternalMember3;
		};

		block TestBlock {
			uint32 SomeValue;
			float32 AnotherValue;
			#if SomeValue == 3
				uint16 thisShouldntBeHere;
			#elif SomeValue > 4 && Version > 32
				uint16 butThisShouldBeHere;
			#endif
			TemplatedType(expr 32) SimpleTemplate;
			TemplatedType(expr 4+SomeValue) ArrayMember[5];
			TemplatedType(expr 2*SomeValue) ComplexArrayMember[SomeValue & 0xf];
			TemplatedType2(typename uint32, expr 4) NestedTemplate;
			TemplatedType2(typename TemplatedType(expr 4), expr 6) NestedTemplate2;
		};
		)";

		struct templ
		{
			uint32 SomeValue;
			float AnotherValue;
		};

		auto decl = std::make_shared<Formatters::BinarySchemata>(testBlock, ::Assets::DirectorySearchRules{}, ::Assets::DependencyValidation{});

		std::vector<uint64_t> bigBuffer(1024, 0);
		((templ*)bigBuffer.data())->SomeValue = 5 + 0x30;
		((templ*)bigBuffer.data())->AnotherValue = 32.5f;
		Formatters::EvaluationContext context;
		context.SetGlobalParameter("Version", 48);

		Formatters::BinaryFormatter formatter(context, bigBuffer);
		formatter.PushPattern(decl, decl->FindBlockDefinition("TestBlock"));
        std::stringstream str;
		Formatters::SerializeBlock(str, formatter);

        auto out = str.str();
        REQUIRE(out.find("thisShouldntBeHere") == std::string::npos);
        REQUIRE(out.find("butThisShouldBeHere") != std::string::npos);

		Formatters::BinaryFormatter formatter2(context, bigBuffer);
		formatter2.PushPattern(decl, decl->FindBlockDefinition("TestBlock"));
		Formatters::BinaryBlockMatch blockMatch(formatter2);
		REQUIRE(blockMatch["SomeValue"].As<int64_t>() == ((templ*)bigBuffer.data())->SomeValue);
		REQUIRE(blockMatch["NestedTemplate2"]["InternalMember3"]["InternalMember1"].As<int64_t>() == 0);
    }
}
