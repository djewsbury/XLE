// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "AssetServices.h"
#include "AssetSetManager.h"
#include "../ConsoleRig/AttachablePtr.h"

namespace Assets
{
	static ConsoleRig::WeakAttachablePtr<AssetSetManager> s_assetSetsManagerInstance;
	static ConsoleRig::WeakAttachablePtr<IIntermediateCompilers> s_intermediateCompilers;
	static ConsoleRig::WeakAttachablePtr<IIntermediatesStore> s_intermediatesStore;

	AssetSetManager& Services::GetAssetSets() { return *s_assetSetsManagerInstance.lock(); }
	IIntermediateCompilers& Services::GetIntermediateCompilers() { return *s_intermediateCompilers.lock(); }
	IIntermediatesStore& Services::GetIntermediatesStore() { return *s_intermediatesStore.lock(); }

	std::shared_ptr<AssetSetManager> Services::GetAssetSetsPtr() { return s_assetSetsManagerInstance.lock(); }
	std::shared_ptr<IIntermediateCompilers> Services::GetIntermediateCompilersPtr() { return s_intermediateCompilers.lock(); }
	std::shared_ptr<IIntermediatesStore> Services::GetIntermediatesStorePtr() { return s_intermediatesStore.lock(); }
}

