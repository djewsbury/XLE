// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "../../Math/Vector.h"
#include "../../Math/MathSerialization.h"
#include "../../Utility/Streams/StreamFormatter.h"
#include "../../Utility/Streams/StreamDOM.h"
#include "../../Utility/Streams/SerializationUtils.h"
#include "../../Utility/Streams/XmlStreamFormatter.h"
#include "../../Utility/Streams/FormatterUtils.h"
#include "../../Utility/Conversion.h"
#include "../../Utility/StringFormat.h"
#include "../../Utility/ImpliedTyping.h"
#include "../../Core/Prefix.h"
#include <string>
#include "catch2/catch_test_macros.hpp"
#include "catch2/catch_approx.hpp"
#if COMPILER_ACTIVE == COMPILER_TYPE_CLANG
    #include <x86intrin.h>
#endif

/*#include "../../ConsoleRig/GlobalServices.h"
#include "../../ConsoleRig/AttachablePtr.h"
#include "../../Assets/IFileSystem.h"*/

using namespace Catch::literals;

namespace UnitTests
{
    struct TestClass
    {
        int _c1 = 1;
        int _c2 = 5;
        UInt2 _c3 = {2, 3};
    };

    void DeserializationOperator(const StreamDOMElement<InputStreamFormatter<utf8>>& str, TestClass& cls)
    {
        cls._c1 = str.Attribute("c1").As<int>().value();
        cls._c2 = str.Attribute("c2", 600);
        cls._c3 = str.Attribute("c3").As<UInt2>().value();
    }

    TEST_CASE( "StreamFormatter-BasicDeserialization", "[utility]" )
    {
        const std::basic_string<utf8> testString = (const utf8*)R"(c1 = 10; c2 = 50; c3 = {20, 30})";

        SECTION("Using StreamDOMElement::As() to invoke DeserializationOperator")
        {
            InputStreamFormatter<utf8> formatter(MakeStringSection(testString));
            StreamDOM<InputStreamFormatter<utf8>> doc(formatter);
            auto testClass = doc.RootElement().As<TestClass>();
            REQUIRE( testClass._c1 == 10 );
            REQUIRE( testClass._c2 == 50 );
            REQUIRE( testClass._c3 == UInt2(20, 30) );
        }

        SECTION("Using operator>> to invoke DeserializationOperator")
        {
            InputStreamFormatter<utf8> formatter(MakeStringSection(testString));
            StreamDOM<InputStreamFormatter<utf8>> doc(formatter);
            TestClass testClass;
            doc.RootElement() >> testClass;
            REQUIRE( testClass._c1 == 10 );
            REQUIRE( testClass._c2 == 50 );
            REQUIRE( testClass._c3 == UInt2(20, 30) );
        }
    }

    TEST_CASE( "StreamFormatter-InputSyntaxTests", "[utility]" )
    {
        SECTION("NestedElements") {
            const std::basic_string<utf8> testStringWithNestedElements = R"(~~!Format=2; Tab=4
Value0 = value0
Value1 = value1
Element =~
    NestedElement =~
        something = value
    AnotherNestedElement =~
        anotherThing = value
    AnotherValue = something
Value2 = value2)";
            InputStreamFormatter<utf8> formatter(MakeStringSection(testStringWithNestedElements));
            REQUIRE(RequireKeyedItem(formatter).AsString() == "Value0");
            REQUIRE(RequireStringValue(formatter).AsString() == "value0");
            REQUIRE(RequireKeyedItem(formatter).AsString() == "Value1");
            REQUIRE(RequireStringValue(formatter).AsString() == "value1");

            REQUIRE(RequireKeyedItem(formatter).AsString() == "Element");
            RequireBeginElement(formatter);

            REQUIRE(RequireKeyedItem(formatter).AsString() == "NestedElement");
            RequireBeginElement(formatter);
            REQUIRE(RequireKeyedItem(formatter).AsString() == "something");
            REQUIRE(RequireStringValue(formatter).AsString() == "value");
            RequireEndElement(formatter);

            REQUIRE(RequireKeyedItem(formatter).AsString() == "AnotherNestedElement");
            RequireBeginElement(formatter);
            REQUIRE(RequireKeyedItem(formatter).AsString() == "anotherThing");
            REQUIRE(RequireStringValue(formatter).AsString() == "value");
            RequireEndElement(formatter);

            REQUIRE(RequireKeyedItem(formatter).AsString() == "AnotherValue");
            REQUIRE(RequireStringValue(formatter).AsString() == "something");
            RequireEndElement(formatter);

            REQUIRE(RequireKeyedItem(formatter).AsString() == "Value2");
            REQUIRE(RequireStringValue(formatter).AsString() == "value2");
        }

