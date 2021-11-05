// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "AsyncMarkerGroup.h"
#include "IAsyncMarker.h"
#include <memory>
#include <algorithm>

namespace Assets
{
    class AsyncMarkerGroup::Entry
    {
    public:
        std::shared_ptr<IAsyncMarker> _marker;
        std::string _name;
    };
    
    void AsyncMarkerGroup::Add(const std::shared_ptr<IAsyncMarker>& marker, const std::string& name)
    {
        assert(marker);
        _entries.push_back({marker, name});
    }
    
    void AsyncMarkerGroup::Remove(const std::shared_ptr<IAsyncMarker>& marker)
    {
        _entries.erase(std::remove_if(_entries.begin(), _entries.end(),
                                      [&marker](const Entry& e) { return e._marker == marker; }),
                       _entries.end());
    }
    
    AssetState AsyncMarkerGroup::GetAssetState() const
    {
        bool hasPending = false;
        bool hasInvalid = false;
        for (const auto&e:_entries) {
            auto state = e._marker->GetAssetState();
            hasPending |= state == AssetState::Pending;
            hasInvalid |= state == AssetState::Invalid;
        }
        if (hasInvalid) return AssetState::Invalid;
        if (hasPending) return AssetState::Pending;
        return AssetState::Ready;
    }
    
    std::optional<AssetState>   AsyncMarkerGroup::StallWhilePending(std::chrono::microseconds timeout) const
    {
        auto timeToCancel = std::chrono::steady_clock::now() + timeout;

        for (const auto&e:_entries) {
            std::optional<AssetState> state;
            if (timeout.count() != 0) {
                auto now = std::chrono::steady_clock::now();
                if (now >= timeToCancel) return {};
                state = e._marker->StallWhilePending(std::chrono::duration_cast<std::chrono::microseconds>(timeToCancel - now));
            } else {
                state = e._marker->StallWhilePending(std::chrono::microseconds(0));
            }
            if (!state) return {};
            if (state == AssetState::Invalid)
                return AssetState::Invalid;     // as soon as we hit an invalid; we will no longer stall for any remaining assets
        }
        return AssetState::Ready;
    }
    
    AsyncMarkerGroup::AsyncMarkerGroup()
    {
    }
    
    AsyncMarkerGroup::~AsyncMarkerGroup()
    {
    }
    
}
