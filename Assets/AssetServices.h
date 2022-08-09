// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include <memory>

namespace Assets
{
    class AssetSetManager;
    class CompileAndAsyncManager;

    class Services
    {
    public:
        static AssetSetManager& GetAssetSets();
        static std::shared_ptr<AssetSetManager> GetAssetSetsPtr();
        static CompileAndAsyncManager& GetAsyncMan();
        static bool HasAssetSets();
    };
}