        SECTION("Sequences") {
            const std::basic_string<utf8> testStringWithSequences = R"(~~!Format=2; Tab=4
=~
    Value0 = value
    Value1 = value2
=       ~           
    SomeValue=one
    SomeValue=two
=~           ~~ This is comment at the start of an element
    =!%one
    =&*two
    =(three)
=~
    =~
        =~
            =~
)";
            InputStreamFormatter<utf8> formatter(MakeStringSection(testStringWithSequences));
            RequireBeginElement(formatter);
            REQUIRE(RequireKeyedItem(formatter).AsString() == "Value0");
            REQUIRE(RequireStringValue(formatter).AsString() == "value");
            REQUIRE(RequireKeyedItem(formatter).AsString() == "Value1");
            REQUIRE(RequireStringValue(formatter).AsString() == "value2");
            RequireEndElement(formatter);

            RequireBeginElement(formatter);
            REQUIRE(RequireKeyedItem(formatter).AsString() == "SomeValue");
            REQUIRE(RequireStringValue(formatter).AsString() == "one");
            REQUIRE(RequireKeyedItem(formatter).AsString() == "SomeValue");
            REQUIRE(RequireStringValue(formatter).AsString() == "two");
            RequireEndElement(formatter);

            RequireBeginElement(formatter);
            REQUIRE(RequireStringValue(formatter).AsString() == "!%one");
            REQUIRE(RequireStringValue(formatter).AsString() == "&*two");
            REQUIRE(RequireStringValue(formatter).AsString() == "(three)");
            RequireEndElement(formatter);

