// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "TestPlatformScene.h"
#include "../Shared/SampleGlobals.h"
#include "../../PlatformRig/PlatformRigUtil.h"
#include "../../SceneEngine/LightDesc.h"
#include "../../SceneEngine/LightingParserContext.h"
#include "../../SceneEngine/DualContour.h"
#include "../../SceneEngine/DualContourRender.h"
#include "../../RenderCore/Techniques/TechniqueUtils.h"
#include "../../RenderCore/Metal/DeviceContext.h"

#include "../../Tools/ToolsRig/VisualisationUtils.h"

#include "../../Assets/DepVal.h"
#include "../../ConsoleRig/Console.h"
#include "../../ConsoleRig/ResourceBox.h"
#include "../../Utility/Profiling/CPUProfiler.h"
#include "../../Math/Noise.h"
#include "../../Math/Geometry.h"

namespace Sample
{
    class TestPlatformSceneParser::Pimpl
    {
    public:
        std::shared_ptr<ToolsRig::VisCameraSettings> _camera;
        SceneEngine::EnvironmentSettings _envSettings;

        float _time;
    };

///////////////////////////////////////////////////////////////////////////////////////////////////

    void TestPlatformSceneParser::PrepareFrame(RenderCore::IThreadContext& context)
    {
    }

    namespace Test
    {
        using namespace SceneEngine;

        class DualContourTest
        {
        public:
            DualContourMesh _mesh;
            std::unique_ptr<DualContourRenderer> _renderer;

            const ::Assets::DependencyValidation& GetDependencyValidation() const { return _dependencyValidation; }

            DualContourTest(unsigned gridDims);
            ~DualContourTest();

        protected:
            ::Assets::DependencyValidation _dependencyValidation;
        };

        class TestDensityFunction : public IVolumeDensityFunction
        {
        public:
            Boundary    GetBoundary() const
            {
                return std::make_pair(Float3(-10.f, -10.f, -10.f), Float3(10.f, 10.f, 10.f));
            }

            float       GetDensity(const Float3& pt) const
            {
                float radius = 10.f * .66f;
                
                // float basic = 1.f - (Magnitude(pt) / radius);

                float xyLen = Magnitude(Truncate(pt));
                float a = std::max(1.f-XlAbs(3.f*3.f*xyLen*xyLen/(radius*radius)), XlExp(-XlAbs(3.f*xyLen/radius))) - 0.05f;
                float basic = radius * a - XlAbs(pt[2]);

                static Float3 posMult(.75f, .75f, .3f);
                static float hgrid = 1.f;
                static float gain = 0.75f;
                static float lacunarity = 2.1042f;
                static unsigned octaves = 4;
                
                auto spherical0 = CartesianToSpherical(Float3(pt[0], pt[1], pt[2]));
                auto spherical1 = CartesianToSpherical(Float3(pt[1], pt[2], pt[0]));
                auto spherical2 = CartesianToSpherical(Float3(pt[2], pt[0], pt[1]));
                float weight0 = 1.f - 2.f * XlAbs(.5f - spherical0[0] / gPI);
                float weight1 = 1.f - 2.f * XlAbs(.5f - spherical1[0] / gPI);
                float weight2 = 1.f - 2.f * XlAbs(.5f - spherical2[0] / gPI);
                
                if ((weight0 + weight1 + weight2) == 0.f)
                    return basic; // (happens at the origin)
                
                float noise0 = SimplexFBM(
                    Float4(spherical0[0] * posMult[0], XlCos(spherical0[1]) * posMult[1], XlSin(spherical0[1]) * posMult[1], spherical0[2] * posMult[2]),
                    hgrid, gain, lacunarity, octaves);
                float noise1 = SimplexFBM(
                    Float4(spherical1[0] * posMult[0], XlCos(spherical1[1]) * posMult[1], XlSin(spherical1[1]) * posMult[1], spherical1[2] * posMult[2]),
                    hgrid, gain, lacunarity, octaves);
                float noise2 = SimplexFBM(
                    Float4(spherical2[0] * posMult[0], XlCos(spherical2[1]) * posMult[1], XlSin(spherical2[1]) * posMult[1], spherical2[2] * posMult[2]),
                    hgrid, gain, lacunarity, octaves);
                
                float noiseScale = 4.f * std::min(1.f, 10.f * (1.f - std::min(xyLen, 10.f) / 10.f));
                return basic + noiseScale * //.33f * 
                        ( (noise0 * weight0 / (weight0 + weight1 + weight2))
                        + (noise1 * weight1 / (weight0 + weight1 + weight2))
                        + (noise2 * weight2 / (weight0 + weight1 + weight2)));
            }

