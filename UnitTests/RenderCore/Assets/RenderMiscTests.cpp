// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "../../../RenderCore/Assets/PredefinedCBLayout.h"
#include "../../../RenderCore/Types.h"
#include "../../../RenderCore/Format.h"
#include "../../../Tools/ToolsRig/VisualisationGeo.h"
#include "../../../Utility/ImpliedTyping.h"
#include "../../../Utility/MemoryUtils.h"
#include "../../../Math/Vector.h"
#include "../../../Math/MathSerialization.h"
#include "catch2/catch_test_macros.hpp"
#include "catch2/catch_approx.hpp"
#include <algorithm>
#include <random>

using namespace Catch::literals;
namespace UnitTests
{
	using NameAndType = RenderCore::Assets::PredefinedCBLayout::NameAndType;

	static const std::vector<NameAndType> s_poorlyOrdered {
		NameAndType { "f_a", ImpliedTyping::TypeOf<float>() },
		NameAndType { "f3_a", ImpliedTyping::TypeOf<Float3>() },
		NameAndType { "f3_b", ImpliedTyping::TypeOf<Float3>() },
		NameAndType { "f2_a", ImpliedTyping::TypeOf<Float2>() },
		NameAndType { "f4_a", ImpliedTyping::TypeOf<Float4>() },
		NameAndType { "f_b", ImpliedTyping::TypeOf<float>() },
		NameAndType { "f2_b", ImpliedTyping::TypeOf<Float2>() },		
		NameAndType { "f4_b", ImpliedTyping::TypeOf<Float4>() },		
		NameAndType { "f_c", ImpliedTyping::TypeOf<float>() },
		NameAndType { "f_d", ImpliedTyping::TypeOf<float>() }
	};

	static const std::vector<NameAndType> s_wellOrdered {
		NameAndType { "f4_a", ImpliedTyping::TypeOf<Float4>() },
		NameAndType { "f4_b", ImpliedTyping::TypeOf<Float4>() },

		NameAndType { "f3_a", ImpliedTyping::TypeOf<Float3>() },
		NameAndType { "f_a", ImpliedTyping::TypeOf<float>() },
		
		NameAndType { "f3_b", ImpliedTyping::TypeOf<Float3>() },
		NameAndType { "f_b", ImpliedTyping::TypeOf<float>() },
		
		NameAndType { "f2_a", ImpliedTyping::TypeOf<Float2>() },		
		NameAndType { "f2_b", ImpliedTyping::TypeOf<Float2>() },
		
		NameAndType { "f_c", ImpliedTyping::TypeOf<float>() },
		NameAndType { "f_d", ImpliedTyping::TypeOf<float>() }
	};

	TEST_CASE( "PredefinedCBLayout-OptimizeElementOrder", "[rendercore_assets]" )
	{
		auto shdLang = RenderCore::ShaderLanguage::HLSL;

		RenderCore::Assets::PredefinedCBLayout poorlyOrdered(
			MakeIteratorRange(s_poorlyOrdered));
		RenderCore::Assets::PredefinedCBLayout wellOrdered(
			MakeIteratorRange(s_wellOrdered));

		auto reorderedPoorEle = s_poorlyOrdered;
		RenderCore::Assets::PredefinedCBLayout::OptimizeElementOrder(MakeIteratorRange(reorderedPoorEle), shdLang);

		auto reorderedWellEle = s_wellOrdered;
		RenderCore::Assets::PredefinedCBLayout::OptimizeElementOrder(MakeIteratorRange(reorderedWellEle), shdLang);

		RenderCore::Assets::PredefinedCBLayout reorderedPoor(
			MakeIteratorRange(reorderedPoorEle));
		RenderCore::Assets::PredefinedCBLayout reorderedWell(
			MakeIteratorRange(reorderedWellEle));

		REQUIRE(wellOrdered.GetSize(shdLang) == reorderedWell.GetSize(shdLang));
		REQUIRE(wellOrdered.GetSize(shdLang) == reorderedPoor.GetSize(shdLang));
		REQUIRE(poorlyOrdered.GetSize(shdLang) > wellOrdered.GetSize(shdLang));
		REQUIRE(reorderedWell.CalculateHash() == reorderedPoor.CalculateHash());
	}

	static void TestHashingNormalizingAndScrambling(IteratorRange<const RenderCore::InputElementDesc*> inputAssembly)
	{
		using namespace RenderCore;
		auto hashingSeed = Hash64("hash-for-seed");
		auto expectedHash = HashInputAssembly(inputAssembly, hashingSeed);
		auto normalizedElements = NormalizeInputAssembly(inputAssembly);
		REQUIRE(expectedHash == HashInputAssembly(normalizedElements, hashingSeed));
		std::mt19937_64 rng(0);
		for (unsigned c=0; c<400; ++c) {
			auto scrambled = normalizedElements;
			std::shuffle(scrambled.begin(), scrambled.end(), rng);
			auto scrambledHash = HashInputAssembly(scrambled, hashingSeed);
			REQUIRE(scrambledHash == expectedHash);
		}
	}

	TEST_CASE( "HashInputAssembly", "[rendercore]" )
	{
		using namespace RenderCore;

		auto hashingSeed = Hash64("hash-for-seed");

		// "InputElementDesc" and "MiniInputElementDesc" should hash to the same value
		auto hashExpandedStyle = HashInputAssembly(MakeIteratorRange(ToolsRig::Vertex3D_InputLayout), hashingSeed);
		auto hashCompressedStyle = HashInputAssembly(MakeIteratorRange(ToolsRig::Vertex3D_MiniInputLayout), hashingSeed);
		REQUIRE(hashExpandedStyle == hashCompressedStyle);

		hashExpandedStyle = HashInputAssembly(MakeIteratorRange(ToolsRig::Vertex2D_InputLayout), hashingSeed);
		hashCompressedStyle = HashInputAssembly(MakeIteratorRange(ToolsRig::Vertex2D_MiniInputLayout), hashingSeed);
		REQUIRE(hashExpandedStyle == hashCompressedStyle);

		TestHashingNormalizingAndScrambling(ToolsRig::Vertex3D_InputLayout);
		TestHashingNormalizingAndScrambling(ToolsRig::Vertex2D_InputLayout);

		InputElementDesc complicatedIA[] = 
		{
			InputElementDesc { "POSITION", 0, Format::R8G8B8A8_UNORM, 0, 0 },
			InputElementDesc { "POSITION", 1, Format::R8G8B8A8_UNORM, 1, 16 },
			InputElementDesc { "TEXCOORD", 0, Format::R32_FLOAT, 1, ~0u },
			InputElementDesc { "TEXTANGENT", 0, Format::R8G8B8A8_UNORM, 1, 24 },
			InputElementDesc { "NORMAL", 0, Format::R8G8B8A8_UNORM, 0, 24 },
			InputElementDesc { "TEXCOORD", 3, Format::R8G8B8A8_UNORM, 0, ~0u }
		};
		TestHashingNormalizingAndScrambling(MakeIteratorRange(complicatedIA));
	}
}

