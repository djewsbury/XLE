// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../RenderOverlays/OverlayPrimitives.h"
#include "../Formatters/TextFormatter.h"		// for FormatException
#include "../Formatters/IDynamicFormatter.h"
#include "../Formatters/FormatterUtils.h"
#include "../Math/MathSerialization.h"
#include "../Utility/IteratorUtils.h"
#include "../Utility/ImpliedTyping.h"

namespace PlatformRig
{
	struct ThemeStaticData
	{
		RenderOverlays::ColorB _semiTransparentTint = 0xff2e3440;
		RenderOverlays::ColorB _topBarBorderColor = 0xffffffff;
		RenderOverlays::ColorB _headingBkgrnd = 0xffffffff;
		RenderOverlays::ColorB _menuBkgrnd[6] = { 0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff };

		unsigned _shadowOffset0 = 8;
		unsigned _shadowOffset1 = 8;
		unsigned _shadowSoftnessRadius = 16;
		
		ThemeStaticData() = default;

		template<typename Formatter>
			ThemeStaticData(Formatter& fmttr)
		{
			uint64_t keyname;
			while (TryKeyedItem(fmttr, keyname)) {
				switch (keyname) {
				case "SemiTransparentTint"_h: _semiTransparentTint = DeserializeColor(fmttr); break;
				case "TopBarBorderColor"_h: _topBarBorderColor = DeserializeColor(fmttr); break;
				case "HeadingBackground"_h: _headingBkgrnd = DeserializeColor(fmttr); break;
				case "MenuBackground0"_h: _menuBkgrnd[0] = DeserializeColor(fmttr); break;
				case "MenuBackground1"_h: _menuBkgrnd[1] = DeserializeColor(fmttr); break;
				case "MenuBackground2"_h: _menuBkgrnd[2] = DeserializeColor(fmttr); break;
				case "MenuBackground3"_h: _menuBkgrnd[3] = DeserializeColor(fmttr); break;
				case "MenuBackground4"_h: _menuBkgrnd[4] = DeserializeColor(fmttr); break;
				case "MenuBackground5"_h: _menuBkgrnd[5] = DeserializeColor(fmttr); break;
				case "ShadowOffset0"_h: _shadowOffset0 = Formatters::RequireCastValue<decltype(_shadowOffset0)>(fmttr); break;
				case "ShadowOffset1"_h: _shadowOffset1 = Formatters::RequireCastValue<decltype(_shadowOffset1)>(fmttr); break;
				case "ShadowSoftnessRadius"_h: _shadowSoftnessRadius = Formatters::RequireCastValue<decltype(_shadowSoftnessRadius)>(fmttr); break;
				default: SkipValueOrElement(fmttr); break;
				}
			}
		}
	};

	template<typename Formatter>
		inline RenderOverlays::ColorB DeserializeColor(Formatter& fmttr)
	{
		IteratorRange<const void*> value;
		ImpliedTyping::TypeDesc typeDesc;
		if (!Formatters::TryRawValue(fmttr, value, typeDesc))
			Throw(Formatters::FormatException("Expecting color value", fmttr.GetLocation()));

		if (auto intForm = ImpliedTyping::VariantNonRetained{typeDesc, value}.TryCastValue<unsigned>()) {
			return *intForm;
		} else if (auto tripletForm = ImpliedTyping::VariantNonRetained{typeDesc, value}.TryCastValue<UInt3>()) {
			return RenderOverlays::ColorB{uint8_t((*tripletForm)[0]), uint8_t((*tripletForm)[1]), uint8_t((*tripletForm)[2])};
		} else if (auto quadForm = ImpliedTyping::VariantNonRetained{typeDesc, value}.TryCastValue<UInt4>()) {
			return RenderOverlays::ColorB{uint8_t((*quadForm)[0]), uint8_t((*quadForm)[1]), uint8_t((*quadForm)[2]), uint8_t((*quadForm)[3])};
		} else {
			Throw(Formatters::FormatException("Could not interpret value as color", fmttr.GetLocation()));
		}
	}

	inline RenderOverlays::ColorB DeserializeColor(const ImpliedTyping::VariantNonRetained& value)
	{
		if (auto intForm = value.TryCastValue<unsigned>()) {
			return *intForm;
		} else if (auto tripletForm = value.TryCastValue<UInt3>()) {
			return RenderOverlays::ColorB{uint8_t((*tripletForm)[0]), uint8_t((*tripletForm)[1]), uint8_t((*tripletForm)[2])};
		} else if (auto quadForm = value.TryCastValue<UInt4>()) {
			return RenderOverlays::ColorB{uint8_t((*quadForm)[0]), uint8_t((*quadForm)[1]), uint8_t((*quadForm)[2]), uint8_t((*quadForm)[3])};
		} else {
			if (auto str = value.TryCastValue<std::string>()) {
				Throw(std::runtime_error("Could not interpret value as color: " + *str));
			} else
				Throw(std::runtime_error("Could not interpret value as color"));
		}
	}
}

