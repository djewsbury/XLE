// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "NascentObjectsSerialize.h"
#include "NascentModel.h"
#include "NascentCommandStream.h"
#include "NascentMaterialTable.h"
#include "../Assets/AssetUtils.h"
#include "../../Assets/NascentChunk.h"
#include "../../Formatters/TextOutputFormatter.h"
#include "../../Utility/Streams/SerializationUtils.h"
#include <sstream>

#if defined(_DEBUG)
	#define WRITE_METRICS
#endif

namespace RenderCore { namespace Assets { namespace GeoProc
{
	std::vector<::Assets::SerializedArtifact> SerializeSkinToChunks(
		const std::string& name,
		const NascentModel& model,
		const NascentSkeleton& embeddedSkeleton,
		const ModelCompilationConfiguration& cfg)
	{
		return model.SerializeToChunks(name, embeddedSkeleton, cfg);
	}

	std::vector<::Assets::SerializedArtifact> SerializeSkeletonToChunks(
		const std::string& name,
		const NascentSkeleton& skeleton)
	{
		auto block = ::Assets::SerializeToBlob(skeleton);

		#if defined(WRITE_METRICS)
			std::stringstream metricsStream;
			SerializationOperator(metricsStream, skeleton.GetSkeletonMachine());
			auto metricsBlock = ::Assets::AsBlob(metricsStream);
		#endif

		return {
			::Assets::SerializedArtifact{
				RenderCore::Assets::ChunkType_Skeleton, 0, name, 
				std::move(block)},
			#if defined(WRITE_METRICS)
				::Assets::SerializedArtifact{
					RenderCore::Assets::ChunkType_Metrics, 0, "skel-" + name, 
					std::move(metricsBlock)}
			#endif
		};
	}

	std::vector<::Assets::SerializedArtifact> SerializeAnimationsToChunks(
		const std::string& name,
		const NascentAnimationSet& animationSet)
	{
		::Assets::BlockSerializer serializer;
		SerializationOperator(serializer, animationSet);
		auto block = ::Assets::AsBlob(serializer);

		#if defined(WRITE_METRICS)
			std::stringstream metricsStream;
			SerializationOperator(metricsStream, animationSet);
			auto metricsBlock = ::Assets::AsBlob(metricsStream);
		#endif

		return {
			::Assets::SerializedArtifact{
				RenderCore::Assets::ChunkType_AnimationSet, 0, name, 
				std::move(block)},
			#if defined(WRITE_METRICS)
				::Assets::SerializedArtifact{
					RenderCore::Assets::ChunkType_Metrics, 0, "anim-" + name, 
					std::move(metricsBlock)}
			#endif
		};
	}

	std::vector<::Assets::SerializedArtifact> SerializeMaterialToChunks(
		const std::string& name,
		const NascentMaterialTable& materialTable,
		const ::Assets::DirectorySearchRules& searchRules)
	{
		MemoryOutputStream<> strm;
		{
			Formatters::TextOutputFormatter fmttr(strm);
			fmttr << materialTable;
		}

		return {
			::Assets::SerializedArtifact{
				RenderCore::Assets::ChunkType_RawMat, 0, name,
				::Assets::AsBlob(strm)
			},
			::Assets::SerializedArtifact{
				ConstHash64("DirectorySearchRules"), 0, name,
				searchRules.Serialize()
			}
		};
	}

}}}
