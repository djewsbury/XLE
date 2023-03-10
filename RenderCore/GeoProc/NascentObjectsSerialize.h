// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../Assets/ICompileOperation.h"

namespace RenderCore { namespace Assets { namespace GeoProc
{
	class NascentModel;
	class NascentSkeleton;
	class NascentAnimationSet;
	struct NativeVBSettings;

	std::vector<::Assets::SerializedArtifact> SerializeSkinToChunks(
		const std::string& name,
		const NascentModel& model,
		const NascentSkeleton& embeddedSkeleton,
		const NativeVBSettings& nativeSettings);

	std::vector<::Assets::SerializedArtifact> SerializeSkeletonToChunks(
		const std::string& name,
		const NascentSkeleton& skeleton);

	std::vector<::Assets::SerializedArtifact> SerializeAnimationsToChunks(
		const std::string& name,
		const NascentAnimationSet& animationSet);
}}}
