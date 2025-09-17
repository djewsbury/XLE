// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "TerrainOp.h"
#include <vector>

namespace ToolsRig
{
    /// <summary>Generates ambient occlusion for terrain</summary>
    /// For each point on the terrain, calculates how much of the sky hemisphere
    /// is hidden by other parts of the terrain. This can be used to calculate
    /// the quantity of ambient light that effects the object.
    class AOOperator : public ITerrainOp
    {
    public:
        void Calculate(
            void* dst, Float2 coord, 
            SceneEngine::TerrainUberHeightsSurface& heightsSurface, float xyScale) const;
        ImpliedTyping::TypeDesc GetOutputFormat() const;
        void FillDefault(void* dst, unsigned count) const;
        const char* GetName() const;

        AOOperator(unsigned testRadius, float power);
        ~AOOperator();
    protected:
        using AoSample = uint8;
        unsigned _testRadius;
        float _power;
        std::vector<Float3> _testPts;
    };

    class AngleBasedShadowsOperator : public ITerrainOp
    {
    public:
        void Calculate(
            void* dst, Float2 coord, 
            SceneEngine::TerrainUberHeightsSurface& heightsSurface, float xyScale) const;
        ImpliedTyping::TypeDesc GetOutputFormat() const;
        void FillDefault(void* dst, unsigned count) const;
        const char* GetName() const;

            // note: searchDistance is in number of grid elements (ie, not world space units)
        AngleBasedShadowsOperator(Float2 sunDirectionOfMovement, float searchDistance);
        ~AngleBasedShadowsOperator();
    protected:
        Float2 _sunDirectionOfMovement;
        float _searchDistance;
    };
}
