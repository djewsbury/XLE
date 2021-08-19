// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../Math/Matrix.h"
#include "../Utility/IteratorUtils.h"

namespace RenderCore { class IThreadContext; }
namespace RenderCore { namespace Techniques { class ParsingContext; } }
namespace RenderCore { namespace Assets { class SkeletonMachine; class TransformationParameterSet; } }

namespace RenderOverlays
{
	class IOverlayContext;

	void    RenderSkeleton(
        IOverlayContext& overlayContext, 
        RenderCore::Techniques::ParsingContext& parserContext, 
		const RenderCore::Assets::SkeletonMachine& skeleton,
		IteratorRange<const Float4x4*> outputMatrices,
		const Float4x4& localToWorld,
		bool drawBoneNames = false);

	void    RenderSkeleton(
        IOverlayContext& overlayContext, 
        RenderCore::Techniques::ParsingContext& parserContext, 
		const RenderCore::Assets::SkeletonMachine& skeleton,
		const Float4x4& localToWorld,
		bool drawBoneNames = false);
}
