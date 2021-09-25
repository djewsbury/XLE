// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "../../Math/Vector.h"
#include "../../Math/MathSerialization.h"
#include "../../Utility/Conversion.h"
#include "../../Utility/ImpliedTyping.h"
#include "../../Utility/Streams/SerializationUtils.h"
#include <string>
#include <sstream>
#include "catch2/catch_test_macros.hpp"
#include "catch2/catch_approx.hpp"

using namespace Catch::literals;

namespace UnitTests
{

    TEST_CASE( "ConversionPatterns-ImpliedTyping", "[utility]" )
    {
        // Conversion from string into basic value types via the ImpliedTyping system
        REQUIRE( ImpliedTyping::ConvertFullMatch<unsigned>("123u") == 123u );

        // Conversion into strings from basic value types via the ImpliedTyping system
        REQUIRE( ImpliedTyping::AsString(UInt3(1, 2, 3)) == "{1, 2, 3}v" );
    }

    static bool JustWhitespace(StringSection<> str)
    {
        for (auto c:str)
            if (c != ' ' && c != '\t')
                return false;
        return true;
    }

    template<typename Type>
        static void CompareConversionPaths(StringSection<> stringForm, Type value)
    {
        // Parse() followed by a Cast() should give the same result as Convert()
        // Convert is just a more efficient way to get there
        char midwayBuffer[1024];
        Type convertedCopy;
        auto parseResult = ImpliedTyping::Parse(stringForm, MakeOpaqueIteratorRange(midwayBuffer));
        REQUIRE(JustWhitespace(MakeStringSection(parseResult._end, stringForm.end())));
        ImpliedTyping::Cast(
            MakeOpaqueIteratorRange(convertedCopy), ImpliedTyping::TypeOf<Type>(), 
            MakeIteratorRange(midwayBuffer, PtrAdd(midwayBuffer, parseResult._type.GetSize())), parseResult._type);
        REQUIRE(convertedCopy == value);

        auto parseFullMatch = ImpliedTyping::ParseFullMatch(stringForm, MakeOpaqueIteratorRange(midwayBuffer));
        REQUIRE(parseFullMatch._type != ImpliedTyping::TypeCat::Void);
        ImpliedTyping::Cast(
            MakeOpaqueIteratorRange(convertedCopy), ImpliedTyping::TypeOf<Type>(), 
            MakeIteratorRange(midwayBuffer, PtrAdd(midwayBuffer, parseResult._type.GetSize())), parseResult._type);
        REQUIRE(convertedCopy == value);

        auto conversionResult = ImpliedTyping::Convert(stringForm, MakeOpaqueIteratorRange(convertedCopy), ImpliedTyping::TypeOf<Type>());
        REQUIRE(JustWhitespace(MakeStringSection(conversionResult._end, stringForm.end())));
        REQUIRE(conversionResult._successfulConvert == true);
        REQUIRE(convertedCopy == value);

        auto convertFullMatch = ImpliedTyping::ConvertFullMatch(stringForm, MakeOpaqueIteratorRange(convertedCopy), ImpliedTyping::TypeOf<Type>());
        REQUIRE(convertFullMatch);
        REQUIRE(convertedCopy == value);

        auto convertFullMatch2 = ImpliedTyping::ConvertFullMatch<Type>(stringForm);
        REQUIRE(convertFullMatch2.value() == value);
    }

