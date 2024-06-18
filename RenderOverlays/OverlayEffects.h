// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "OverlayPrimitives.h"
#include "../Assets/AssetsCore.h"
#include <memory>

namespace RenderCore { class IResourceView; }
namespace RenderCore { namespace Techniques { class ParsingContext; class IComputeShaderOperator; class PipelineCollection; }}
namespace std { template<typename T> class promise; }

namespace RenderOverlays
{
	class GaussianBlurOperator;
	class BroadBlurOperator;

	class BlurryBackgroundEffect
	{
	public:
		enum class Type { NarrowAccurateBlur, BroadBlur };
		std::shared_ptr<RenderCore::IResourceView> GetResourceView(
			Type type = Type::BroadBlur,
			float blurStrength = DefaultBlurStrength(),		// (0-1 value, from min to max)
			uint64_t inputAttachment = DefaultInputAttachment());
		Float2 AsTextureCoords(Coord2 screenSpace);

		BlurryBackgroundEffect(RenderCore::Techniques::ParsingContext& parsingContext);
		~BlurryBackgroundEffect();

		static void PrepareResources(std::promise<void>&&, std::shared_ptr<RenderCore::Techniques::PipelineCollection>);

		static uint64_t DefaultInputAttachment();
		static float DefaultBlurStrength();
	private:
		RenderCore::Techniques::ParsingContext* _parsingContext;
		std::shared_ptr<RenderCore::IResourceView> _backgroundResource;
		::Assets::PtrToMarkerPtr<GaussianBlurOperator> _gaussianBlur;
		::Assets::PtrToMarkerPtr<BroadBlurOperator> _broadBlur;
	};
}
