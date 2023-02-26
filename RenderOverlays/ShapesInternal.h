// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "OverlayPrimitives.h"
#include "../Math/Vector.h"
#include <memory>
#include <future>

namespace RenderCore { namespace Techniques { class ImmediateDrawableMaterial; }}
namespace RenderOverlays { class IOverlayContext; class Font; }

namespace RenderOverlays { namespace Internal
{
	class DefaultFontsBox
	{
	public:
		std::shared_ptr<Font> _defaultFont;
		std::shared_ptr<Font> _tableHeaderFont;
		std::shared_ptr<Font> _tableValuesFont;

		DefaultFontsBox(
			std::shared_ptr<Font> defaultFont,
			std::shared_ptr<Font> headerFont,
			std::shared_ptr<Font> valuesFont)
		: _defaultFont(std::move(defaultFont)), _tableHeaderFont(std::move(headerFont)), _tableValuesFont(std::move(valuesFont))
		{}

		static void ConstructToPromise(std::promise<std::shared_ptr<DefaultFontsBox>>&& promise);
	};

	void DrawPCCTTQuad(
		IOverlayContext& context,
		const Float3& mins, const Float3& maxs, 
		ColorB color0, ColorB color1,
		const Float2& minTex0, const Float2& maxTex0, 
		const Float2& minTex1, const Float2& maxTex1,
		RenderCore::Techniques::ImmediateDrawableMaterial&& material);

	struct CB_RoundedRectSettings
	{
		float _roundedProportion;
		unsigned _cornerFlags;
		unsigned _dummy[2];
	};
}}