            RequireBeginElement(formatter);
            RequireBeginElement(formatter);
            RequireBeginElement(formatter);
            RequireBeginElement(formatter);
            RequireEndElement(formatter);
            RequireEndElement(formatter);
            RequireEndElement(formatter);
            RequireEndElement(formatter);
        }

        SECTION("CompressedElements") {
            const std::basic_string<utf8> testStringWithCompressedElements = R"(~~!Format=2; Tab=4
Value0 = value; Value1 = value2
=one; =two;

~~ Following is a sight detail in the syntax. In the following element, the second "=~" that appears
~~ begins a nested element within the first
~~ This is also the case if all of the elements begin on the same line. The syntax looks a little
~~ ambigous; but we interpret it as nesting

=~; SomeValue=one;
    SomeValue=two; =~

~~ Following on from above; are examples where elements still nest within each other

=~; mappedEle=~; =~;
=~; value=1; =~; value=2; =~; value=3
)";
            InputStreamFormatter<utf8> formatter(MakeStringSection(testStringWithCompressedElements));
            REQUIRE(RequireKeyedItem(formatter).AsString() == "Value0");
            REQUIRE(RequireStringValue(formatter).AsString() == "value");
            REQUIRE(RequireKeyedItem(formatter).AsString() == "Value1");
            REQUIRE(RequireStringValue(formatter).AsString() == "value2");

            REQUIRE(RequireStringValue(formatter).AsString() == "one");
            REQUIRE(RequireStringValue(formatter).AsString() == "two");

            RequireBeginElement(formatter);
            REQUIRE(RequireKeyedItem(formatter).AsString() == "SomeValue");
            REQUIRE(RequireStringValue(formatter).AsString() == "one");
            REQUIRE(RequireKeyedItem(formatter).AsString() == "SomeValue");
            REQUIRE(RequireStringValue(formatter).AsString() == "two");
            RequireBeginElement(formatter);
            RequireEndElement(formatter);
            RequireEndElement(formatter);

            RequireBeginElement(formatter);
            REQUIRE(RequireKeyedItem(formatter).AsString() == "mappedEle");
            RequireBeginElement(formatter);
            RequireBeginElement(formatter);
            RequireEndElement(formatter);
            RequireEndElement(formatter);
            RequireEndElement(formatter);

            RequireBeginElement(formatter);
            REQUIRE(RequireKeyedItem(formatter).AsString() == "value");
            REQUIRE(RequireStringValue(formatter).AsString() == "1");
            RequireBeginElement(formatter);
            REQUIRE(RequireKeyedItem(formatter).AsString() == "value");
            REQUIRE(RequireStringValue(formatter).AsString() == "2");
            RequireBeginElement(formatter);
            REQUIRE(RequireKeyedItem(formatter).AsString() == "value");
            REQUIRE(RequireStringValue(formatter).AsString() == "3");
            RequireEndElement(formatter);
            RequireEndElement(formatter);
            RequireEndElement(formatter);
        }
    }

    TEST_CASE( "StreamFormatter-XMLInputSyntaxTests", "[utility]" )
    {

        const std::basic_string<utf8> testStringXML = R"(

    <element v0="stringValue" v1="5.">
        <nestedElement v0="attributeValue0" v1="attributeValue1" v2="attributeValue2"/>
        Character data
    </element>

        )";

        XmlInputStreamFormatter<utf8> formatter(MakeStringSection(testStringXML));
        formatter._allowCharacterData = true;
        REQUIRE(RequireKeyedItem(formatter).AsString() == "element");
        RequireBeginElement(formatter);
        REQUIRE(RequireKeyedItem(formatter).AsString() == "v0");
        REQUIRE(RequireStringValue(formatter).AsString() == "stringValue");
        REQUIRE(RequireKeyedItem(formatter).AsString() == "v1");
        REQUIRE(RequireStringValue(formatter).AsString() == "5.");

            REQUIRE(RequireKeyedItem(formatter).AsString() == "nestedElement");
            RequireBeginElement(formatter);
            REQUIRE(RequireKeyedItem(formatter).AsString() == "v0");
            REQUIRE(RequireStringValue(formatter).AsString() == "attributeValue0");
            REQUIRE(RequireKeyedItem(formatter).AsString() == "v1");
            REQUIRE(RequireStringValue(formatter).AsString() == "attributeValue1");
            REQUIRE(RequireKeyedItem(formatter).AsString() == "v2");
            REQUIRE(RequireStringValue(formatter).AsString() == "attributeValue2");
            RequireEndElement(formatter);

        StringSection<char> cdata;
        REQUIRE(formatter.TryCharacterData(cdata));
        RequireEndElement(formatter);

        /*{
            auto globalServices = ConsoleRig::MakeAttachablePtr<ConsoleRig::GlobalServices>();
            auto blob = ::Assets::MainFileSystem::TryLoadFileAsBlob("/home/davidj/code/XLE/Working/Game/Model/Galleon/Galleon.DAE");
            MemoryMappedInputStream inputStream  { AsPointer(blob->begin()), AsPointer(blob->end()) };
            XmlInputStreamFormatter<utf8> formatter ( inputStream );
            StreamDOM<XmlInputStreamFormatter<utf8>> dom ( formatter );
            REQUIRE(dom.RootElement().Element("COLLADA").Attribute("version").Value().AsString() == "1.4.1");
            REQUIRE(dom.RootElement().Element("COLLADA").Element("library_effects").Element("effect").Attribute("id").Value().AsString() == "galleon_01");
            (void)dom;
        }*/
    }

    const std::basic_string<utf8> testString = (const utf8*)R"~~(~~!Format=2; Tab=4

DiffuseDims={512u, 512u}v; NormalDims={512u, 512u}v; ParamDims={512u, 512u}v

StrataMaterial=~
    Strata=~
        One=~
            Texture0=Tex0
            Texture1=Tex1
            Slopes=SlopesTex
            EndHeight = 0.4
            Mapping = {4,3,2}
        Two=~
            Texture0=aaTex0
            Texture1=12Tex1
            Slopes=XXXSlopesTexXXX
            EndHeight = 3
            Mapping = {7,9,10,12,14,16,17}
    MaterialId=2

