// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "../../Math/MathSerialization.h"
#include "../../Math/Vector.h"
#include "../../Math/Matrix.h"
#include "../../Utility/ImpliedTyping.h"
#include "../../Utility/ParameterBox.h"
#include "catch2/catch_test_macros.hpp"
#include "catch2/catch_approx.hpp"

using namespace Catch::literals;
namespace UnitTests
{
    TEST_CASE( "MathSerialization-TypeOfForMathTypes", "[math]" )
    {
        REQUIRE( ImpliedTyping::TypeOf<Float2>() == Utility::ImpliedTyping::TypeDesc{Utility::ImpliedTyping::TypeCat::Float, 2, Utility::ImpliedTyping::TypeHint::Vector} );
        REQUIRE( ImpliedTyping::TypeOf<Float3>() == Utility::ImpliedTyping::TypeDesc{Utility::ImpliedTyping::TypeCat::Float, 3, Utility::ImpliedTyping::TypeHint::Vector} );
        REQUIRE( ImpliedTyping::TypeOf<Float4>() == Utility::ImpliedTyping::TypeDesc{Utility::ImpliedTyping::TypeCat::Float, 4, Utility::ImpliedTyping::TypeHint::Vector} );

        REQUIRE( ImpliedTyping::TypeOf<Double2>() == Utility::ImpliedTyping::TypeDesc{Utility::ImpliedTyping::TypeCat::Double, 2, Utility::ImpliedTyping::TypeHint::Vector} );
        REQUIRE( ImpliedTyping::TypeOf<Double3>() == Utility::ImpliedTyping::TypeDesc{Utility::ImpliedTyping::TypeCat::Double, 3, Utility::ImpliedTyping::TypeHint::Vector} );
        REQUIRE( ImpliedTyping::TypeOf<Double4>() == Utility::ImpliedTyping::TypeDesc{Utility::ImpliedTyping::TypeCat::Double, 4, Utility::ImpliedTyping::TypeHint::Vector} );

        REQUIRE( ImpliedTyping::TypeOf<UInt2>() == Utility::ImpliedTyping::TypeDesc{Utility::ImpliedTyping::TypeCat::UInt32, 2, Utility::ImpliedTyping::TypeHint::Vector} );
        REQUIRE( ImpliedTyping::TypeOf<UInt3>() == Utility::ImpliedTyping::TypeDesc{Utility::ImpliedTyping::TypeCat::UInt32, 3, Utility::ImpliedTyping::TypeHint::Vector} );
        REQUIRE( ImpliedTyping::TypeOf<UInt4>() == Utility::ImpliedTyping::TypeDesc{Utility::ImpliedTyping::TypeCat::UInt32, 4, Utility::ImpliedTyping::TypeHint::Vector} );

        REQUIRE( ImpliedTyping::TypeOf<Int2>() == Utility::ImpliedTyping::TypeDesc{Utility::ImpliedTyping::TypeCat::Int32, 2, Utility::ImpliedTyping::TypeHint::Vector} );
        REQUIRE( ImpliedTyping::TypeOf<Int3>() == Utility::ImpliedTyping::TypeDesc{Utility::ImpliedTyping::TypeCat::Int32, 3, Utility::ImpliedTyping::TypeHint::Vector} );
        REQUIRE( ImpliedTyping::TypeOf<Int4>() == Utility::ImpliedTyping::TypeDesc{Utility::ImpliedTyping::TypeCat::Int32, 4, Utility::ImpliedTyping::TypeHint::Vector} );
    }
        
