// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "IntermediatesStore.h"
#include "ICompileOperation.h"
#include "AssetsCore.h"
#include "../OSServices/AttachableLibrary.h"
#include "../Utility/IteratorUtils.h"
#include <string>

namespace Assets
{
	class DependentFileState;
	class IFileSystem;

	class LooseFilesStorage
	{
	public:
		std::shared_ptr<IArtifactCollection> StoreCompileProducts(
			StringSection<> archivableName,
			IteratorRange<const ICompileOperation::SerializedArtifact*> artifacts,
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
			StringSection<> baseDirectory, const OSServices::LibVersionDesc& compilerVersionInfo);
		~LooseFilesStorage();
	private:
		std::string _baseDirectory;
		OSServices::LibVersionDesc _compilerVersionInfo;
		std::shared_ptr<IFileSystem> _filesystem;
		std::string MakeProductsFileName(StringSection<>);
	};
}