GradFlagMaterial=~
    MaterialId=0
    Texture0=grassTextureNo9227
    Texture1=tr_canyon_rock_700b_800b
    Texture2=tr_canyon_rock3d_708a
    Texture3=tr_canyon_rock3d_602b
    Texture4=grassTextureNo9227; Mapping={1.8f, 1f, 1f, 1f, 1f}

GradFlagMaterial=~
    MaterialId=1
    Texture0=ProcTexture
    Texture1=stonesTextureNo8648
    Texture2=tr_canyon_rock3d_409a
    Texture3=tr_canyon_rock3d_409a
    Texture4=gravelTextureNo7899; Mapping={1.8f, 1f, 1f, 1f, 1f}

ProcTextureSetting=~; Name=ProcTexture; 
    Texture0=grassTextureNo7109
    Texture1=grassTextureNo6354; HGrid=5f; Gain=0.5f
)~~";

    const std::string testString2 = R"--(~~!Format=2; Tab=4

EnvSettings=~
	Name=environment
	
	ToneMapSettings=~; BloomDesaturationFactor=0.6f; BloomRampingFactor=0.8f; SceneKey=0.23f
		BloomBlurStdDev=1.32f; Flags=3i; BloomBrightness=20.8f; LuminanceMax=3f; WhitePoint=8f
		BloomThreshold=10f; LuminanceMin=0.06f; BloomScale=-1405359i
	
	AmbientSettings=~; AmbientLight=-12080934i; AmbientBrightness=0.1f; SkyReflectionBlurriness=2f
		SkyTexture=xleres/DefaultResources/sky/desertsky.dds; SkyReflectionScale=8f
		SkyBrightness=0.68f
	
	ShadowFrustumSettings=~; FrustumCount=5i; ShadowRasterDepthBias=400i; FrustumSizeFactor=4f
		ShadowSlopeScaledBias=1f; Flags=1i; ShadowDepthBiasClamp=0f; MaxBlurSearch=64f
		MinBlurSearch=2f; MaxDistanceFromCamera=500f; WorldSpaceResolveBias=0f; FocusDistance=3f
		BlurAngleDegrees=0.25f; Name=shadows; TextureSize=2048i
	
	DirectionalLight=~; DiffuseModel=1i; DiffuseBrightness=3.5f; Diffuse=-1072241i
		Specular=-1647966i; Flags=1i; SpecularBrightness=19f; DiffuseWideningMin=0.2f
		Transform={1f, 0f, 0f, 0f, 0f, 1f, 0f, 0f, 0f, 0f, 1f, 0f, -5.05525f, 32.7171f, 5.73295f, 1f}
		ShadowResolveModel=0i; SpecularNonMetalBrightness=15f; DiffuseWideningMax=0.9f
		Name=DirLight; ShadowFrustumSettings=shadows; Visible=0u
	
	DirectionalLight=~; DiffuseModel=0i; DiffuseBrightness=0.75f; Diffuse=-957580i; Specular=-1i
		Flags=0i; SpecularBrightness=5f; DiffuseWideningMin=0.5f
		Transform={1f, 0f, 0f, 0f, 0f, 1f, 0f, 0f, 0f, 0f, 1f, 0f, 3.59605f, 2.4588f, -2.49624f, 1f}
		ShadowResolveModel=0i; SpecularNonMetalBrightness=5f; DiffuseWideningMax=2.5f
		Name=SecondaryLight; ShadowFrustumSettings=shadows; Visible=0u
	
	DirectionalLight=~; DiffuseModel=0i; DiffuseBrightness=0.2f; Diffuse=-5247249i; Specular=-1i
		Flags=0i; SpecularBrightness=3.5f; DiffuseWideningMin=0.5f
		Transform={1f, 0f, 0f, 0f, 0f, 1f, 0f, 0f, 0f, 0f, 1f, 0f, -9.9108f, -8.22611f, 2.5275f, 1f}
		ShadowResolveModel=0i; SpecularNonMetalBrightness=3.5f; DiffuseWideningMax=2.5f
		Name=TertiaryLight; ShadowFrustumSettings=shadows; Visible=0u
)--";

    struct ExampleSerializableObject
    {
        struct StrataMaterial
        {
            struct Strata
            {
                std::string _texture[3];
                float _mappingConstant[3] = { 0.f, 0.f, 0.f };
                float _endHeight = 0.f;
            };
            std::vector<Strata> _strata;
            unsigned _id = 0;
        };

        struct GradFlagMaterial
        {
            std::string _texture[5];
            float _mappingConstant[5] = { 0.f, 0.f, 0.f, 0.f, 0.f };
            unsigned _id = 0;
        };

        struct ProcTextureSetting
        {
            std::string _name;
            std::string _texture[2];
            float _hgrid = 100.f, _gain = .5f;
        };

        std::vector<StrataMaterial>     _strataMaterials;
        std::vector<GradFlagMaterial>   _gradFlagMaterials;
        std::vector<ProcTextureSetting> _procTextures;

        UInt2   _diffuseDims = UInt2(32, 32);
        UInt2   _normalDims = UInt2(32, 32);
        UInt2   _paramDims = UInt2(32, 32);
    };