    TEST_CASE( "Utilities-ImpliedTypingTest", "[utility]" )
    {
        char tempBuffer[256];
        REQUIRE(ImpliedTyping::ConvertFullMatch<signed>("true").value() == 1);
        // REQUIRE(ImpliedTyping::ConvertFullMatch<signed>("{true, 60, 1.f}").value() == 1); what should be the correct result in this case?
        REQUIRE(ImpliedTyping::ConvertFullMatch<signed>("{32}").value() == 32);
        REQUIRE(ImpliedTyping::ParseFullMatch<char>("{}", MakeIteratorRange(tempBuffer))._arrayCount == 0);
        REQUIRE(ImpliedTyping::ConvertFullMatch<unsigned>("0x5a").value() == 0x5a);
        REQUIRE(ImpliedTyping::ConvertFullMatch<unsigned>("-32u").value() == -32u);
        REQUIRE(ImpliedTyping::ConvertFullMatch<signed>("-0x7b").value() == -0x7b);
        REQUIRE(ImpliedTyping::ParseFullMatch<char>("{3u, 3u, 4.f}", MakeIteratorRange(tempBuffer))._type == ImpliedTyping::TypeCat::Float);
        REQUIRE((((float*)tempBuffer)[0] == 3.0_a && ((float*)tempBuffer)[1] == 3.0_a && ((float*)tempBuffer)[2] == 4.0_a));
        REQUIRE(ImpliedTyping::ParseFullMatch<char>("{3u, 4.f, 3u}", MakeIteratorRange(tempBuffer))._type == ImpliedTyping::TypeCat::Float);
        REQUIRE((((float*)tempBuffer)[0] == 3.0_a && ((float*)tempBuffer)[1] == 4.0_a && ((float*)tempBuffer)[2] == 3.0_a));
        REQUIRE(ImpliedTyping::ParseFullMatch<char>("{4.f, 3u, 3u}", MakeIteratorRange(tempBuffer))._type == ImpliedTyping::TypeCat::Float);
        REQUIRE((((float*)tempBuffer)[0] == 4.0_a && ((float*)tempBuffer)[1] == 3.0_a && ((float*)tempBuffer)[2] == 3.0_a));

        CompareConversionPaths("{4, 5, 6}", UInt3(4,5,6));
        CompareConversionPaths("{4,5,6}", UInt3(4,5,6));
        CompareConversionPaths("  {4,5,6}", UInt3(4,5,6));
        CompareConversionPaths("  {4,  5,6  }  ", UInt3(4,5,6));
        CompareConversionPaths("{4}", 4);
        CompareConversionPaths("   4  \t  ", 4);
        CompareConversionPaths("  \t \t 4", 4);
        CompareConversionPaths(" 4  ", 4);
        CompareConversionPaths("4", true);
        CompareConversionPaths("200i", uint8_t(200));

        CompareConversionPaths("true", true);
        CompareConversionPaths("True", true);
        CompareConversionPaths("TRUE", true);
        CompareConversionPaths("yes", true);
        CompareConversionPaths("Yes", true);
        CompareConversionPaths("YES", true);
        CompareConversionPaths("false", false);
        CompareConversionPaths("False", false);
        CompareConversionPaths("FALSE", false);
        CompareConversionPaths("no", false);
        CompareConversionPaths("No", false);
        CompareConversionPaths("NO", false);

        REQUIRE_THROWS(ImpliedTyping::ConvertFullMatch<bool>("nothing").value());
        REQUIRE_THROWS(ImpliedTyping::ConvertFullMatch<bool>("truet").value());
        bool temp = false;
        auto badConvert = ImpliedTyping::Parse(MakeStringSection("nothing"), MakeOpaqueIteratorRange(temp));
        REQUIRE(badConvert._type._type == ImpliedTyping::TypeCat::Void);

        CompareConversionPaths("-45", unsigned(-45));

        REQUIRE_THROWS(ImpliedTyping::ConvertFullMatch<unsigned>("").value());
        REQUIRE_THROWS(ImpliedTyping::ConvertFullMatch<unsigned>("    ").value());
        REQUIRE_THROWS(ImpliedTyping::ConvertFullMatch<unsigned>("   \t  \t  ").value());

        REQUIRE_THROWS(ImpliedTyping::ConvertFullMatch<signed>("-0x-304").value());
        REQUIRE_THROWS(ImpliedTyping::ConvertFullMatch<float>("0.0.0f32").value());
        REQUIRE_THROWS(ImpliedTyping::ConvertFullMatch<signed>("-+54").value());

        // The following are all poorly formed
        REQUIRE(ImpliedTyping::ParseFullMatch<char>("0x0x5a", MakeIteratorRange(tempBuffer))._type == ImpliedTyping::TypeCat::Void);
        REQUIRE(ImpliedTyping::ParseFullMatch<char>("0x-5a", MakeIteratorRange(tempBuffer))._type == ImpliedTyping::TypeCat::Void);
        REQUIRE(ImpliedTyping::ParseFullMatch<char>("5a", MakeIteratorRange(tempBuffer))._type == ImpliedTyping::TypeCat::Void);
        REQUIRE(ImpliedTyping::ParseFullMatch<char>("truefalse", MakeIteratorRange(tempBuffer))._type == ImpliedTyping::TypeCat::Void);
        REQUIRE(ImpliedTyping::ParseFullMatch<char>("3, 4, 5", MakeIteratorRange(tempBuffer))._type == ImpliedTyping::TypeCat::Void);
        REQUIRE(ImpliedTyping::ParseFullMatch<char>("32u-2", MakeIteratorRange(tempBuffer))._type == ImpliedTyping::TypeCat::Void);
        REQUIRE(ImpliedTyping::ParseFullMatch<char>("32i23", MakeIteratorRange(tempBuffer))._type == ImpliedTyping::TypeCat::Void);
        REQUIRE(ImpliedTyping::ParseFullMatch<char>("897unsigned", MakeIteratorRange(tempBuffer))._type == ImpliedTyping::TypeCat::Void);
        REQUIRE(ImpliedTyping::ParseFullMatch<char>("--54", MakeIteratorRange(tempBuffer))._type == ImpliedTyping::TypeCat::Void);
        REQUIRE(ImpliedTyping::ParseFullMatch<char>("-+54", MakeIteratorRange(tempBuffer))._type == ImpliedTyping::TypeCat::Void);
        REQUIRE(ImpliedTyping::ParseFullMatch<char>("+-54", MakeIteratorRange(tempBuffer))._type == ImpliedTyping::TypeCat::Void);
        REQUIRE(ImpliedTyping::ParseFullMatch<char>("+54", MakeIteratorRange(tempBuffer))._type == ImpliedTyping::TypeCat::Void);
        REQUIRE(ImpliedTyping::ParseFullMatch<char>("++54", MakeIteratorRange(tempBuffer))._type == ImpliedTyping::TypeCat::Void);
        REQUIRE(ImpliedTyping::ParseFullMatch<char>("0.0.0f32", MakeIteratorRange(tempBuffer))._type == ImpliedTyping::TypeCat::Void);
        REQUIRE(ImpliedTyping::ParseFullMatch<char>("{ 43 23, 545, 5 }", MakeIteratorRange(tempBuffer))._type == ImpliedTyping::TypeCat::Void);
        REQUIRE(ImpliedTyping::ParseFullMatch<char>("{ 1, 2, 3,", MakeIteratorRange(tempBuffer))._type == ImpliedTyping::TypeCat::Void);
        REQUIRE(ImpliedTyping::ParseFullMatch<char>("{ 1, 2, 3,}", MakeIteratorRange(tempBuffer))._type == ImpliedTyping::TypeCat::Void);
    }

