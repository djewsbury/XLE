// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "OverlayPrimitives.h"
#include "../Math/Vector.h"
#include "../Assets/DepVal.h"
#include <memory>
#include <future>

namespace RenderCore { namespace Techniques { class ImmediateDrawableMaterial; class RetainedUniformsStream; }}
namespace RenderOverlays { class IOverlayContext; class Font; }

namespace RenderOverlays { namespace Internal
{
	class DefaultFontsBox
	{
	public:
		std::shared_ptr<Font> _defaultFont;
		std::shared_ptr<Font> _tableHeaderFont;
		std::shared_ptr<Font> _tableValuesFont;
		::Assets::DependencyValidation _depVal;

		const ::Assets::DependencyValidation& GetDependencyValidation() const { return _depVal; }

		DefaultFontsBox(
			std::shared_ptr<Font> defaultFont,
			std::shared_ptr<Font> headerFont,
			std::shared_ptr<Font> valuesFont,
			::Assets::DependencyValidation depVal)
		: _defaultFont(std::move(defaultFont)), _tableHeaderFont(std::move(headerFont)), _tableValuesFont(std::move(valuesFont)), _depVal(std::move(depVal))
		{}
		DefaultFontsBox();

		static void ConstructToPromise(std::promise<std::shared_ptr<DefaultFontsBox>>&& promise);
	};

	DefaultFontsBox& GetDefaultFontsBox();

	void DrawPCCTTQuad(
		IOverlayContext& context,
		const Float3& mins, const Float3& maxs, 
		ColorB color0, ColorB color1,
		const Float2& minTex0, const Float2& maxTex0, 
		const Float2& minTex1, const Float2& maxTex1,
		const RenderCore::Techniques::ImmediateDrawableMaterial& material,
		RenderCore::Techniques::RetainedUniformsStream&& uniforms);

	struct CB_RoundedRectSettings
	{
		float _roundedProportion = 1.f / 8.f;
		float _roundingMaxPixels = 16.f;
		unsigned _cornerFlags = 0xf;
		unsigned _dummy[1];
	};

	struct CB_ShapesFramework
	{
		float _borderSizePix = 1.f;
	};
}}
