// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../Assets/ICompileOperation.h"
#include "../Assets/ModelCompilationConfiguration.h"

namespace Assets { template<typename T> using PortableVector = std::vector<T>; }

namespace RenderCore { namespace Assets { namespace GeoProc
{
	class NascentModel;
	class NascentSkeleton;
	class NascentAnimationSet;
	class NascentMaterialTable;
	struct NativeVBSettings;

	::Assets::PortableVector<::Assets::SerializedArtifact> SerializeSkinToChunks(
		const std::string& name,
		const NascentModel& model,
		const NascentSkeleton& embeddedSkeleton,
		const ModelCompilationConfiguration&);

	::Assets::PortableVector<::Assets::SerializedArtifact> SerializeSkeletonToChunks(
		const std::string& name,
		const NascentSkeleton& skeleton);

	::Assets::PortableVector<::Assets::SerializedArtifact> SerializeAnimationsToChunks(
		const std::string& name,
		const NascentAnimationSet& animationSet);

	::Assets::PortableVector<::Assets::SerializedArtifact> SerializeMaterialToChunks(
		const std::string& name,
		const NascentMaterialTable&);
		
	::Assets::PortableVector<::Assets::SerializedArtifact> SerializeMaterialToChunks(
		const std::string& name,
		const NascentMaterialTable&,
		const ::Assets::DirectorySearchRules&);
}}}
