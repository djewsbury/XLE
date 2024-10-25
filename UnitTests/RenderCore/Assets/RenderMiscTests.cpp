// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "../../UnitTestHelper.h"
#include "../../EmbeddedRes.h"
#include "../../../RenderCore/Assets/PredefinedCBLayout.h"
#include "../../../RenderCore/Types.h"
#include "../../../RenderCore/Format.h"
#include "../../../RenderOverlays/Font.h"
#include "../../../Tools/ToolsRig/VisualisationGeo.h"
#include "../../../Assets/Marker.h"
#include "../../../Assets/MountingTree.h"
#include "../../../ConsoleRig/GlobalServices.h"
#include "../../../ConsoleRig/AttachablePtr.h"
#include "../../../Utility/ImpliedTyping.h"
#include "../../../Utility/MemoryUtils.h"
#include "../../../Math/Vector.h"
#include "../../../Math/MathSerialization.h"
#include "catch2/catch_test_macros.hpp"
#include "catch2/catch_approx.hpp"
#include <algorithm>
#include <random>

using namespace Catch::literals;
using namespace Utility::Literals;

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
		auto hashingSeed = "hash-for-seed"_h;
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

		auto hashingSeed = "hash-for-seed"_h;

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

	TEST_CASE( "StringEllipsis", "[renderoverlays]" )
	{
		// Test restricting string size by replacing parts with ellipses
		auto globalServices = ConsoleRig::MakeGlobalServices(GetStartupConfig());
		auto mnt0 = ::Assets::MainFileSystem::GetMountingTree()->Mount("xleres", UnitTests::CreateEmbeddedResFileSystem());

		auto futureFont = RenderOverlays::MakeFont("Petra", 16);
		futureFont->StallWhilePending();
		auto font = futureFont->Actualize();

		const char longFileName[] = "c:/abcdefghijklmnopqrstuvwxyz/ABCDEFGHIJKLMNOPQRSTUVWXYZ/12345678901234567890/filename.txt";
		{
			auto baseWidth = RenderOverlays::StringWidth(*font, MakeStringSection(longFileName));
			const float restrictedWidth = 512;
			REQUIRE(baseWidth > restrictedWidth);
			char buffer[1024];

			float ellipsisWidth = RenderOverlays::StringEllipsis(buffer, dimof(buffer), *font, MakeStringSection(longFileName), restrictedWidth);
			REQUIRE(ellipsisWidth < baseWidth);
			REQUIRE(ellipsisWidth <= restrictedWidth);

			ellipsisWidth = RenderOverlays::StringEllipsisDoubleEnded(buffer, dimof(buffer), *font, MakeStringSection(longFileName), MakeStringSectionLiteral("/\\"), restrictedWidth);
			REQUIRE(ellipsisWidth < baseWidth);
			REQUIRE(ellipsisWidth <= restrictedWidth);
			REQUIRE(XlEqString(buffer, "c:/.../12345678901234567890/filename.txt"));

			ellipsisWidth = RenderOverlays::StringEllipsisDoubleEnded(buffer, dimof(buffer), *font, MakeStringSectionLiteral("c://////////////////////////////////////////////////////////////////////////////////filename.txt"), MakeStringSectionLiteral("/\\"), restrictedWidth);
			REQUIRE(ellipsisWidth <= restrictedWidth);

			ellipsisWidth = RenderOverlays::StringEllipsisDoubleEnded(buffer, dimof(buffer), *font, MakeStringSectionLiteral("c:\\abcdefghijklmnopqrstuvwxyz\\ABCDEFGHIJKLMNOPQRSTUVWXYZ\\12345678901234567890\\filename.txt"), MakeStringSectionLiteral("/\\"), restrictedWidth);
			REQUIRE(ellipsisWidth <= restrictedWidth);

			// limit by buffer size, rather than rendering width
			char limitedOutputBuffer[32];
			ellipsisWidth = RenderOverlays::StringEllipsisDoubleEnded(limitedOutputBuffer, dimof(limitedOutputBuffer), *font, MakeStringSection(longFileName), MakeStringSectionLiteral("/\\"), restrictedWidth);
			REQUIRE(ellipsisWidth < baseWidth);
			REQUIRE(ellipsisWidth <= restrictedWidth);
			REQUIRE(limitedOutputBuffer[dimof(limitedOutputBuffer)-1] == '\0');
			REQUIRE(XlEqString(limitedOutputBuffer, "c:/.../filename.txt"));

			ellipsisWidth = RenderOverlays::StringEllipsis(limitedOutputBuffer, dimof(limitedOutputBuffer), *font, MakeStringSection(longFileName), restrictedWidth);
			REQUIRE(ellipsisWidth < baseWidth);
			REQUIRE(ellipsisWidth <= restrictedWidth);
			REQUIRE(limitedOutputBuffer[dimof(limitedOutputBuffer)-1] == '\0');

			// limit by buffer size again, but this time with very large values for the allowed width
			ellipsisWidth = RenderOverlays::StringEllipsisDoubleEnded(limitedOutputBuffer, dimof(limitedOutputBuffer), *font, MakeStringSection(longFileName), MakeStringSectionLiteral("/\\"), 1e6f);
			REQUIRE(limitedOutputBuffer[dimof(limitedOutputBuffer)-1] == '\0');
			REQUIRE(XlEqString(limitedOutputBuffer, "c:/.../filename.txt"));

			ellipsisWidth = RenderOverlays::StringEllipsis(limitedOutputBuffer, dimof(limitedOutputBuffer), *font, MakeStringSection(longFileName), 1e6f);
			REQUIRE(limitedOutputBuffer[dimof(limitedOutputBuffer)-1] == '\0');

			// so limited, there's no enough room for the ellipsis
			ellipsisWidth = RenderOverlays::StringEllipsisDoubleEnded(limitedOutputBuffer, 4, *font, MakeStringSection(longFileName), MakeStringSectionLiteral("/\\"), 1e6f);
			REQUIRE(std::strlen(limitedOutputBuffer) == 3);
			ellipsisWidth = RenderOverlays::StringEllipsis(limitedOutputBuffer, 4, *font, MakeStringSection(longFileName), 1e6f);
			REQUIRE(std::strlen(limitedOutputBuffer) == 3);

			// very long string with no separators
			ellipsisWidth = RenderOverlays::StringEllipsisDoubleEnded(buffer, dimof(buffer), *font, MakeStringSection(longFileName), MakeStringSectionLiteral("---"), restrictedWidth);
			REQUIRE(ellipsisWidth < baseWidth);
			REQUIRE(ellipsisWidth <= restrictedWidth);
		}

		{
			// utf8 test data
			// reference -- https://www.w3.org/2001/06/utf-8-test/UTF-8-demo.html
			// "From a speech of Demosthenes in the 4th century BC:"
			const char input[] = u8"Οὐχὶ ταὐτὰ παρίσταταί μοι γιγνώσκειν, ὦ ἄνδρες ᾿Αθηναῖοι, ὅταν τ᾿ εἰς τὰ πράγματα ἀποβλέψω καὶ ὅταν πρὸς τοὺς";
			char buffer[1024];
			const float restrictedWidth = 512;
			auto ellipsisWidth = RenderOverlays::StringEllipsisDoubleEnded(buffer, dimof(buffer), *font, MakeStringSection(input), MakeStringSectionLiteral(" "), restrictedWidth);
			REQUIRE(ellipsisWidth <= restrictedWidth);
		}

		{
			// invalid cases
			char singleSizeBuffer[1];
			float ellipsisWidth = RenderOverlays::StringEllipsisDoubleEnded(singleSizeBuffer, dimof(singleSizeBuffer), *font, MakeStringSectionLiteral("filename.txt"), MakeStringSectionLiteral("/\\"), 1024.f);
			REQUIRE(singleSizeBuffer[0] == '\0');
			REQUIRE(ellipsisWidth == 0.f);
			ellipsisWidth = RenderOverlays::StringEllipsis(singleSizeBuffer, dimof(singleSizeBuffer), *font, MakeStringSectionLiteral("filename.txt"), 1024.f);
			REQUIRE(singleSizeBuffer[0] == '\0');
			REQUIRE(ellipsisWidth == 0.f);
			char zeroSizeBuffer[0];
			ellipsisWidth = RenderOverlays::StringEllipsisDoubleEnded(zeroSizeBuffer, dimof(zeroSizeBuffer), *font, MakeStringSectionLiteral("filename.txt"), MakeStringSectionLiteral("/\\"), 1024.f);
			REQUIRE(ellipsisWidth == 0.f);
			ellipsisWidth = RenderOverlays::StringEllipsis(zeroSizeBuffer, dimof(zeroSizeBuffer), *font, MakeStringSectionLiteral("filename.txt"), 1024.f);
			REQUIRE(ellipsisWidth == 0.f);
			char largeBuffer[1024];
			ellipsisWidth = RenderOverlays::StringEllipsisDoubleEnded(largeBuffer, dimof(largeBuffer), *font, MakeStringSectionLiteral(""), MakeStringSectionLiteral("/\\"), 1024.f);
			REQUIRE(largeBuffer[0] == '\0');
			REQUIRE(ellipsisWidth == 0.f);
			ellipsisWidth = RenderOverlays::StringEllipsis(largeBuffer, dimof(largeBuffer), *font, MakeStringSectionLiteral(""), 1024.f);
			REQUIRE(largeBuffer[0] == '\0');
			REQUIRE(ellipsisWidth == 0.f);

			ellipsisWidth = RenderOverlays::StringEllipsisDoubleEnded(largeBuffer, dimof(largeBuffer), *font, MakeStringSectionLiteral("filename.txt"), MakeStringSectionLiteral("/\\"), 0.f);
			REQUIRE(largeBuffer[0] == '\0');
			REQUIRE(ellipsisWidth == 0.f);
			RenderOverlays::StringEllipsis(largeBuffer, dimof(largeBuffer), *font, MakeStringSectionLiteral("filename.txt"), 0.f);
			REQUIRE(largeBuffer[0] == '\0');
			REQUIRE(ellipsisWidth == 0.f);

			ellipsisWidth = RenderOverlays::StringEllipsisDoubleEnded(largeBuffer, dimof(largeBuffer), *font, MakeStringSectionLiteral("filename.txt"), MakeStringSectionLiteral("/\\"), -1024.f);
			REQUIRE(largeBuffer[0] == '\0');
			REQUIRE(ellipsisWidth == 0.f);
			RenderOverlays::StringEllipsis(largeBuffer, dimof(largeBuffer), *font, MakeStringSectionLiteral("filename.txt"), -1024.f);
			REQUIRE(largeBuffer[0] == '\0');
			REQUIRE(ellipsisWidth == 0.f);
		}

		{
			// no ellipsis cases
			char largeBuffer[1024];
			float ellipsisWidth = RenderOverlays::StringEllipsis(largeBuffer, dimof(largeBuffer), *font, MakeStringSectionLiteral("filename.txt"), 1024.f);
			float normalWidth = RenderOverlays::StringWidth(*font, MakeStringSectionLiteral("filename.txt"));
			REQUIRE(ellipsisWidth == normalWidth);
			REQUIRE(XlEqString(largeBuffer, "filename.txt"));

			ellipsisWidth = RenderOverlays::StringEllipsisDoubleEnded(largeBuffer, dimof(largeBuffer), *font, MakeStringSection(longFileName), MakeStringSectionLiteral("---"), 1024.f);
			normalWidth = RenderOverlays::StringWidth(*font, MakeStringSection(longFileName));
			REQUIRE(ellipsisWidth == normalWidth);
			REQUIRE(XlEqString(MakeStringSection(longFileName), largeBuffer));
		}

		::Assets::MainFileSystem::GetMountingTree()->Unmount(mnt0);
	}

	TEST_CASE( "StringWordBreak", "[renderoverlays]" )
	{
		auto globalServices = ConsoleRig::MakeGlobalServices(GetStartupConfig());
		auto mnt0 = ::Assets::MainFileSystem::GetMountingTree()->Mount("xleres", UnitTests::CreateEmbeddedResFileSystem());

		auto futureFont = RenderOverlays::MakeFont("Petra", 16);
		futureFont->StallWhilePending();
		auto font = futureFont->Actualize();

		const char longString[] = "abcdefghijklmnopqrstuvwxyz ABCDEFGHIJKLMNOPQRSTUVWXYZ 12345678901234567890";
		const char longString2[] = "\r\rabcdefghijklmnopqrstuvwxyz ABCDEFGHI\r\n\r\nJKLMNOPQRSTUVWXYZ 12345678901234567890\n";

		{
			auto split0 = RenderOverlays::StringSplitByWidth(*font, MakeStringSection(longString), 64.f, MakeStringSectionLiteral(" \t"), {});
			REQUIRE(split0._sections.size() == 3);

			auto split1 = RenderOverlays::StringSplitByWidth(*font, MakeStringSection(longString2), 64.f, MakeStringSectionLiteral(" \t"), {});
			REQUIRE(split1._sections.size() == 7);

			auto split2 = RenderOverlays::StringSplitByWidth(*font, MakeStringSection(longString2), FLT_MAX, MakeStringSectionLiteral(" \t"), {});
			// the splitting here is very specific:
			// note that the final \n has no effect on the result
			REQUIRE(split2._sections.size() == 5);
			REQUIRE(split2._sections[0].IsEmpty());
			REQUIRE(split2._sections[1].IsEmpty());
			REQUIRE(split2._sections[2].AsString() == "abcdefghijklmnopqrstuvwxyz ABCDEFGHI");
			REQUIRE(split2._sections[3].IsEmpty());
			REQUIRE(split2._sections[4].AsString() == "JKLMNOPQRSTUVWXYZ 12345678901234567890");
		}

		{
			// various odd cases
			auto split0 = RenderOverlays::StringSplitByWidth(*font, {}, FLT_MAX, MakeStringSectionLiteral(" \t"), {});
			REQUIRE(split0._sections.size() == 0);
			REQUIRE(split0._maxLineWidth == 0.f);
			auto split1 = RenderOverlays::StringSplitByWidth(*font, MakeStringSectionLiteral("         "), FLT_MAX, MakeStringSectionLiteral(" \t"), {});
			REQUIRE(split1._sections.size() == 0);
			REQUIRE(split1._maxLineWidth == 0.f);
			auto split2 = RenderOverlays::StringSplitByWidth(*font, MakeStringSectionLiteral("- - - - - - - - -"), 0.f, MakeStringSectionLiteral(" \t"), {});
			REQUIRE(split2._sections.size() == 9);
			REQUIRE(split2._maxLineWidth == RenderOverlays::StringWidth(*font, MakeStringSectionLiteral("-")));

			// trailing whitespace is just excluded, regardless of explicit newlines before it
			auto split3 = RenderOverlays::StringSplitByWidth(*font, MakeStringSectionLiteral("---\n             "), FLT_MAX, MakeStringSectionLiteral(" \t"), {});
			REQUIRE(split3._sections.size() == 1);
			REQUIRE(split3._sections[0].size() == 3);

			auto split4 = RenderOverlays::StringSplitByWidth(*font, MakeStringSectionLiteral("---             "), 0.f, MakeStringSectionLiteral(" \t"), {});
			REQUIRE(split4._sections.size() == 1);
			REQUIRE(split4._sections[0].size() == 3);
		}

	}
}