    TEST_CASE( "MathSerialization-StringToValues", "[math]" )
    {
        // parsing a vector with variable elements that require conversion
        auto t0 = ImpliedTyping::ConvertFullMatch<Float4>("{.5f, 10, true, false}");
        REQUIRE(t0.has_value());
        REQUIRE(Equivalent(t0.value(), Float4{.5f, 10.f, 1.f, 0.f}, 0.001f));

        auto t0a = ImpliedTyping::ConvertFullMatch<Float4>("{-50i, -.3, .3f, -0x500}");
        REQUIRE(t0a.has_value());
        REQUIRE(Equivalent(t0a.value(), Float4{-50, -.3f, .3f, -float(0x500)}, 0.001f));

        auto t0b = ImpliedTyping::ConvertFullMatch<Vector4T<unsigned>>("{6.25f, 10, true, false}");
        REQUIRE(t0b.has_value());
        REQUIRE(t0b.value() == Vector4T<unsigned>{6, 10, 1, 0});

        auto t0c = ImpliedTyping::ConvertFullMatch<Vector4T<signed>>("{-50i, -2.3, .3f, -0x500}");
        REQUIRE(t0c.has_value());
        REQUIRE(t0c.value() == Vector4T<signed>{-50, -2, 0, -0x500});

        auto t0d = ImpliedTyping::ConvertFullMatch<Vector4T<float>>("{+0x1000, -0x300, +0x700, -0x200}");
        REQUIRE(t0d.has_value());
        REQUIRE(Equivalent(t0d.value(), Vector4T<float>{float(0x1000), -float(0x300), float(0x700), -float(0x200)}, 0.001f));

        // parsing some high precision values
        auto t2 = ImpliedTyping::ConvertFullMatch<Double3>("{1e5, 23e-3, 16}");
        REQUIRE(t2.has_value());
        REQUIRE(Equivalent(t2.value(), Double3{1e5, 23e-3, 16}, 1e-6));

        // poorly formed string cases
        REQUIRE(!ImpliedTyping::ConvertFullMatch<int>("0x").has_value());
        REQUIRE(!ImpliedTyping::ConvertFullMatch<int>("0x0x500").has_value());
        REQUIRE(!ImpliedTyping::ConvertFullMatch<int>("00x500").has_value());
        REQUIRE(!ImpliedTyping::ConvertFullMatch<int>("x500").has_value());
        REQUIRE(!ImpliedTyping::ConvertFullMatch<unsigned>("0x").has_value());
        REQUIRE(!ImpliedTyping::ConvertFullMatch<unsigned>("0x0x500").has_value());
        REQUIRE(!ImpliedTyping::ConvertFullMatch<unsigned>("00x500").has_value());
        REQUIRE(!ImpliedTyping::ConvertFullMatch<unsigned>("x500").has_value());
        REQUIRE(!ImpliedTyping::ConvertFullMatch<float>("0x").has_value());
        REQUIRE(!ImpliedTyping::ConvertFullMatch<float>("0x0x500").has_value());
        REQUIRE(!ImpliedTyping::ConvertFullMatch<float>("00x500").has_value());
        REQUIRE(!ImpliedTyping::ConvertFullMatch<float>("x500").has_value());
        REQUIRE(!ImpliedTyping::ConvertFullMatch<float>("{}").has_value());
        REQUIRE(!ImpliedTyping::ConvertFullMatch<Vector2T<float>>("{1.f}").has_value());
        REQUIRE(!ImpliedTyping::ConvertFullMatch<Vector4T<float>>("{1.f, 2.f, 3.f}").has_value());
        REQUIRE(!ImpliedTyping::ConvertFullMatch<Float4>("23").has_value()); // parsing a scalar as a vector
    }

    TEST_CASE( "MathSerialization-StoringInParameterBoxes", "[math]" )
    {
        // Storing and retrieving with some basic conversion from float to double
        ParameterBox box;
        box.SetParameter("Vector", Float3{1e5, 23e-3, 16});
        REQUIRE(Equivalent(box.GetParameter<Double3>("Vector").value(), Double3{1e5, 23e-3, 16}, 1e-6));

        // Store as string and retrieve as vector type
        box.SetParameter("Vector2", "{245, 723, .456}");
        REQUIRE(Equivalent(box.GetParameter<Float3>("Vector2").value(), Float3{245, 723, .456}, 1e-6f));

        // Store as vector and retrieve as string
        box.SetParameter("Vector3", Float3{546.45, 0.735, 273});
        REQUIRE(box.GetParameterAsString("Vector3").value() == "{546.45, 0.735, 273}v");
    }

}

