// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "../../Utility/Meta/ClassAccessors.h"
#include "../../Utility/Meta/ClassAccessorsImpl.h"
#include "../../Utility/Meta/AccessorSerialize.h"
#include "../../Formatters/TextFormatter.h"
#include "../../Formatters/TextOutputFormatter.h"
#include "../../Utility/Streams/StreamTypes.h"
#include "../../Math/Vector.h"
#include "../../Math/MathSerialization.h"
#include <stdexcept>
#include "catch2/catch_test_macros.hpp"
#include "catch2/catch_approx.hpp"

using namespace Catch::literals;
namespace UnitTests
{
    class TestClass
    {
    public:
        int _intMember = 54;
        Float4 _vectorMember = Float4{1,2,3,4};

        int GetIntMember() const { return _intMember; }
        bool GetIntMemberOpaque(IteratorRange<void*> dest, ImpliedTyping::TypeDesc destType) const 
        {
            return ImpliedTyping::Cast(
                dest, destType,
                MakeOpaqueIteratorRange(_intMember), ImpliedTyping::TypeOf<int>());
        }

        void SetIntMember(int newValue) { _intMember = newValue; }
        bool SetIntMemberOpaque(IteratorRange<const void*> src, ImpliedTyping::TypeDesc srcType)
        {
            return ImpliedTyping::Cast(
                MakeOpaqueIteratorRange(_intMember), ImpliedTyping::TypeOf<int>(),
                src, srcType);
        }
    };

    static int GetIntMember_FreeFunction(const TestClass& input) { return input._intMember; }
    static void SetIntMember_FreeFunction(TestClass& output, int newValue) { output._intMember = newValue; }