    struct TestClass
    {
        int _c = 1;
        UInt2 _c2 = {2, 3};
    };

    void SerializationOperator(std::ostream& str, const TestClass& cls)
    {
        str << cls._c << ", ";
        str << cls._c2;
    }

    void DeserializationOperator(std::istream& str, TestClass& cls)
    {
        str >> cls._c >> cls._c2[0] >> cls._c2[1];
    }

    TEST_CASE( "ConversionPatterns-SerializationOperator", "[utility]" )
    {
        // Above we've implemented SerializationOperator and DeserializationOperator for
        // a couple of types. Typically we don't call these implementation directly -- instead
        // we access them via some more broad pattern, such as operator<< or operator>>
        //
        // Here we'll use some string streams to execute the declared serialization/deserialization
        // operators
        std::stringstream str;
        str << TestClass{};
        REQUIRE( str.str() == "1, 2 3");

        std::istringstream istr("1 2 3");
        TestClass deserialized{};
        istr >> deserialized;
        REQUIRE( deserialized._c == 1 );
        REQUIRE( deserialized._c2 == UInt2{2, 3} );
    }

    //
    // StreamOperator
    // Serialize() Deserialize
    // Conversion::Convert<>
    // .As() method in SteamDOM classes
    //
    // object -> ParmeterBox
    // ParameterBox -> object
    //
    // object -> StreamDOM
    // StreamDOM -> object
    //
    // NascentBlockSerializer
    // OutputStreamFormatter
    // "Data" class
    // 
    // input formatter for json/other formats
    // walking through the input stream formatter vs via StreamDOM
    // InputStream & OutputStream types
    //

}
