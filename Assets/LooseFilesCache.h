// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "AssetsCore.h"
#include "../OSServices/AttachableLibrary.h"
#include "../Utility/IteratorUtils.h"
#include <string>

namespace Assets
{
	struct DependentFileState;
	class IFileSystem;
	struct SerializedArtifact;
	class IArtifactCollection;
	class StoreReferenceCounts;

	class LooseFilesStorage
	{
	public:
		std::shared_ptr<IArtifactCollection> StoreCompileProducts(
			StringSection<> archivableName,
			IteratorRange<const SerializedArtifact*> artifacts,
			::Assets::AssetState state,
			IteratorRange<const DependentFileState*> dependencies,
			const std::shared_ptr<StoreReferenceCounts>& storeRefCounts,
			uint64_t refCountHashCode);

		std::shared_ptr<IArtifactCollection> RetrieveCompileProducts(
			StringSection<> archivableName,
			const std::shared_ptr<StoreReferenceCounts>& storeRefCounts,
			uint64_t refCountHashCode);

		LooseFilesStorage(
			std::shared_ptr<IFileSystem> filesystem,
			StringSection<> baseDirectory, StringSection<> fsMountPt, const OSServices::LibVersionDesc& compilerVersionInfo,
			bool checkDepVals);
		~LooseFilesStorage();
	private:
		std::string _baseDirectory;
		OSServices::LibVersionDesc _compilerVersionInfo;
		std::shared_ptr<IFileSystem> _filesystem;
		std::string _fsMountPt;
		bool _checkDepVals;
		std::string MakeProductsFileName(StringSection<>);
	};
}

