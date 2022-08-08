// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../../Math/Matrix.h"
#include "../../Utility/IteratorUtils.h"

namespace RenderCore { namespace Techniques
{
	class DrawableConstructor;
	class DrawablesPacket;
	class ModelConstructionSkeletonBinding;

	struct LightWeightBuildDrawables
	{
		static void InstancedFixedSkeleton(
			DrawableConstructor& constructor,
			IteratorRange<DrawablesPacket** const> pkts,
			IteratorRange<const Float3x4*> objectToWorlds);

		static void InstancedFixedSkeleton(
			DrawableConstructor& constructor,
			IteratorRange<DrawablesPacket** const> pkts,
			IteratorRange<const Float3x4*> objectToWorlds,
			IteratorRange<const unsigned*> viewMasks);

		static void SingleInstance(
			DrawableConstructor& constructor,
			IteratorRange<DrawablesPacket** const> pkts,
			const Float3x4& objectToWorld,
			unsigned deformInstanceIdx = 0,
			uint32_t viewMask= 1);

		static void SingleInstance(
			DrawableConstructor& constructor,
			IteratorRange<DrawablesPacket** const> pkts,
			const Float3x4& objectToWorld,
			const ModelConstructionSkeletonBinding& skeletonBinding,
			IteratorRange<const Float4x4*> animatedSkeletonOutput,
			unsigned deformInstanceIdx = 0,
			uint32_t viewMask= 1);
	};
}}
