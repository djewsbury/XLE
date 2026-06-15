// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../../Math/Matrix.h"
#include "../../Utility/IteratorUtils.h"
#include "../../Utility/StringUtils.h"

namespace RenderCore { class UniformsStreamInterface; }
namespace std { template<typename T> class future; }

namespace RenderCore { namespace Techniques
{
	class DrawableConstructor;
	class DrawablesPacket;
	struct ModelConstructionSkeletonBinding;
	class RetainedUniformsStream;
	class IDrawablesPool;
	class IPipelineAcceleratorPool;

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

		static void VertexStreamInstancedFixedSkeleton(
			DrawableConstructor& constructor,
			IteratorRange<DrawablesPacket** const> pkts,
			unsigned instanceCount,
			IteratorRange<const void*> vertexStream1Data);

		static void SingleInstance(
			DrawableConstructor& constructor,
			IteratorRange<DrawablesPacket** const> pkts,
			const Float3x4& objectToWorld,
			unsigned deformInstanceIdx = 0,
			uint32_t viewMask = 1);

		static void SingleInstance(
			DrawableConstructor& constructor,
			IteratorRange<DrawablesPacket** const> pkts,
			const Float3x4& objectToWorld,
			const ModelConstructionSkeletonBinding& skeletonBinding,
			IteratorRange<const Float4x4*> animatedSkeletonOutput,
			unsigned deformInstanceIdx = 0,
			uint32_t viewMask = 1);

		static void SingleInstance(
			DrawableConstructor& constructor,
			IteratorRange<DrawablesPacket** const> pkts,
			UniformsStreamInterface& usi,			// usi must out-live the drawables created
			const RetainedUniformsStream& uniforms,
			unsigned deformInstanceIdx = 0);

		static unsigned GetDrawableCount(
			DrawableConstructor& constructor,
			unsigned pktIndex);
	};

	std::future<std::shared_ptr<DrawableConstructor>> CreateDrawableConstructorFromModelAndMaterial(
		std::shared_ptr<IDrawablesPool> dp,
		std::shared_ptr<IPipelineAcceleratorPool> pa,
		StringSection<> model, StringSection<> material = {});

	std::future<std::shared_ptr<DrawableConstructor>> CreateDrawableConstructorFromCompoundObject(
		std::shared_ptr<IDrawablesPool> dp,
		std::shared_ptr<IPipelineAcceleratorPool> pa,
		StringSection<> compoundObject);
}}