////////////////////////////////////////////////////////////////////////////////////////////////////////////////
        //   S T R E A M   D O M   //
////////////////////////////////////////////////////////////////////////////////////////////////////////////////

    static void DeserializationOperator(
        const Utility::StreamDOMElement<InputStreamFormatter<utf8>>& src,
        ExampleSerializableObject::StrataMaterial& mat)
    {
        static const utf8* TextureNames[] = { "Texture0", "Texture1", "Slopes" };

        mat._id = src.Attribute("MaterialId", 0u);
        for (const auto& c:src.Element("Strata").children()) {
            ExampleSerializableObject::StrataMaterial::Strata newStrata;
            for (unsigned t=0; t<dimof(TextureNames); ++t)
                newStrata._texture[t] = c.Attribute(TextureNames[t]).Value().AsString();

            newStrata._endHeight = c.Attribute("EndHeight", 0.f);
            auto mappingConst = c.Attribute("Mapping", Float4(1.f, 1.f, 1.f, 1.f));
            newStrata._mappingConstant[0] = mappingConst[0];
            newStrata._mappingConstant[1] = mappingConst[1];
            newStrata._mappingConstant[2] = mappingConst[2];

            mat._strata.push_back(newStrata);
        }
    }

    static void DeserializationOperator(
        const Utility::StreamDOMElement<InputStreamFormatter<utf8>>& src,
        ExampleSerializableObject::GradFlagMaterial& mat)
    {
        mat._id = src.Attribute("MaterialId", 0);
            
        mat._texture[0] = src.Attribute("Texture0").Value().AsString();
        mat._texture[1] = src.Attribute("Texture1").Value().AsString();
        mat._texture[2] = src.Attribute("Texture2").Value().AsString();
        mat._texture[3] = src.Attribute("Texture3").Value().AsString();
        mat._texture[4] = src.Attribute("Texture4").Value().AsString();

        // Manually parsing and converting into the array type we want
        // This is sort of a wordy approach to this; normally for vector & matrix types
        // we shouldn't need so many steps
        uint8_t buffer[512];
        auto mappingAttr = src.Attribute("Mapping").Value();
        auto parsedType = ImpliedTyping::ParseFullMatch(
            mappingAttr,
            MakeIteratorRange(buffer));
        ImpliedTyping::Cast(
            MakeIteratorRange(mat._mappingConstant), 
            ImpliedTyping::TypeDesc{ImpliedTyping::TypeCat::Float, dimof(mat._mappingConstant)},
            MakeIteratorRange(buffer), parsedType);
    }

    static void DeserializationOperator(
        const Utility::StreamDOMElement<InputStreamFormatter<utf8>>& src,
        ExampleSerializableObject::ProcTextureSetting& mat)
    {
        mat._name = src.Attribute("Name").Value().AsString();
        mat._texture[0] = src.Attribute("Texture0").Value().AsString();
        mat._texture[1] = src.Attribute("Texture1").Value().AsString();
        mat._hgrid = src.Attribute("HGrid", mat._hgrid);
        mat._gain = src.Attribute("Gain", mat._gain);
    }

    static void DeserializationOperator(
        const Utility::StreamDOMElement<InputStreamFormatter<utf8>>& doc,
        ExampleSerializableObject& result)
    {
        for (const auto& matCfg:doc.children()) {
            if (XlEqString(matCfg.Name(), "StrataMaterial")) {
                result._strataMaterials.push_back(matCfg.As<ExampleSerializableObject::StrataMaterial>());
            } else if (XlEqString(matCfg.Name(), "GradFlagMaterial")) {
                result._gradFlagMaterials.push_back(matCfg.As<ExampleSerializableObject::GradFlagMaterial>());
            } else if (XlEqString(matCfg.Name(), "ProcTextureSetting")) {
                result._procTextures.push_back(matCfg.As<ExampleSerializableObject::ProcTextureSetting>());
            }
        }

        result._diffuseDims = doc.Attribute("DiffuseDims", result._diffuseDims);
        result._normalDims = doc.Attribute("NormalDims", result._normalDims);
        result._paramDims = doc.Attribute("ParamDims", result._paramDims);
    }

