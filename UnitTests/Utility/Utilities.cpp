// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "../../Utility/ParameterBox.h"
#include "../../Utility/StringFormat.h"
#include "../../Utility/Streams/Stream.h"
#include "../../Utility/Streams/StreamTypes.h"
#include "../../Utility/Streams/PathUtils.h"
#include "../../Utility/FunctionUtils.h"
#include "../../Utility/MemoryUtils.h"
#include "../../Utility/Conversion.h"
#include "../../Utility/FastParseValue.h"
#include <stdexcept>
#include "catch2/catch_test_macros.hpp"
#include "catch2/catch_approx.hpp"

using namespace Catch::literals;
namespace UnitTests
{
    static int foo(int x, int y, int z) { return x + y + z; }
    static int foo1(int x, int y, int z) { return x + y + z; }
    static float foo1(int x, int y, float z) { return x + y + z; }

    class ThrowOnDestructor
    {
    public:
        static bool s_expectingDestroy;
        static unsigned s_destroyCount;
        ~ThrowOnDestructor() { ++s_destroyCount; if (!s_expectingDestroy) Throw(std::runtime_error("Object was destroyed at unexpected time")); }
    };

    bool ThrowOnDestructor::s_expectingDestroy = false;
    unsigned ThrowOnDestructor::s_destroyCount = 0;

    TEST_CASE( "Utilities-ParameterBoxTest", "[utility]" )
    {
        ParameterBox test(
            {
                std::make_pair("SomeParam", "1u"),
                std::make_pair("SomeParam1", ".4f"),
                std::make_pair("SomeParam2", "344.f"),
                std::make_pair("SomeParam3", ".56f"),
                std::make_pair("SomeParam4", "78f"),
                std::make_pair("VectorParam", "{4.5f, 7.5f, 9.5f}v"),
                std::make_pair("ColorParam", "{.5f, .5f, .5f}c")
            });

        REQUIRE( test.GetParameter<unsigned>("SomeParam").value() == 1u );
        REQUIRE( test.GetParameter<float>("SomeParam1").value() == .4_a );
        REQUIRE( test.GetParameter<float>("SomeParam2").value() == 344_a );
        REQUIRE( test.GetParameter<float>("SomeParam3").value() == .56_a );

        test.SetParameter("AParam", false);
        test.SetParameter("AParam", 5);
        test.SetParameter("AParam", 5.f);
        test.SetParameter("AParam", 500.f);
        REQUIRE( test.GetParameter<float>("AParam").value() == 500_a );

        test.SetParameter("ShouldBeTrue", true);
        REQUIRE( test.GetParameter<bool>("ShouldBeTrue").value() == true );

        std::vector<std::pair<const utf8*, std::string>> stringTable;
        BuildStringTable(stringTable, test);

        // for (auto i=stringTable.begin(); i!=stringTable.end(); ++i) {
        //     XlOutputDebugString(
        //         StringMeld<256>() << i->first << " = " << i->second << "\n");
        // }

    }

    template<typename CharType>
        static void FillStream(StreamBuf<CharType>& stream)
        {
            stream.WriteChar('B');
            stream.Write(u8"<<StringB>>");
            stream.WriteChar('D');
            stream.Write(u8"<<StringD>>");
        }

    TEST_CASE( "Utilities-MemoryStreamTest", "[utility]" )
    {
        auto memStreamA = std::make_unique<MemoryOutputStream<char>>();
        // auto memStreamB = std::make_unique<MemoryOutputStream<utf16>>();
        auto memStreamC = std::make_unique<MemoryOutputStream<utf8>>();
        FillStream(*memStreamA);
        // FillStream(*memStreamB);
        FillStream(*memStreamC);

        auto stringA = memStreamA->AsString();
        // auto stringB = memStreamB->AsString();
        auto stringC = memStreamC->AsString();

        REQUIRE(stringA == "B<<StringB>>D<<StringD>>");
        // REQUIRE(stringB == (const utf16*)u"B<<StringB>>D<<StringD>>");
        REQUIRE(stringC == u8"B<<StringB>>D<<StringD>>");
    }

