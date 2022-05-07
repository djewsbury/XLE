// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "NascentObjectsSerialize.h"
#include "NascentModel.h"
#include "NascentCommandStream.h"
#include "../Assets/AssetUtils.h"
#include "../Assets/NascentChunk.h"
#include <sstream>

namespace RenderCore { namespace Assets { namespace GeoProc
{
	std::vector<::Assets::ICompileOperation::SerializedArtifact> SerializeSkinToChunks(
		const std::string& name,
		const NascentModel& model,
		const NascentSkeleton& embeddedSkeleton,
		const NativeVBSettings& nativeSettings)
	{
		return model.SerializeToChunks(name, embeddedSkeleton, nativeSettings);
	}

	std::vector<::Assets::ICompileOperation::SerializedArtifact> SerializeSkeletonToChunks(
		const std::string& name,
		const NascentSkeleton& skeleton)
	{
		auto block = ::Assets::SerializeToBlob(skeleton);

		std::stringstream metricsStream;
		SerializationOperator(metricsStream, skeleton.GetSkeletonMachine());
		auto metricsBlock = ::Assets::AsBlob(metricsStream);

		return {
			::Assets::ICompileOperation::SerializedArtifact{
				RenderCore::Assets::ChunkType_Skeleton, 0, name, 
				std::move(block)},
			::Assets::ICompileOperation::SerializedArtifact{
				RenderCore::Assets::ChunkType_Metrics, 0, "skel-" + name, 
				std::move(metricsBlock)}
		};
	}

	std::vector<::Assets::ICompileOperation::SerializedArtifact> SerializeAnimationsToChunks(
		const std::string& name,
		const NascentAnimationSet& animationSet)
	{
		::Assets::BlockSerializer serializer;
		SerializationOperator(serializer, animationSet);
		auto block = ::Assets::AsBlob(serializer);

		std::stringstream metricsStream;
		SerializationOperator(metricsStream, animationSet);
		auto metricsBlock = ::Assets::AsBlob(metricsStream);

		return {
			::Assets::ICompileOperation::SerializedArtifact{
				RenderCore::Assets::ChunkType_AnimationSet, 0, name, 
				std::move(block)},
			::Assets::ICompileOperation::SerializedArtifact{
				RenderCore::Assets::ChunkType_Metrics, 0, "anim-" + name, 
				std::move(metricsBlock)}
		};
	}

}}}