////////////////////////////////////////////////////////////////////////////////////////////////////////////////
        //   F O R M A T T E R   P A R S I N G   //
////////////////////////////////////////////////////////////////////////////////////////////////////////////////

    static void DeserializationOperator(
        InputStreamFormatter<utf8>& formatter,
        ExampleSerializableObject::StrataMaterial& result)
    {
        static const utf8* TextureNames[] = { "Texture0", "Texture1", "Slopes" };
        
        while (formatter.PeekNext() == FormatterBlob::KeyedItem) {
            auto name = RequireKeyedItem(formatter);

            if (XlEqString(name, "MaterialId")) {
                result._id = Conversion::Convert<decltype(result._id)>(RequireStringValue(formatter));
            } else if (XlEqString(name, "Strata")) {
                RequireBeginElement(formatter);

                while (formatter.PeekNext() != FormatterBlob::EndElement) {
                    auto ignoredName = RequireKeyedItem(formatter);
                    RequireBeginElement(formatter);

                    ExampleSerializableObject::StrataMaterial::Strata newStrata;
                    while (formatter.PeekNext() == FormatterBlob::KeyedItem) {
                        auto name = RequireKeyedItem(formatter);
                        auto value = RequireStringValue(formatter);

                        if (XlEqString(name, "EndHeight")) {
                            newStrata._endHeight = Conversion::Convert<decltype(newStrata._endHeight)>(value);
                        } else if (XlEqString(name, "Mapping")) {
                            auto mappingConst = ImpliedTyping::ConvertFullMatch<Float4>(value).value();
                            newStrata._mappingConstant[0] = mappingConst[0];
                            newStrata._mappingConstant[1] = mappingConst[1];
                            newStrata._mappingConstant[2] = mappingConst[2];
                        } else {
                            unsigned t=0;
                            for (; t<dimof(TextureNames); ++t)
                                if (XlEqString(name, TextureNames[t])) {
                                    newStrata._texture[t] = value.AsString();
                                    break;
                                }
                            if (t == dimof(TextureNames))
                                Throw(FormatException("Unknown attribute encountered", formatter.GetLocation()));
                        }
                    }
                    RequireEndElement(formatter);
                    result._strata.push_back(newStrata);
                }

                RequireEndElement(formatter);
            } else {
                Throw(FormatException("Unknown item encountered", formatter.GetLocation()));
            }
        }
    }
    
    static void DeserializationOperator(
        InputStreamFormatter<utf8>& formatter,
        ExampleSerializableObject::GradFlagMaterial& result)
    {
        while (formatter.PeekNext() == FormatterBlob::KeyedItem) {
            auto name = RequireKeyedItem(formatter);
            auto value = RequireStringValue(formatter);

            if (XlEqString(name, "MaterialId")) {
                result._id = ImpliedTyping::ConvertFullMatch<decltype(result._id)>(value).value();
            } else if (XlEqString(name, "Texture0")) {
                result._texture[0] = value.AsString();
            } else if (XlEqString(name, "Texture1")) {
                result._texture[1] = value.AsString();
            } else if (XlEqString(name, "Texture2")) {
                result._texture[2] = value.AsString();
            } else if (XlEqString(name, "Texture3")) {
                result._texture[3] = value.AsString();
            } else if (XlEqString(name, "Texture4")) {
                result._texture[4] = value.AsString();
            } else if (XlEqString(name, "Mapping")) {
                uint8_t buffer[512];
                auto parsedType = ImpliedTyping::ParseFullMatch(
                    value,
                    MakeIteratorRange(buffer));
                ImpliedTyping::Cast(
                    MakeIteratorRange(result._mappingConstant), 
                    ImpliedTyping::TypeDesc{ImpliedTyping::TypeCat::Float, dimof(result._mappingConstant)},
                    MakeIteratorRange(buffer), parsedType);
            } else
                Throw(FormatException("Unknown attribute encountered", formatter.GetLocation()));
        }

        auto next = formatter.PeekNext();
        if (next != FormatterBlob::EndElement && next != FormatterBlob::None)
            Throw(FormatException("Unexpected blob", formatter.GetLocation()));
    }
    
    static void DeserializationOperator(
        InputStreamFormatter<utf8>& formatter,
        ExampleSerializableObject::ProcTextureSetting& result)
    {
        while (formatter.PeekNext() == FormatterBlob::KeyedItem) {
            auto name = RequireKeyedItem(formatter);
            auto value = RequireStringValue(formatter);

            if (XlEqString(name, "Name")) {
                result._name = value.AsString();
            } else if (XlEqString(name, "Texture0")) {
                result._texture[0] = value.AsString();
            } else if (XlEqString(name, "Texture1")) {
                result._texture[1] = value.AsString();
            } else if (XlEqString(name, "HGrid")) {
                result._hgrid = ImpliedTyping::ConvertFullMatch<decltype(result._hgrid)>(value).value();
            } else if (XlEqString(name, "Gain")) {
                result._gain = ImpliedTyping::ConvertFullMatch<decltype(result._gain)>(value).value();
            } else
                Throw(FormatException("Unknown attribute encountered", formatter.GetLocation()));
        }

        auto next = formatter.PeekNext();
        if (next != FormatterBlob::EndElement && next != FormatterBlob::None)
            Throw(FormatException("Unexpected blob", formatter.GetLocation()));
    }
    
    static void DeserializationOperator(
        InputStreamFormatter<utf8>& formatter,
        ExampleSerializableObject& result)
    {
        while (formatter.PeekNext() == FormatterBlob::KeyedItem) {
            auto name = RequireKeyedItem(formatter);

            if (XlEqString(name, "DiffuseDims")) {
                result._diffuseDims = ImpliedTyping::ConvertFullMatch<UInt2>(RequireStringValue(formatter)).value();
            } else if (XlEqString(name, "NormalDims")) {
                result._normalDims = ImpliedTyping::ConvertFullMatch<UInt2>(RequireStringValue(formatter)).value();
            } else if (XlEqString(name, "ParamDims")) {
                result._paramDims = ImpliedTyping::ConvertFullMatch<UInt2>(RequireStringValue(formatter)).value();
            } else if (XlEqString(name, "StrataMaterial")) {
                RequireBeginElement(formatter);
                ExampleSerializableObject::StrataMaterial newMaterial;
                formatter >> newMaterial;
                result._strataMaterials.push_back(std::move(newMaterial));
                RequireEndElement(formatter);
            } else if (XlEqString(name, "GradFlagMaterial")) {
                RequireBeginElement(formatter);
                ExampleSerializableObject::GradFlagMaterial newMaterial;
                formatter >> newMaterial;
                result._gradFlagMaterials.push_back(std::move(newMaterial));
                RequireEndElement(formatter);
            } else if (XlEqString(name, "ProcTextureSetting")) {
                RequireBeginElement(formatter);
                ExampleSerializableObject::ProcTextureSetting newMaterial;
                formatter >> newMaterial;
                result._procTextures.push_back(std::move(newMaterial));
                RequireEndElement(formatter);
            } else
                Throw(FormatException("Unknown mapping encountered", formatter.GetLocation()));
        }

        auto next = formatter.PeekNext();
        if (next != FormatterBlob::EndElement && next != FormatterBlob::None)
            Throw(FormatException("Unexpected blob", formatter.GetLocation()));
    }

