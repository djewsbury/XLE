// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "AssetsCore.h"
#include "IAsyncMarker.h"
#include "../Utility/Threading/Mutex.h"
#include <vector>

namespace Assets
{
    /** <summary>Group together multiple IAsyncMarkers into a single marker</summary>
    This is a "future" object that represents multiple asychronous operations.
    The state is set according to the component operations as per these rules:
    - if any operation is invalid, the group is invalid (even if some operations are pending)
    - if any operation is pending, the group is pending
    - otherwise, all operations are ready and the group is ready
    */
    class AsyncMarkerGroup : public IAsyncMarker
    {
    public:
        void Add(const std::shared_ptr<IAsyncMarker>&, const std::string& name);
        void Remove(const std::shared_ptr<IAsyncMarker>&);
        
        AssetState        GetAssetState() const override;
        std::optional<AssetState>   StallWhilePending(std::chrono::microseconds timeout = std::chrono::microseconds(0)) const override;

        AsyncMarkerGroup();
        ~AsyncMarkerGroup();
    private:
        class Entry;
        std::vector<Entry> _entries;
        Threading::Mutex _lock;
    };
}