    TEST_CASE( "Utilities-GlyphCount", "[utility]" )
    {
        // const utf8* utf8InputLiteral = u8"z\u00df\u6c34\U0001f34c"; // L"zß水🍌"
        // This is just an odd unicode string with characters of different byte sizes. There are glyphs from different languages, as well as some emojis in there
        // There are 65 separate characters
        const utf8* utf8InputLiteral = u8"\u8272\u306f\u5302\u3078\u3069\U0001f534\x20\U0001f959\u6563\u308a\u306c\u308b\u3092\U0001f34e\x0a\uc6b0\ub9ac\ub098\ub77c\uc758\U0001f4c5\x20\ub300\ud45c\U0001f3f4\uc801\uc778\x20\uace0\uc591\uc774\uc0c1\x20\U0001f4bb\uc5ec\ubc30\uc6b0\ub77c\uace0\x20\ud558\uba74\x20\ub204\uad6c\ub098\x20\ube60\uc9d0\uc5c6\uc774\x20\ub5a0\uc624\U0001f4fd\ub974\ub294\x20\ubc30\uc6b0\U0001f6e1\uac00\x20\uc788\uc8e0";
        const utf16* utf16InputLiteral = u"\u8272\u306f\u5302\u3078\u3069\U0001f534\x20\U0001f959\u6563\u308a\u306c\u308b\u3092\U0001f34e\x0a\uc6b0\ub9ac\ub098\ub77c\uc758\U0001f4c5\x20\ub300\ud45c\U0001f3f4\uc801\uc778\x20\uace0\uc591\uc774\uc0c1\x20\U0001f4bb\uc5ec\ubc30\uc6b0\ub77c\uace0\x20\ud558\uba74\x20\ub204\uad6c\ub098\x20\ube60\uc9d0\uc5c6\uc774\x20\ub5a0\uc624\U0001f4fd\ub974\ub294\x20\ubc30\uc6b0\U0001f6e1\uac00\x20\uc788\uc8e0";
        const wchar_t* wcharInputLiteral = L"\u8272\u306f\u5302\u3078\u3069\U0001f534\x20\U0001f959\u6563\u308a\u306c\u308b\u3092\U0001f34e\x0a\uc6b0\ub9ac\ub098\ub77c\uc758\U0001f4c5\x20\ub300\ud45c\U0001f3f4\uc801\uc778\x20\uace0\uc591\uc774\uc0c1\x20\U0001f4bb\uc5ec\ubc30\uc6b0\ub77c\uace0\x20\ud558\uba74\x20\ub204\uad6c\ub098\x20\ube60\uc9d0\uc5c6\uc774\x20\ub5a0\uc624\U0001f4fd\ub974\ub294\x20\ubc30\uc6b0\U0001f6e1\uac00\x20\uc788\uc8e0";

        auto utf8String = std::basic_string<utf8>(utf8InputLiteral);
        auto* utf8NullTerminated = utf8String.c_str();
        auto utf16String = std::basic_string<utf16>(utf16InputLiteral);
        auto* utf16NullTerminated = utf16String.c_str();
        auto wcharString = std::basic_string<wchar_t>(wcharInputLiteral);
        auto* wcharNullTerminated = wcharString.c_str();

        // XlStringSize() and StringSection::Length return the number of character primitives
        // used by the string (ie, the number of bytes is XlStringSize() * sizeof(CharType))
        // This is also the same as std::string::size()
        auto countInCharPrimitives1 = XlStringSize(utf8NullTerminated);
        auto countInCharPrimitives2 = XlStringSize(utf16NullTerminated);
        auto countInCharPrimitives1a = utf8String.size();
        auto countInCharPrimitives2a = utf16String.size();
        auto countInCharPrimitives1b = MakeStringSection(utf8String).Length();
        auto countInCharPrimitives2b = MakeStringSection(utf16String).Length();
        REQUIRE( countInCharPrimitives1 == countInCharPrimitives1a );
        REQUIRE( countInCharPrimitives1 == countInCharPrimitives1b );
        REQUIRE( countInCharPrimitives2 == countInCharPrimitives2a );
        REQUIRE( countInCharPrimitives2 == countInCharPrimitives2b );

        // The number of character primitives in this utf16 is less that 
        // the utf8 version for this particular string
        REQUIRE( countInCharPrimitives1 > countInCharPrimitives2 );
        // Also, the number of bytes for the utf16 version is less (but again that might be specific to this string)
        REQUIRE( (countInCharPrimitives1 * sizeof(utf8)) > (countInCharPrimitives2 * sizeof(utf16)) );

        // XlGlyphCount returns the number of glyphs in the string, regardless of
        // how they are stored
        auto characterCount1 = XlGlyphCount(utf8NullTerminated);
        auto characterCount2 = XlGlyphCount(utf16NullTerminated);
        REQUIRE( characterCount1 == characterCount2 );
        REQUIRE( characterCount1 < countInCharPrimitives1 );
        REQUIRE( characterCount2 < countInCharPrimitives2 );
    }
        
