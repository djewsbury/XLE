// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../Math/Vector.h"
#include "../Math/Matrix.h"
#include <vector>
#include <memory>

namespace RenderCore { class IThreadContext; }
namespace RenderCore { namespace Techniques 
{
    class TechniqueContext; class CameraDesc;
	class ParsingContext;
    class IPipelineAcceleratorPool;
    class DrawablesPacket;
    struct Drawable;
    using VisibilityMarkerId = uint32_t;
}}

namespace SceneEngine
{
    class ModelIntersectionResources;

    class ModelIntersectionStateContext
    {
    public:
        struct ResultEntry
        {
            Float3 _ptA; float _barycentricA;
            Float3 _ptB; float _barycentricB;
            Float3 _ptC; float _barycentricC;
			float _intersectionDepth;
            unsigned _drawableIndex;
            unsigned _packetIndex;
            Float3 _normal;

            static bool CompareDepth(const ResultEntry& lhs, const ResultEntry& rhs)
                { return lhs._intersectionDepth < rhs._intersectionDepth; }
        };

        std::vector<ResultEntry> GetResults();
        void SetRay(const std::pair<Float3, Float3> worldSpaceRay);
        void SetFrustum(const Float4x4& frustum);
        void ExecuteDrawables(
            RenderCore::Techniques::ParsingContext& parsingContext, 
            RenderCore::Techniques::DrawablesPacket& drawablePkt,
            unsigned pktIdx,
            const RenderCore::Techniques::CameraDesc* cameraForLOD = nullptr);

        enum TestType { RayTest = 0, FrustumTest = 1 };

        ModelIntersectionStateContext(
            TestType testType,
            RenderCore::IThreadContext& threadContext,
            const std::shared_ptr<RenderCore::Techniques::IPipelineAcceleratorPool>& pipelineAcceleratorPool,
            RenderCore::Techniques::VisibilityMarkerId visibilityMarkerId);
        ~ModelIntersectionStateContext();

    protected:
        class Pimpl;
        std::unique_ptr<Pimpl> _pimpl;

        static const unsigned s_maxResultCount = 256;
    };
}