    TEST_CASE( "ClassAccessors-MemberSpecification", "[utility]" )
    {
        // For ClassAccessors::Add() to work with different types of getter/setter functions,
        // the Internal::WrapGetFunction() function must be specialized. Let's check that common forms
        // will compile first

        SECTION("WrapGetFunction") {

            ClassAccessors::Property::GetterFn getterFuns[5];

            getterFuns[0] = Utility::Internal::WrapGetFunction(
                [](const TestClass& cls) { return cls._intMember; });

            getterFuns[1] = Utility::Internal::WrapGetFunction(&TestClass::GetIntMember);

            getterFuns[2] = Utility::Internal::WrapGetFunction(&TestClass::GetIntMemberOpaque);

            getterFuns[3] = Utility::Internal::WrapGetFunction(&GetIntMember_FreeFunction);

            getterFuns[4] = Utility::Internal::WrapGetFunction(
                [](const void* cls, IteratorRange<void*> dest, ImpliedTyping::TypeDesc destType) { 
                    return ImpliedTyping::Cast(
                        dest, destType,
                        MakeOpaqueIteratorRange(((const TestClass*)cls)->_intMember), ImpliedTyping::TypeOf<int>());
                });

            TestClass ex{};
            for (const auto&fn:getterFuns) {
                float resultCastedToFloat = 0.f;
                auto res = fn(&ex, MakeOpaqueIteratorRange(resultCastedToFloat), ImpliedTyping::TypeOf<float>());
                REQUIRE(res);
                REQUIRE( resultCastedToFloat == 54_a );
            }

            auto fn0 = Utility::Internal::WrapGetFunction(
                [](const TestClass& cls) { return cls._vectorMember; });

            auto fn1 = Utility::Internal::WrapGetFunction(
                [](const TestClass& cls) { return "SomeString"; });

            (void)fn0; (void)fn1;
        }

        SECTION("WrapSetFunction") {

            ClassAccessors::Property::SetterFn setterFuns[5];

            setterFuns[0] = Utility::Internal::WrapSetFunction(
                [](TestClass& cls, int newValue) { cls._intMember = newValue; });

            setterFuns[1] = Utility::Internal::WrapSetFunction(&TestClass::SetIntMember);

            setterFuns[2] = Utility::Internal::WrapSetFunction(&TestClass::SetIntMemberOpaque);

            setterFuns[3] = Utility::Internal::WrapSetFunction(&SetIntMember_FreeFunction);

            setterFuns[4] = Utility::Internal::WrapSetFunction(
                [](void* cls, IteratorRange<const void*> src, ImpliedTyping::TypeDesc srcType) { 
                    return ImpliedTyping::Cast(
                        MakeOpaqueIteratorRange(((TestClass*)cls)->_intMember), ImpliedTyping::TypeOf<int>(),
                        src, srcType);
                });

            int iterations = 0;
            for (const auto&fn:setterFuns) {
                TestClass ex{};
                float newValueAsFloat = 1.f + iterations;
                auto res = fn(&ex, MakeOpaqueIteratorRange(newValueAsFloat), ImpliedTyping::TypeOf<float>());
                REQUIRE(res);
                REQUIRE( ex._intMember == (int)newValueAsFloat );

                ++iterations;
            }

            auto fn0 = Utility::Internal::WrapSetFunction(
                [](TestClass& cls, const Float4& newValue) { cls._vectorMember = newValue; });

            (void)fn0;
        }

        ClassAccessors accessors(TypeHashCode<TestClass>);
        accessors.Add(
            "IntMemberWithImplicitFunctions",
            [](const TestClass& cls) { return cls._intMember; },
            [](TestClass& cls, int newValue) { cls._intMember = newValue; });

        accessors.Add(
            "IntMemberWithExplicitFunctions",
            [](const void* cls, IteratorRange<void*> dest, ImpliedTyping::TypeDesc destType) { 
                return ImpliedTyping::Cast(
                    dest, destType,
                    MakeOpaqueIteratorRange(((TestClass*)cls)->_intMember), ImpliedTyping::TypeOf<int>());
            },
            [](void* cls, IteratorRange<const void*> src, ImpliedTyping::TypeDesc srcType) { 
                return ImpliedTyping::Cast(
                    MakeOpaqueIteratorRange(((TestClass*)cls)->_intMember), ImpliedTyping::TypeOf<int>(),
                    src, srcType);
            });

        accessors.Add("IntPtrToMember", &TestClass::_intMember);

        SECTION("IntegerAccessors") {

            TestClass ex{};
            ex._intMember = 38;
            REQUIRE( accessors.Get<int>(ex, "IntMemberWithImplicitFunctions").value() == 38 );
            REQUIRE( accessors.Get<int>(ex, "IntMemberWithExplicitFunctions").value() == 38 );
            REQUIRE( accessors.Get<int>(ex, "IntPtrToMember").value() == 38 );

            ex._intMember = 96;
            REQUIRE( accessors.Get<float>(ex, "IntMemberWithImplicitFunctions").value() == 96_a );
            REQUIRE( accessors.Get<float>(ex, "IntMemberWithExplicitFunctions").value() == 96_a );
            REQUIRE( accessors.Get<float>(ex, "IntPtrToMember").value() == 96_a );

            accessors.Set(ex, "IntMemberWithImplicitFunctions", 18 );
            REQUIRE( ex._intMember == 18 );

            accessors.Set(ex, "IntMemberWithExplicitFunctions", 19.f );
            REQUIRE( ex._intMember == 19 );

            accessors.Set(ex, "IntPtrToMember", Float4{20, 3, 2, 5} );
            REQUIRE( ex._intMember == 20 );

        }

        accessors.Add(
            "VectorMemberWithImplicitFunctions",
            [](const TestClass& cls) { return cls._vectorMember; },
            [](TestClass& cls, const Float4& newValue) { cls._vectorMember = newValue; });

        accessors.Add(
            "VectorMemberWithExplicitFunctions",
            [](const void* cls, IteratorRange<void*> dest, ImpliedTyping::TypeDesc destType) { 
                return ImpliedTyping::Cast(
                    dest, destType,
                    MakeOpaqueIteratorRange(((TestClass*)cls)->_vectorMember), ImpliedTyping::TypeOf<Float4>());
            },
            [](void* cls, IteratorRange<const void*> src, ImpliedTyping::TypeDesc srcType) { 
                return ImpliedTyping::Cast(
                    MakeOpaqueIteratorRange(((TestClass*)cls)->_vectorMember), ImpliedTyping::TypeOf<Float4>(),
                    src, srcType);
            });

        accessors.Add("VectorPtrToMember", &TestClass::_vectorMember);

        SECTION("FloatAccessors") {

            TestClass ex{};
            ex._vectorMember = Float4{23, 34, 45, 46};
            REQUIRE( accessors.Get<Float4>(ex, "VectorMemberWithImplicitFunctions").value() == Float4{23, 34, 45, 46} );
            REQUIRE( accessors.Get<Float4>(ex, "VectorMemberWithExplicitFunctions").value() == Float4{23, 34, 45, 46} );
            REQUIRE( accessors.Get<Float4>(ex, "VectorPtrToMember").value() == Float4{23, 34, 45, 46} );

            ex._vectorMember = Float4{-100, 2, 3, 4};
            REQUIRE( accessors.Get<int>(ex, "VectorMemberWithImplicitFunctions").value() == -100 );
            REQUIRE( accessors.Get<int>(ex, "VectorMemberWithExplicitFunctions").value() == -100 );
            REQUIRE( accessors.Get<int>(ex, "VectorPtrToMember").value() == -100 );

            accessors.Set(ex, "VectorMemberWithImplicitFunctions", Float4{28, 29, 30, 31} );
            REQUIRE( ex._vectorMember == Float4{28, 29, 30, 31} );

            accessors.Set(ex, "VectorMemberWithExplicitFunctions", 62.f );
            REQUIRE( ex._vectorMember == Float4{62, 0, 0, 1} );

            accessors.Set(ex, "VectorPtrToMember", 67 );
            REQUIRE( ex._vectorMember == Float4{67, 0, 0, 1} );

        }

        SECTION("StringInterface") {

            TestClass ex{};
            REQUIRE( accessors.GetAsString(ex, "VectorPtrToMember").value() == std::string("{1, 2, 3, 4}v") );
            REQUIRE( accessors.GetAsString(ex, "VectorPtrToMember", true).value() == std::string("{1f, 2f, 3f, 4f}v") );

            REQUIRE( accessors.SetFromString(ex, "VectorPtrToMember", "{34, 56, 23, 1}") == true );
            REQUIRE( ex._vectorMember == Float4{34, 56, 23, 1} );

        }

        SECTION("FailureCases") {

            TestClass ex{};
            REQUIRE( accessors.Get<int>(ex, "missing").has_value() == false );
            REQUIRE( accessors.GetAsString(ex, "missing").has_value() == false );

            int temp;
            REQUIRE( accessors.Get(MakeOpaqueIteratorRange(temp), ImpliedTyping::TypeOf<int>(), &ex, "missing") == false );

            REQUIRE( accessors.Set(ex, "missing", 5) == false );
            REQUIRE( accessors.SetFromString(ex, "missing", "5") == false );
            REQUIRE( accessors.Set(&ex, "missing", MakeOpaqueIteratorRange(temp), ImpliedTyping::TypeOf<int>()) == false );

        }

        accessors.Add(
            "GetWithNoSet",
            [](const void* cls, IteratorRange<void*> dest, ImpliedTyping::TypeDesc destType) { 
                return ImpliedTyping::Cast(
                    dest, destType,
                    MakeOpaqueIteratorRange(((TestClass*)cls)->_vectorMember), ImpliedTyping::TypeOf<Float4>());
            },
            nullptr);

        accessors.Add(
            "SetWithNoGet",
            nullptr,
            [](void* cls, IteratorRange<const void*> src, ImpliedTyping::TypeDesc srcType) { 
                return ImpliedTyping::Cast(
                    MakeOpaqueIteratorRange(((TestClass*)cls)->_vectorMember), ImpliedTyping::TypeOf<Float4>(),
                    src, srcType);
            });

    }

    TEST_CASE( "ClassAccessors-AccessorSerialize", "[utility]" )
    {
        ClassAccessors accessors(TypeHashCode<TestClass>);
        accessors.Add("IntMember", &TestClass::_intMember);
        accessors.Add("VectorMember", &TestClass::_vectorMember);

        SECTION("Serialize") {
            std::stringstream stream;
            {
                Formatters::TextOutputFormatter formatter(stream);

                TestClass ex{};
                ex._intMember = 30;
                ex._vectorMember = {5, 7, 6, 5};
                AccessorSerialize(formatter, &ex, accessors);
            }

            auto serializedString = stream.str();
            REQUIRE(serializedString == "~~!Format=2; Tab=4\r\nIntMember=30; VectorMember={5, 7, 6, 5}v");
        }

        SECTION("Deserialize") {
            std::string input{"IntMember = -342i; VectorMember = {30, 31, 32, 33}"};
            Formatters::TextInputFormatter<char> formatter(MakeStringSection(input));
            TestClass ex{};
            AccessorDeserialize(formatter, &ex, accessors);

            REQUIRE(ex._intMember == -342);
            REQUIRE(ex._vectorMember == Float4{30, 31, 32, 33});
        }
    }

}