    TEST_CASE( "Utilities-MakeFunctionTest", "[utility]" )
    {
        using namespace std::placeholders;
            
            // unambuiguous
        auto f0 = MakeFunction(foo);
        auto f1 = MakeFunction([](int x, int y, int z) { return x + y + z;});
        REQUIRE( MakeFunction([](int x, int y, int z) { return x + y + z;})(1,2,3) == 6 );
            
        int first = 4;
        auto lambda_state = [=](int y, int z) { return first + y + z;}; //lambda with states
        REQUIRE( MakeFunction(lambda_state)(1,2) == 7 );
            
            // ambuiguous cases
        auto f2 = MakeFunction<int,int,int,int>(std::bind(foo,_1,_2,_3)); //bind results has multiple operator() overloads
        REQUIRE( f2(1,2,3) == 6 );
        auto f3 = MakeFunction<int,int,int,int>(foo1);     //overload1
        auto f4 = MakeFunction<float,int,int,float>(foo1); //overload2

        REQUIRE( f3(1,2,3) == 6 );
        REQUIRE( f4(1,2,3.5f) == 6.5_a );
    }

    TEST_CASE( "Utilities-VariationFunctionsTest", "[utility]" )
    {
        using namespace std::placeholders;

        VariantFunctions fns;

        fns.Add(0, foo);
        REQUIRE( fns.Call<int>(0, 10, 20, 30) == 60 );
        fns.Remove(0);

        fns.Add(0, [](int x, int y) { return x+y; });
        REQUIRE( fns.Call<int>(0, 10, 20) == 30 );

        {
                // test holding a reference along with the function ptr
            {
                auto obj = std::make_shared<ThrowOnDestructor>();
                fns.Add(1000, [obj]() { return obj;});
            }

            auto ptr = fns.Call<std::shared_ptr<ThrowOnDestructor>>(1000);
            ptr.reset();

                // the actual object should only be destroyed during
                // this "Remove" call
            ThrowOnDestructor::s_expectingDestroy = true;
            fns.Remove(1000);
            ThrowOnDestructor::s_expectingDestroy = false;
        
            REQUIRE( ThrowOnDestructor::s_destroyCount == 1u );
        }

        auto bindFn = MakeFunction<int, int>(
            std::bind(
                [](int x, int y) { return x+y; },
                _1, 20));
        fns.Add(1, std::move(bindFn));
        REQUIRE( fns.Call<int>(1, 10) == 30 );
        REQUIRE( fns.Get<int(int)>(1)(10) == 30 );

            // attempting to call functions that don't exist
        REQUIRE( fns.CallDefault<int>(3, 10) == 10 );
        int res = 0;
        REQUIRE( !fns.TryCall<int>(res, 3) );

        REQUIRE( fns.Has<int(int)>(1) );
        REQUIRE( !fns.Has<int(int)>(2) );

        REQUIRE_THROWS_AS(
            [&]() {
                fns.Has<int(int, int)>(1);
            }(),
            VariantFunctions::SignatureMismatch);

            // heavy load test (will crash if there are any failures)
        for (auto i=0u; i<100; ++i)
            fns.Add(100+i, [](int x, int y) { return x+y; });
    }

    TEST_CASE( "Utilities-MakeRelativePathTest", "[utility]" )
    {
        REQUIRE(
            std::string("SomeDir/Source/SourceFile.cpp") ==
            MakeRelativePath(
                MakeSplitPath("D:\\LM\\Code"), 
                MakeSplitPath("D:\\LM\\Code\\SomeDir\\Source\\SourceFile.cpp")));

        REQUIRE(
            std::string("D:/LM/.Source/SourceFile.cpp") ==
            MakeSplitPath("D:\\LM\\Code\\.././\\SomeDir\\..\\.Source/////\\SourceFile.cpp").Simplify().Rebuild());

        REQUIRE(
            std::string("D:/LM/SomeDir/") ==
            MakeSplitPath("D:\\LM\\Code../..\\SomeDir/").Simplify().Rebuild());

        REQUIRE(
            std::string("somefile.txt") ==
            MakeSplitPath(".///somefile.txt").Simplify().Rebuild());

        REQUIRE(
            std::string("") ==
            MakeSplitPath(".///").Simplify().Rebuild());

        REQUIRE(
            std::string("") ==
            MakeSplitPath(".///somepath//..//A/B/../..///").Simplify().Rebuild());

        REQUIRE(
            std::string("SomeObject") ==
            MakeRelativePath(MakeSplitPath("D:\\LM\\Code"), MakeSplitPath("D:\\LM\\Code\\SomeObject")));

        REQUIRE(
            std::string("SomeObject/") ==
            MakeRelativePath(MakeSplitPath("D:\\LM\\Code"), MakeSplitPath("D:\\LM\\Code\\SomeObject\\")));

        REQUIRE(
            std::string("../../SomeDir/Source/SourceFile.cpp") ==
            MakeRelativePath(MakeSplitPath("D:\\LM\\Code\\SomeOtherDirectory\\Another\\"), MakeSplitPath("D:\\LM\\Code\\SomeDir\\Source\\SourceFile.cpp")));

        REQUIRE(
            std::string("../../Code/SomeDir/Source/SourceFile.cpp") ==
            MakeRelativePath(MakeSplitPath("D:\\./LM\\\\Code\\..\\SomeOtherDirectory\\/\\Another\\"), MakeSplitPath("D:\\LM\\Code\\SomeDir\\Source\\SourceFile.cpp")));

        REQUIRE(
            std::string("Source/SourceFile.cpp") ==
            MakeRelativePath(MakeSplitPath("D:\\LM\\Code\\SomeOtherDirectory\\Another\\../.."), MakeSplitPath("D:\\LM\\Code\\SomeDir\\../.\\Source\\./SourceFile.cpp")));

        // When all of the path segments do not match, we can either end up with a full path
        // If both paths are absolute, it gets relativitized
        REQUIRE(
            std::string("../../SomePath/Source/SourceFile.cpp") ==
            MakeRelativePath(MakeSplitPath("D:\\AnotherPath\\Something\\"), MakeSplitPath("D:\\SomePath\\Source\\SourceFile.cpp")));

        // But if both paths are not absolute (ie, relative to the current working directory)
        // then we don't relativitize the path
        REQUIRE(
            std::string("D:SomePath/Source/SourceFile.cpp") ==
            MakeRelativePath(MakeSplitPath("D:AnotherPath\\Something\\"), MakeSplitPath("D:SomePath\\Source\\SourceFile.cpp")));
    }

