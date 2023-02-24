// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "OverlayPrimitives.h"
#include "../Assets/AssetsCore.h"
#include <memory>

namespace RenderCore { class IResourceView; }
namespace RenderCore { namespace Techniques { class ParsingContext; class IComputeShaderOperator; }}

namespace RenderOverlays
{
	class GaussianBlurOperator;
	class BroadBlurOperator;

	class BlurryBackgroundEffect
	{
	public:
		std::shared_ptr<RenderCore::IResourceView> GetResourceView();
		Float2 AsTextureCoords(Coord2 screenSpace);

		enum class Type { NarrowAccurateBlur, BroadBlur };

		BlurryBackgroundEffect(RenderCore::Techniques::ParsingContext& parsingContext, Type type = Type::BroadBlur);
		~BlurryBackgroundEffect();
	private:
		RenderCore::Techniques::ParsingContext* _parsingContext;
		std::shared_ptr<RenderCore::IResourceView> _backgroundResource;
		::Assets::PtrToMarkerPtr<GaussianBlurOperator> _gaussianBlur;
		::Assets::PtrToMarkerPtr<BroadBlurOperator> _broadBlur;
	};
}