////////////////////////////////////////////////////////////////////////////////////////////////////////////////

    static no_inline void RunPerformanceTest1(
        const std::basic_string<utf8>& testString, unsigned iterationCount)
    {
        for (unsigned c=0; c<iterationCount; ++c) {
            InputStreamFormatter<utf8> formatter{MakeStringSection(testString)};
            ExampleSerializableObject matConfig;
            formatter >> matConfig;
            (void)matConfig;
        }
    }

    static no_inline void RunPerformanceTest2(
        const std::basic_string<utf8>& testString, unsigned iterationCount)
    {
        for (unsigned c=0; c<iterationCount; ++c) {
            InputStreamFormatter<utf8> formatter(MakeStringSection(testString));
            StreamDOM<InputStreamFormatter<utf8>> doc(formatter);
            ExampleSerializableObject matConfig;
            doc.RootElement() >> matConfig;
            (void)matConfig;
        }
    }

    TEST_CASE( "StreamFormatter-BasicDOMInterface", "[utility]" )
    {
        // Load an aribtrary file, and use the basic interface of the StreamDOM<> class 
        InputStreamFormatter<utf8> formatter(MakeStringSection(testString));
        StreamDOM<InputStreamFormatter<utf8>> doc(formatter);
        auto rootElement = doc.RootElement();

        SECTION("C++11 syntax element iteration") {
            unsigned visitedChildren = 0;
            for (auto i=rootElement.begin_children(); i!=rootElement.end_children(); ++i) {
                std::cout << i->Name() << std::endl;
                ++visitedChildren;
            }
            REQUIRE(visitedChildren == 4);
        }

        SECTION("for(:) syntax element iteration") {
            unsigned visitedChildren = 0;
            for (const auto& i:rootElement.children()) {
                std::cout << i.Name() << std::endl;
                ++visitedChildren;
            }
            REQUIRE(visitedChildren == 4);
        }

        SECTION("C++11 syntax attribute iteration") {
            unsigned visitedAttributes = 0;
            for (auto i=rootElement.begin_attributes(); i!=rootElement.end_attributes(); ++i) {
                std::cout << i->Name() << ":" << i->Value() << std::endl;
                ++visitedAttributes;
            }
            REQUIRE(visitedAttributes == 3);
        }

        SECTION("for(:) syntax attribute iteration") {
            unsigned visitedAttributes = 0;
            for (const auto& i:rootElement.attributes()) {
                std::cout << i.Name() << ":" << i.Value() << std::endl;
                ++visitedAttributes;
            }
            REQUIRE(visitedAttributes == 3);
        }
    }

    template<typename CharType>
        void InitStreamDOMForCharacterType()
    {
        auto converted = Conversion::Convert<std::basic_string<CharType>>(testString2);
        InputStreamFormatter<CharType> formatter(MakeStringSection(converted));
        StreamDOM<InputStreamFormatter<CharType>> doc(formatter);
    }

    TEST_CASE( "StreamFormatter-TestDOMParse", "[utility]" )
    {
        // Parse stream DOM from an example input using various different
        // character types
        InitStreamDOMForCharacterType<utf8>();
    }

    TEST_CASE( "StreamFormatter-ClassPropertiesPerformance", "[utility]" )
    {
        #if defined(_DEBUG)
            const unsigned iterationCount = 64 * 128;
        #else
            const unsigned iterationCount = 64 * 1024;
        #endif
        
        #if COMPILER_ACTIVE == COMPILER_TYPE_CLANG      // instruction count query not implemented for other compilers
            unsigned auxBegin = 0, auxMiddle = 0, auxEnd = 0;
            auto start = __builtin_ia32_rdtscp(&auxBegin);
            RunPerformanceTest1(testString, iterationCount);
            auto middle = __builtin_ia32_rdtscp(&auxMiddle);
            RunPerformanceTest2(testString, iterationCount);
            auto end = __builtin_ia32_rdtscp(&auxEnd);

            std::cout << "InputStreamFormatter based deserialization: " << (middle-start) / iterationCount << " cycles per iteration (" << (middle-start) / iterationCount / testString.size() << " cycles per character)." << std::endl;
            std::cout << "StreamDOM based deserialization: " << (end-middle) / iterationCount << " cycles per iteration (" << (end-middle) / iterationCount / testString.size() << " cycles per character)." << std::endl;
        #endif
    }
}