    TEST_CASE( "Utilities-CaseInsensitivePathHandling", "[utility]" )
    {
        // MakeRelativePath shoudl behave differently for case sensitive vs insensitive paths  
        FilenameRules caseInsensitiveRules('/', false);
        FilenameRules caseSensitiveRules('/', true);

        // ignore case when matching directory names when using case insensitive rules
        REQUIRE(
            std::string("somefolder/someobject") ==
            MakeRelativePath(MakeSplitPath("D:\\lm\\code"), MakeSplitPath("D:\\LM\\Code\\SomeFolder\\SomeObject"), caseInsensitiveRules));

        // But case is important in directory names when using case sensitive rules
        REQUIRE(
            std::string("../Code/SomeFolder/SomeObject") ==
            MakeRelativePath(MakeSplitPath("D:\\LM\\code"), MakeSplitPath("D:\\LM\\Code\\SomeFolder\\SomeObject"), caseSensitiveRules));
    }

    TEST_CASE( "Utilities-MiscHashTest", "[utility]" )
    {
        StringSection<> s0("somestring"), s1("1234567890qwerty");
        REQUIRE(
            ConstHash64<'some', 'stri', 'ng'>::Value ==
            ConstHash64FromString(s0.begin(), s0.end()));
        REQUIRE(
            ConstHash64<'1234', '5678', '90qw', 'erty'>::Value ==
            ConstHash64FromString(s1.begin(), s1.end()));
    }

    TEST_CASE( "Utilities-FastParseValue (integer)", "[utility]" )
    {
        const uint64_t testCount = 100000;
        for (uint64_t t=0; t<testCount; ++t) {
            auto u32 = (std::numeric_limits<uint32_t>::max() / testCount) * t;
            char buffer[64];
            uint32_t parsed = 0;

            XlUI32toA(u32, buffer, dimof(buffer), 10);
            REQUIRE(*FastParseValue(MakeStringSection(buffer), parsed) == '\0');
            REQUIRE(parsed == u32);
            REQUIRE(*FastParseValue(MakeStringSection(buffer), parsed, 10) == '\0');
            REQUIRE(parsed == u32);

            XlUI32toA(u32, buffer, dimof(buffer), 16);
            REQUIRE(*FastParseValue(MakeStringSection(buffer), parsed, 16) == '\0');
            REQUIRE(parsed == u32);

            XlUI32toA(u32, buffer, dimof(buffer), 8);
            REQUIRE(*FastParseValue(MakeStringSection(buffer), parsed, 8) == '\0');
            REQUIRE(parsed == u32);
        }

        for (uint64_t t=0; t<testCount; ++t) {
            auto i64 = int64_t((std::numeric_limits<uint64_t>::max() / testCount) * t);
            char buffer[128];
            int64_t parsed = 0;

            XlI64toA(i64, buffer, dimof(buffer), 10);
            REQUIRE(*FastParseValue(MakeStringSection(buffer), parsed) == '\0');
            REQUIRE(parsed == i64);
            REQUIRE(*FastParseValue(MakeStringSection(buffer), parsed, 10) == '\0');
            REQUIRE(parsed == i64);

            XlI64toA(i64, buffer, dimof(buffer), 16);
            REQUIRE(*FastParseValue(MakeStringSection(buffer), parsed, 16) == '\0');
            REQUIRE(parsed == i64);

            XlI64toA(i64, buffer, dimof(buffer), 8);
            REQUIRE(*FastParseValue(MakeStringSection(buffer), parsed, 8) == '\0');
            REQUIRE(parsed == i64);
        }
    }
}

