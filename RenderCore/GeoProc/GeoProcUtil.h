// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../../Math/Vector.h"
#include "../../Math/Matrix.h"

namespace RenderCore { namespace Assets { class VertexElement; }}
namespace RenderCore { namespace Assets { namespace GeoProc
{
    unsigned int    FloatBits(float input);
    unsigned int    FloatBits(double input);

    extern bool ImportCameras;

    void AddToBoundingBox(  std::pair<Float3, Float3>& boundingBox,
                            const Float3& localPosition, const Float4x4& localToWorld);
    void AddToBoundingBox(  std::pair<Float3, Float3>& boundingBox,
                            const void* vertexData, size_t vertexStride, size_t vertexCount,
                            const Assets::VertexElement& elementDesc, 
                            const Float4x4& localToWorld);
    std::pair<Float3, Float3>   InvalidBoundingBox();

    Float4 AsFloat4(const void* rawData, unsigned nativeFormat);

    Assets::VertexElement FindPositionElement(const Assets::VertexElement elements[], size_t elementCount);
}}}
