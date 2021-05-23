// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../../Math/Vector.h"
#include "../../Utility/IteratorUtils.h"
#include <vector>

namespace RenderCore { class InputElementDesc; class MiniInputElementDesc; }
namespace ToolsRig
{
    namespace Internal
    {
        #pragma pack(push)
        #pragma pack(1)
        class Vertex2D
        {
        public:
            Float2      _position;
            Float2      _texCoord;
        };

        class Vertex3D
        {
        public:
            Float3      _position;
            Float3      _normal;
            Float2      _texCoord;
            Float4      _tangent;
            // Float3      _bitangent;
        };
        #pragma pack(pop)
    }

    extern IteratorRange<const RenderCore::InputElementDesc*> Vertex2D_InputLayout;
    extern IteratorRange<const RenderCore::InputElementDesc*> Vertex3D_InputLayout;

	extern std::vector<RenderCore::MiniInputElementDesc> Vertex2D_MiniInputLayout;
    extern std::vector<RenderCore::MiniInputElementDesc> Vertex3D_MiniInputLayout;

    std::vector<Internal::Vertex3D>     BuildGeodesicSphere(int detail = 4);
    std::vector<Internal::Vertex3D>     BuildCube();
    std::vector<Internal::Vertex3D>     BuildTriangleBasePyramid();
}

