// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "AssetServices.h"
#include "AssetSetManager.h"
#include "CompileAndAsyncManager.h"
#include "../ConsoleRig/AttachablePtr.h"

namespace Assets
{
	static ConsoleRig::WeakAttachablePtr<AssetSetManager> s_assetSetsManagerInstance;
	static ConsoleRig::WeakAttachablePtr<CompileAndAsyncManager> s_compileAndAsyncManager;

	AssetSetManager& Services::GetAssetSets()
	{
		return *s_assetSetsManagerInstance.lock();
	}

	std::shared_ptr<AssetSetManager> Services::GetAssetSetsPtr()
	{
		return s_assetSetsManagerInstance.lock();
	}

	CompileAndAsyncManager& Services::GetAsyncMan()
	{
		return *s_compileAndAsyncManager.lock();
	}

	bool Services::HasAssetSets()
	{
		return !s_assetSetsManagerInstance.expired();
	}
}

