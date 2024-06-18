// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include <memory>

namespace Assets
{
    class AssetSetManager;
    class IIntermediateCompilers;
    class IIntermediatesStore;

    class Services
    {
    public:
        static AssetSetManager& GetAssetSets();
        static IIntermediateCompilers& GetIntermediateCompilers();
        static IIntermediatesStore& GetIntermediatesStore();

        static std::shared_ptr<AssetSetManager> GetAssetSetsPtr();
        static std::shared_ptr<IIntermediateCompilers> GetIntermediateCompilersPtr();
        static std::shared_ptr<IIntermediatesStore> GetIntermediatesStorePtr();
    };
}