            Float3      GetNormal(const Float3& pt) const
            {
                static float range = 0.05f;
                float x0 = GetDensity(pt + Float3(-range, 0.f, 0.f));
                float x1 = GetDensity(pt + Float3( range, 0.f, 0.f));
                float y0 = GetDensity(pt + Float3(0.f, -range, 0.f));
                float y1 = GetDensity(pt + Float3(0.f,  range, 0.f));
                float z0 = GetDensity(pt + Float3(0.f, 0.f, -range));
                float z1 = GetDensity(pt + Float3(0.f, 0.f,  range));
                return Normalize(Float3(x0-x1, y0-y1, z0-z1));
            }
        };

        DualContourTest::DualContourTest(unsigned gridDims)
        {
			_mesh = DualContourMesh_Build({gridDims, gridDims, gridDims}, TestDensityFunction());
            _renderer = std::make_unique<DualContourRenderer>(std::ref(_mesh));
            
            _dependencyValidation = _renderer->GetDependencyValidation();
        }

        DualContourTest::~DualContourTest() {}
    }

    void TestPlatformSceneParser::ExecuteScene(   
        RenderCore::IThreadContext& context, 
		RenderCore::Techniques::ParsingContext& parserContext,
        LightingParserContext& lightingParserContext, 
        const SceneParseSettings& parseSettings,
        SceneEngine::PreparedScene& preparedPackets,
        unsigned techniqueIndex) const
    {
        CPUProfileEvent pEvnt("ExecuteScene", g_cpuProfiler);

        bool renderAsCloud = Tweakable("RenderAsCloud", false);
        auto& box = ConsoleRig::FindCachedBox<Test::DualContourTest>(Tweakable("GridDims", 256));

        auto& metalContext = *RenderCore::Metal::DeviceContext::Get(context);

        if (    parseSettings._batchFilter == RenderCore::Techniques::BatchFilter::General
            ||  parseSettings._batchFilter == RenderCore::Techniques::BatchFilter::PreDepth
            ||  parseSettings._batchFilter == RenderCore::Techniques::BatchFilter::DMShadows
            ||  parseSettings._batchFilter == RenderCore::Techniques::BatchFilter::RayTracedShadows) {

            if (!renderAsCloud)
                box._renderer->Render(&metalContext, parserContext, techniqueIndex);
        }

        if (parseSettings._batchFilter == RenderCore::Techniques::BatchFilter::Transparent) {
            if (renderAsCloud)
                box._renderer->RenderAsCloud(&metalContext, parserContext);
            if (Tweakable("TerrainWireframe", false))
                box._renderer->RenderUnsortedTrans(&metalContext, parserContext, 8);
        }
    }

    void TestPlatformSceneParser::PrepareScene(
        RenderCore::IThreadContext& context, 
        RenderCore::Techniques::ParsingContext& parserContext,
        SceneEngine::PreparedScene& preparedPackets) const
    {
    }

    bool TestPlatformSceneParser::HasContent(const SceneParseSettings& parseSettings) const
    {
        if (    parseSettings._batchFilter == RenderCore::Techniques::BatchFilter::General
            ||  parseSettings._batchFilter == RenderCore::Techniques::BatchFilter::PreDepth
            ||  parseSettings._batchFilter == RenderCore::Techniques::BatchFilter::DMShadows
            ||  parseSettings._batchFilter == RenderCore::Techniques::BatchFilter::RayTracedShadows)
            return true;
        if (parseSettings._batchFilter == RenderCore::Techniques::BatchFilter::Transparent)
            return true;
        return false;
    }

///////////////////////////////////////////////////////////////////////////////////////////////////

    RenderCore::Techniques::CameraDesc TestPlatformSceneParser::GetCameraDesc() const  {  return ToolsRig::AsCameraDesc(*_pimpl->_camera); }
    float TestPlatformSceneParser::GetTimeValue() const {  return _pimpl->_time; }
    std::shared_ptr<ToolsRig::VisCameraSettings> TestPlatformSceneParser::GetCameraPtr() { return _pimpl->_camera; }
    void TestPlatformSceneParser::Update(float deltaTime) { _pimpl->_time += deltaTime;  }
    const SceneEngine::EnvironmentSettings& TestPlatformSceneParser::GetEnvSettings() const { return _pimpl->_envSettings; }

    TestPlatformSceneParser::TestPlatformSceneParser()
    {
        auto pimpl = std::make_unique<Pimpl>();
        pimpl->_time = 0.f;

        pimpl->_camera = std::make_shared<ToolsRig::VisCameraSettings>();
        pimpl->_camera->_nearClip = 0.5f;
        pimpl->_camera->_farClip = 6000.f;
        pimpl->_camera->_position = Float3(-10.f, 0.f, 0.f);

        pimpl->_envSettings = PlatformRig::DefaultEnvironmentSettings();

        _pimpl = std::move(pimpl);
    }

    TestPlatformSceneParser::~TestPlatformSceneParser()
    {}


}

