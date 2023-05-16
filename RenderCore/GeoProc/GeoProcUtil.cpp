// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "GeoProcUtil.h"

namespace RenderCore { namespace Assets { namespace GeoProc
{
    void AddToBoundingBox(  std::pair<Float3, Float3>& boundingBox,
                            const Float3& localPosition, const Float4x4& localToWorld)
    {
        Float3 transformedPosition = Truncate(localToWorld * Expand(localPosition, 1.f));

        boundingBox.first[0]    = std::min(transformedPosition[0], boundingBox.first[0]);
        boundingBox.first[1]    = std::min(transformedPosition[1], boundingBox.first[1]);
        boundingBox.first[2]    = std::min(transformedPosition[2], boundingBox.first[2]);
        boundingBox.second[0]   = std::max(transformedPosition[0], boundingBox.second[0]);
        boundingBox.second[1]   = std::max(transformedPosition[1], boundingBox.second[1]);
        boundingBox.second[2]   = std::max(transformedPosition[2], boundingBox.second[2]);
    }

    std::pair<Float3, Float3>       InvalidBoundingBox()
    {
        const Float3 mins(      std::numeric_limits<Float3::value_type>::max(),
                                std::numeric_limits<Float3::value_type>::max(),
                                std::numeric_limits<Float3::value_type>::max());
        const Float3 maxs(      -std::numeric_limits<Float3::value_type>::max(),
                                -std::numeric_limits<Float3::value_type>::max(),
                                -std::numeric_limits<Float3::value_type>::max());
        return std::make_pair(mins, maxs);
    }

}}}

