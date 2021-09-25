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
        auto t0 = ImpliedTyping::ConvertFullMatch<Float4>("{.5f, 10, true}");
        REQUIRE(t0.has_value());
        REQUIRE(Equivalent(t0.value(), Float4{.5f, 10.f, 1.f, 1.f}, 0.001f));

        // parsing a scalar as a vector (note the W component defaults to 1)
        auto t1 = ImpliedTyping::ConvertFullMatch<Float4>("23");
        REQUIRE(t1.has_value());
        REQUIRE(Equivalent(t1.value(), Float4{23.f, 0.f, 0.f, 1.f}, 0.001f));

        // parsing some high precision values
        auto t2 = ImpliedTyping::ConvertFullMatch<Double3>("{1e5, 23e-3, 16}");
        REQUIRE(t2.has_value());
        REQUIRE(Equivalent(t2.value(), Double3{1e5, 23e-3, 16}, 1e-6));
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

