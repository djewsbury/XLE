// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "TerrainManipulators.h"
#include "TerrainManipulatorsCommon.h"
#include "ManipulatorsUtil.h"
#include "ManipulatorsRender.h"
#include "../../RenderOverlays/DebuggingDisplay.h"
#include "../../RenderOverlays/Font.h"

#include "../../SceneEngine/Terrain.h"
#include "../../SceneEngine/TerrainUberSurface.h"
#include "../../SceneEngine/SceneEngineUtils.h"
#include "../../SceneEngine/IntersectionTest.h"

#include "../../RenderCore/Techniques/Techniques.h"
#include "../../RenderCore/Techniques/CommonResources.h"

#include "../../RenderCore/Metal/DeviceContext.h"
#include "../../RenderCore/Metal/State.h"
#include "../../RenderCore/Metal/InputLayout.h"
#include "../../RenderCore/RenderUtils.h"

#include "../../Math/ProjectionMath.h"
#include "../../Math/Transformations.h"
#include "../../Math/Geometry.h"
#include "../../Utility/TimeUtils.h"
#include "../../ConsoleRig/Console.h"
#include "../../ConsoleRig/ResourceBox.h"

#include "../../RenderCore/DX11/Metal/DX11Utils.h"

extern unsigned FrameRenderCount;

namespace ToolsRig
{
    static const char HeightsLayerError[] = "This tool only works on heights values. Select the terrain heights layer.";

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
        //      M A N I P U L A T O R S             //
    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

    class RaiseLowerManipulator : public CommonManipulator
    {
    public:
        virtual void    PerformAction(const Float3& worldSpacePosition, float size, float strength);
        virtual const char* GetName() const { return "Raise and Lower"; }

        virtual IteratorRange<const FloatParameter*>  GetFloatParameters() const;
		virtual IteratorRange<const BoolParameter*>   GetBoolParameters() const { return {}; }
		virtual IteratorRange<const IntParameter*>   GetIntParameters() const { return {}; }

        RaiseLowerManipulator(
            std::shared_ptr<SceneEngine::TerrainManager> terrainManager,
            std::shared_ptr<TerrainManipulatorContext> manipulatorContext);
    private:
        float _powerValue;
    };

    void    RaiseLowerManipulator::PerformAction(const Float3& worldSpacePosition, float size, float strength)
    {
        if (_manipulatorContext->_activeLayer != SceneEngine::CoverageId_Heights)
            Throw(::Exceptions::BasicLabel(HeightsLayerError));

            //
            //      Use the uber surface interface to change these values
            //          -- this will make sure all of the cells get updated as needed
            //
        auto *i = _terrainManager->GetHeightsInterface();
        if (i) {
            auto result = i->AdjustHeights(
                *RenderCore::Techniques::GetThreadContext(),
                WorldSpaceToTerrain(Truncate(worldSpacePosition)), 
                WorldSpaceDistanceToTerrainCoords(size), 
                .05f * strength, _powerValue);

            if (result != SceneEngine::TerrainToolResult::Success)
                Throw(TerrainManipulatorException(result));
        }
    }

    auto RaiseLowerManipulator::GetFloatParameters() const -> IteratorRange<const FloatParameter*>
    {
        static FloatParameter parameters[] = 
        {
			FloatParameter{ManipulatorParameterOffset(&RaiseLowerManipulator::_strength), 1.f, 100.f, FloatParameter::ScaleType::Logarithmic, "Strength"},
			FloatParameter{ManipulatorParameterOffset(&RaiseLowerManipulator::_size), 0.1f, 500.f, FloatParameter::ScaleType::Linear, "Size"},
			FloatParameter{ManipulatorParameterOffset(&RaiseLowerManipulator::_powerValue), 1.f/8.f, 8.f, FloatParameter::ScaleType::Linear, "ShapeControl"}
        };
        return MakeIteratorRange(parameters);
    }

    RaiseLowerManipulator::RaiseLowerManipulator(
        std::shared_ptr<SceneEngine::TerrainManager> terrainManager, 
        std::shared_ptr<TerrainManipulatorContext> manipulatorContext)
    : CommonManipulator(std::move(terrainManager), std::move(manipulatorContext))
    {
        _powerValue = 1.f/8.f;
		_strength = 10.f;
    }

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

    class SmoothManipulator : public CommonManipulator
    {
    public:
        virtual void    PerformAction(const Float3& worldSpacePosition, float size, float strength);
        virtual const char* GetName() const { return "Smooth"; }

        virtual IteratorRange<const FloatParameter*>  GetFloatParameters() const;
        virtual IteratorRange<const BoolParameter*>   GetBoolParameters() const;
		virtual IteratorRange<const IntParameter*>   GetIntParameters() const { return {}; }

        SmoothManipulator(
            std::shared_ptr<SceneEngine::TerrainManager> terrainManager,
            std::shared_ptr<TerrainManipulatorContext> manipulatorContext);
    private:
        float _standardDeviation;
        float _filterRadius;
        unsigned _flags;
    };

    void    SmoothManipulator::PerformAction(const Float3& worldSpacePosition, float size, float strength)
    {
        if (_manipulatorContext->_activeLayer != SceneEngine::CoverageId_Heights)
            Throw(::Exceptions::BasicLabel(HeightsLayerError));

        auto *i = _terrainManager->GetHeightsInterface();
        if (i) {
            auto result = i->Smooth(
                *RenderCore::Techniques::GetThreadContext(),
                WorldSpaceToTerrain(Truncate(worldSpacePosition)), 
                WorldSpaceDistanceToTerrainCoords(size),
                unsigned(_filterRadius), 
                _standardDeviation, _strength, _flags);

            if (result != SceneEngine::TerrainToolResult::Success)
                Throw(TerrainManipulatorException(result));
        }
    }

    auto SmoothManipulator::GetFloatParameters() const -> IteratorRange<const FloatParameter*>
    {
        static FloatParameter parameters[] = 
        {
			FloatParameter{ManipulatorParameterOffset(&SmoothManipulator::_size), 0.1f, 500.f, FloatParameter::ScaleType::Linear, "Size"},
            FloatParameter{ManipulatorParameterOffset(&SmoothManipulator::_standardDeviation), 1.f, 6.f, FloatParameter::ScaleType::Linear, "Blurriness"},
            FloatParameter{ManipulatorParameterOffset(&SmoothManipulator::_filterRadius), 2.f, 16.f, FloatParameter::ScaleType::Linear, "FilterRadius"},
            FloatParameter{ManipulatorParameterOffset(&SmoothManipulator::_strength), 0.01f, 1.f, FloatParameter::ScaleType::Linear, "Strength"}
        };
        return MakeIteratorRange(parameters);
    }

    auto SmoothManipulator::GetBoolParameters() const -> IteratorRange<const BoolParameter*>
    {
        static BoolParameter parameters[] = 
        {
			BoolParameter{ManipulatorParameterOffset(&SmoothManipulator::_flags), 0, "SmoothUp"},
			BoolParameter{ManipulatorParameterOffset(&SmoothManipulator::_flags), 1, "SmoothDown"}
        };
        return MakeIteratorRange(parameters);
    }

    SmoothManipulator::SmoothManipulator(
        std::shared_ptr<SceneEngine::TerrainManager> terrainManager, 
        std::shared_ptr<TerrainManipulatorContext> manipulatorContext)
    : CommonManipulator(std::move(terrainManager), std::move(manipulatorContext))
    {
        _standardDeviation = 3.f;
        _filterRadius = 16.f;
        _flags = 0x3;
    }

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

    class NoiseManipulator : public CommonManipulator
    {
    public:
        virtual void    PerformAction(const Float3& worldSpacePosition, float size, float strength);
        virtual const char* GetName() const { return "Add Noise"; }

        virtual IteratorRange<const FloatParameter*>  GetFloatParameters() const;
		virtual IteratorRange<const BoolParameter*>   GetBoolParameters() const { return {}; }
		virtual IteratorRange<const IntParameter*>   GetIntParameters() const { return {}; }

        NoiseManipulator(std::shared_ptr<SceneEngine::TerrainManager> terrainManager, std::shared_ptr<TerrainManipulatorContext> manipulatorContext);
    };

    void    NoiseManipulator::PerformAction(const Float3& worldSpacePosition, float size, float strength)
    {
        if (_manipulatorContext->_activeLayer != SceneEngine::CoverageId_Heights)
            Throw(::Exceptions::BasicLabel(HeightsLayerError));

        auto *i = _terrainManager->GetHeightsInterface();
        if (i) {
            auto result = i->AddNoise(
                *RenderCore::Techniques::GetThreadContext(),
                WorldSpaceToTerrain(Truncate(worldSpacePosition)), 
                WorldSpaceDistanceToTerrainCoords(size), 
                .05f * strength);

            if (result != SceneEngine::TerrainToolResult::Success)
                Throw(TerrainManipulatorException(result));
        }
    }

    auto NoiseManipulator::GetFloatParameters() const -> IteratorRange<const FloatParameter*>
    {
        static FloatParameter parameters[] = 
        {
			FloatParameter{ManipulatorParameterOffset(&NoiseManipulator::_strength), 1.f, 100.f, FloatParameter::ScaleType::Logarithmic, "Strength"},
			FloatParameter{ManipulatorParameterOffset(&NoiseManipulator::_size), 0.1f, 500.f, FloatParameter::ScaleType::Linear, "Size"},
        };
        return MakeIteratorRange(parameters);
    }

    NoiseManipulator::NoiseManipulator(
        std::shared_ptr<SceneEngine::TerrainManager> terrainManager, 
        std::shared_ptr<TerrainManipulatorContext> manipulatorContext)
    : CommonManipulator(std::move(terrainManager), std::move(manipulatorContext))
    {
		_strength = 7.f;
	}

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

    class CopyHeight : public CommonManipulator
    {
    public:
        virtual void    PerformAction(const Float3& worldSpacePosition, float size, float strength);
        virtual const char* GetName() const { return "Copy Height"; }

        virtual IteratorRange<const FloatParameter*>  GetFloatParameters() const;
        virtual IteratorRange<const BoolParameter*>   GetBoolParameters() const;
		virtual IteratorRange<const IntParameter*>   GetIntParameters() const { return {}; }

        CopyHeight(std::shared_ptr<SceneEngine::TerrainManager> terrainManager, std::shared_ptr<TerrainManipulatorContext> manipulatorContext);
    private:
        float _powerValue;
        unsigned _flags;
    };

    void CopyHeight::PerformAction(const Float3& worldSpacePosition, float size, float strength)
    {
        if (_manipulatorContext->_activeLayer != SceneEngine::CoverageId_Heights)
            Throw(::Exceptions::BasicLabel(HeightsLayerError));

        auto *i = _terrainManager->GetHeightsInterface();
        if (i && _targetOnMouseDown.second) {
            auto result = i->CopyHeight(
                *RenderCore::Techniques::GetThreadContext(),
                WorldSpaceToTerrain(Truncate(worldSpacePosition)), 
                WorldSpaceToTerrain(Truncate(_targetOnMouseDown.first)), 
                WorldSpaceDistanceToTerrainCoords(size), 
                strength, _powerValue, _flags);

            if (result != SceneEngine::TerrainToolResult::Success)
                Throw(TerrainManipulatorException(result));
        }
    }

    auto CopyHeight::GetFloatParameters() const -> IteratorRange<const FloatParameter*>
    {
        static FloatParameter parameters[] = 
        {
			FloatParameter{ManipulatorParameterOffset(&CopyHeight::_strength), 1.f, 100.f, FloatParameter::ScaleType::Logarithmic, "Strength"},
			FloatParameter{ManipulatorParameterOffset(&CopyHeight::_size), 0.1f, 500.f, FloatParameter::ScaleType::Linear, "Size"},
			FloatParameter{ManipulatorParameterOffset(&CopyHeight::_powerValue), 1.f/8.f, 8.f, FloatParameter::ScaleType::Linear, "ShapeControl"}
        };
        return MakeIteratorRange(parameters);
    }

    auto CopyHeight::GetBoolParameters() const -> IteratorRange<const BoolParameter*>
    {
        static BoolParameter parameters[] = 
        {
			BoolParameter{ManipulatorParameterOffset(&CopyHeight::_flags), 0, "MoveUp"},
			BoolParameter{ManipulatorParameterOffset(&CopyHeight::_flags), 1, "MoveDown"}
        };
        return MakeIteratorRange(parameters);
    }

    CopyHeight::CopyHeight(
        std::shared_ptr<SceneEngine::TerrainManager> terrainManager, 
        std::shared_ptr<TerrainManipulatorContext> manipulatorContext)
    : CommonManipulator(std::move(terrainManager), std::move(manipulatorContext))
    {
        _powerValue = 1.f/8.f;
        _flags = 0x3;
		_strength = 10.f;
    }

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

    class FineTuneManipulator : public TerrainManipulatorBase
    {
    public:
        virtual bool    OnInputEvent(
            const OSServices::InputSnapshot& evnt, 
            const SceneEngine::IntersectionTestContext& hitTestContext) override;
        virtual void    Render(RenderCore::IThreadContext& context, RenderCore::Techniques::ParsingContext& parserContext);

        virtual const char* GetName() const { return "Fine Tune"; }
        virtual std::string GetStatusText() const { return std::string(); }

        virtual IteratorRange<const FloatParameter*>  GetFloatParameters() const; 
        virtual IteratorRange<const BoolParameter*>   GetBoolParameters() const;
        virtual IteratorRange<const IntParameter*>   GetIntParameters() const;
        virtual void SetActivationState(bool newState);

        FineTuneManipulator(
            std::shared_ptr<SceneEngine::TerrainManager> terrainManager,
            std::shared_ptr<TerrainManipulatorContext> manipulatorContext);

    private:
        std::pair<Float3, bool> _targetPoint;
        float _radius;
        bool _draggingManipulator;
    };

    static const float s_manipulatorCylinderRadius = 0.125f;

    bool FineTuneManipulator::OnInputEvent(
        const OSServices::InputSnapshot& evnt, 
        const SceneEngine::IntersectionTestContext& hitTestContext)
    {
        auto hitTestScene = hitTestContext.GetService<SceneEngine::IIntersectionScene>();
		if (!hitTestScene) return false;

        if (evnt._wheelDelta) {
            float roundingPoint = .5f;
            float wheelAdjustment = XlCeil(_radius / 8.f / roundingPoint) * roundingPoint;
            _radius = std::max(0.5f, _radius + wheelAdjustment * evnt._wheelDelta / 120.f);
        }

        auto pixelRay = hitTestContext.CalculateWorldSpaceRay(evnt._mousePosition);
        auto cylinderRay = std::make_pair(Expand(Truncate(_targetPoint.first), 0.f), Expand(Truncate(_targetPoint.first), 1000.f));

        if (evnt.IsPress_LButton()) {
            // If we click on the main manipulator cylinder, we should start a drag operation
            // Otherwise, we should try to reposition the target point
            bool isClickOnManipulatorCylinder = false;
            if (_targetPoint.second) {
                float muA, muB;
                bool foundResult = ShortestSegmentBetweenLines(muA, muB, cylinderRay, pixelRay);
                if (foundResult && muB > 0.f && muB < 1.f &&
                    MagnitudeSquared(LinearInterpolate(cylinderRay.first, cylinderRay.second, muA) - LinearInterpolate(pixelRay.first, pixelRay.second, muB)) < (s_manipulatorCylinderRadius*s_manipulatorCylinderRadius)) {
                    isClickOnManipulatorCylinder = true;
                }
            }

            if (isClickOnManipulatorCylinder) {
                _draggingManipulator = true;
            } else {
                _targetPoint = FindTerrainIntersection(hitTestContext, *hitTestScene, evnt._mousePosition);
                _draggingManipulator = false;
            }
            return true;
        } else if (evnt.IsHeld_LButton() && _draggingManipulator && _targetPoint.second) {
            // drag the manipulator up or down, and apply the changes to the terrain
            float muA, muB;
            bool foundResult = ShortestSegmentBetweenLines(muA, muB, cylinderRay, pixelRay);
            if (foundResult && muB > 0.f && muB < 1.f) {
                float adjustmentHeightValue = LinearInterpolate(cylinderRay.first, cylinderRay.second, muA)[2];

                auto *i = _terrainManager->GetHeightsInterface();
                if (i) {
                    auto result = i->FineTune(
                        *RenderCore::Techniques::GetThreadContext(),
                        Truncate(_targetPoint.first), _radius, adjustmentHeightValue);

                    if (result != SceneEngine::TerrainToolResult::Success)
                        Throw(TerrainManipulatorException(result));
                }
            }
            return true;
        }

        return false;
    }

    void FineTuneManipulator::Render(RenderCore::IThreadContext& context, RenderCore::Techniques::ParsingContext& parserContext)
    {
        TerrainManipulatorBase::Render(context, parserContext);
        if (_targetPoint.second) {
            RenderCylinderHighlight(context, parserContext, _targetPoint.first, _radius);
            DrawWorldSpaceCylinder(context, parserContext, _targetPoint.first,
                Float3(0.f, 0.f, 1000.f), s_manipulatorCylinderRadius);
        }
    }

    auto FineTuneManipulator::GetFloatParameters() const -> IteratorRange<const FloatParameter*>
    {
        static FloatParameter parameters[] = 
        {
			FloatParameter{ManipulatorParameterOffset(&FineTuneManipulator::_radius), 0.5f, 40.f, FloatParameter::ScaleType::Logarithmic, "Radius"},
        };
        return MakeIteratorRange(parameters);
    }

    auto FineTuneManipulator::GetBoolParameters() const -> IteratorRange<const BoolParameter*>
    {
		return {};
    }

    auto FineTuneManipulator::GetIntParameters() const -> IteratorRange<const IntParameter*>
    {
		return {};
    }

    void FineTuneManipulator::SetActivationState(bool newState) { _draggingManipulator = false; }

    FineTuneManipulator::FineTuneManipulator(
        std::shared_ptr<SceneEngine::TerrainManager> terrainManager,
        std::shared_ptr<TerrainManipulatorContext> manipulatorContext) 
    : TerrainManipulatorBase(terrainManager, manipulatorContext)
    , _draggingManipulator(false)
    , _radius(3.f)
    { }

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

    class FillNoiseManipulator : public RectangleManipulator
    {
    public:
        virtual void    PerformAction(const Float3& anchor0, const Float3& anchor1);
        virtual const char* GetName() const { return "Fill noise"; }
        virtual IteratorRange<const FloatParameter*>  GetFloatParameters() const;
		virtual IteratorRange<const BoolParameter*>   GetBoolParameters() const { return {}; }
		virtual IteratorRange<const IntParameter*>   GetIntParameters() const { return {}; }

        FillNoiseManipulator(std::shared_ptr<SceneEngine::TerrainManager> terrainManager, std::shared_ptr<TerrainManipulatorContext> manipulatorContext);

    private:
        float _baseHeight, _noiseHeight, _roughness, _fractalDetail;
    };

    void    FillNoiseManipulator::PerformAction(const Float3& anchor0, const Float3& anchor1)
    {
        if (_manipulatorContext->_activeLayer != SceneEngine::CoverageId_Heights)
            Throw(::Exceptions::BasicLabel(HeightsLayerError));

        auto *i = _terrainManager->GetHeightsInterface();
        if (i) {
            auto result = i->FillWithNoise(
                *RenderCore::Techniques::GetThreadContext(),
                WorldSpaceToTerrain(Truncate(anchor0)), 
                WorldSpaceToTerrain(Truncate(anchor1)), 
                _baseHeight, _noiseHeight, _roughness, _fractalDetail);

            if (result != SceneEngine::TerrainToolResult::Success)
                Throw(TerrainManipulatorException(result));
        }
    }

    auto FillNoiseManipulator::GetFloatParameters() const -> IteratorRange<const FloatParameter*>
    {
        static FloatParameter parameters[] = 
        {
			FloatParameter{ManipulatorParameterOffset(&FillNoiseManipulator::_baseHeight), 0.1f, 1000.f, FloatParameter::ScaleType::Logarithmic, "BaseHeight"},
			FloatParameter{ManipulatorParameterOffset(&FillNoiseManipulator::_noiseHeight), 0.1f, 2000.f, FloatParameter::ScaleType::Logarithmic, "NoiseHeight"},
			FloatParameter{ManipulatorParameterOffset(&FillNoiseManipulator::_roughness), 10.f, 1000.f, FloatParameter::ScaleType::Logarithmic, "Roughness"},
			FloatParameter{ManipulatorParameterOffset(&FillNoiseManipulator::_fractalDetail), 0.1f, 0.9f, FloatParameter::ScaleType::Linear, "FractalDetail"}
        };
        return MakeIteratorRange(parameters);
    }

    FillNoiseManipulator::FillNoiseManipulator(
        std::shared_ptr<SceneEngine::TerrainManager> terrainManager, 
        std::shared_ptr<TerrainManipulatorContext> manipulatorContext)
    : RectangleManipulator(std::move(terrainManager), std::move(manipulatorContext))
    {
        _baseHeight = 250.0f;
        _noiseHeight = 500.f;
        _roughness = 250.f;
        _fractalDetail = 0.5f;
    }

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

    class PaintCoverageManipulator : public CommonManipulator
    {
    public:
        virtual void PerformAction(const Float3& worldSpacePosition, float size, float strength);
        virtual const char* GetName() const { return "Paint Coverage"; }
        virtual IteratorRange<const FloatParameter*>  GetFloatParameters() const;
		virtual IteratorRange<const BoolParameter*>   GetBoolParameters() const { return {}; }
        virtual IteratorRange<const IntParameter*>   GetIntParameters() const;
        virtual void SetActivationState(bool newState);
		virtual void Render(RenderCore::IThreadContext& context, RenderCore::Techniques::ParsingContext& parserContext);

        PaintCoverageManipulator(std::shared_ptr<SceneEngine::TerrainManager> terrainManager, std::shared_ptr<TerrainManipulatorContext> manipulatorContext);

    private:
        unsigned _paintValue;
    };

    void PaintCoverageManipulator::PerformAction(const Float3& worldSpacePosition, float size, float strength)
    {
		auto coverageLayer = _manipulatorContext->_activeLayer;
		if (coverageLayer == SceneEngine::CoverageId_Heights)
			Throw(::Exceptions::BasicLabel("Select a valid coverage layer. Currently the heights layer is selected"));

        auto* i = _terrainManager->GetCoverageInterface(coverageLayer);
        if (i) {
            auto result = i->Paint(
                *RenderCore::Techniques::GetThreadContext(),
                WorldSpaceToCoverage(coverageLayer, Truncate(worldSpacePosition)), 
                WorldSpaceToCoverageDistance(coverageLayer, size),
                _paintValue);

            if (result != SceneEngine::TerrainToolResult::Success)
                Throw(TerrainManipulatorException(result));
        } else {
            Throw(::Exceptions::BasicLabel("Could not lock this coverage layer"));
        }
    }

    auto PaintCoverageManipulator::GetIntParameters() const -> IteratorRange<const IntParameter*>
    {
        static IntParameter parameters[] = 
        {
			IntParameter{ManipulatorParameterOffset(&PaintCoverageManipulator::_paintValue), 0i32, INT32_MAX, IntParameter::ScaleType::Linear, "PaintValue"}
        };
        return MakeIteratorRange(parameters);
    }

    auto PaintCoverageManipulator::GetFloatParameters() const -> IteratorRange<const FloatParameter*>
    {
        static FloatParameter parameters[] = 
        {
			FloatParameter{ManipulatorParameterOffset(&PaintCoverageManipulator::_size), 0.1f, 500.f, FloatParameter::ScaleType::Linear, "Size"}
        };
        return MakeIteratorRange(parameters);
    }

    void PaintCoverageManipulator::SetActivationState(bool newState)
    {
		auto coverageLayer = _manipulatorContext->_activeLayer;
		if (coverageLayer == SceneEngine::CoverageId_Heights)
			coverageLayer = 0;
		if (!_manipulatorContext->_showCoverage)
			coverageLayer = 0;
        Tweakable("TerrainVisCoverage", 0) = newState ? coverageLayer : 0;
    }

	void PaintCoverageManipulator::Render(RenderCore::IThreadContext& context, RenderCore::Techniques::ParsingContext& parserContext)
	{
		auto coverageLayer = _manipulatorContext->_activeLayer;
		if (coverageLayer == SceneEngine::CoverageId_Heights)
			coverageLayer = 0;

		// Update the tweakable hack with the coverage layer we're looking at
		// It's a bit of a hack. Ideally we need a mechanism for rendering a 
		// second "visualization" pass over the terrain, and use the _manipulatorContext
		// to drive that. However, that is pending improvements to the ISceneParser
		// interface for a more formal "scene prepare" step... So just a simple (but
		// robust) hack for now.
		if (_manipulatorContext->_showCoverage) {
			Tweakable("TerrainVisCoverage", 0) = coverageLayer;
		} else 
			Tweakable("TerrainVisCoverage", 0) = 0;

		CommonManipulator::Render(context, parserContext);
	}

    PaintCoverageManipulator::PaintCoverageManipulator(
        std::shared_ptr<SceneEngine::TerrainManager> terrainManager, 
        std::shared_ptr<TerrainManipulatorContext> manipulatorContext)
    : CommonManipulator(std::move(terrainManager), std::move(manipulatorContext))
    {
        _paintValue = 1;
    }

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

    class ErosionManipulator : public RectangleManipulator
    {
    public:
        virtual void            PerformAction(const Float3& anchor0, const Float3& anchor1);
        virtual const char*     GetName() const { return "Erosion simulation"; }
        virtual void            Render(RenderCore::IThreadContext& context, RenderCore::Techniques::ParsingContext& parserContext);

        virtual IteratorRange<const FloatParameter*>  GetFloatParameters() const;
        virtual IteratorRange<const BoolParameter*>   GetBoolParameters() const;
		virtual IteratorRange<const IntParameter*>   GetIntParameters() const { return {}; }
        
        ErosionManipulator(std::shared_ptr<SceneEngine::TerrainManager> terrainManager, std::shared_ptr<TerrainManipulatorContext> manipulatorContext);

    private:
        Float2  _activeMins;
        Float2  _activeMaxs;
        SceneEngine::ErosionSimulation::Settings _params;
        unsigned _flags;

        size_t OffsetOf(const void* member) const;
    };

    void    ErosionManipulator::PerformAction(const Float3& anchor0, const Float3& anchor1)
    {
        auto *i = _terrainManager->GetHeightsInterface();
        if (i) {
            _activeMins = WorldSpaceToTerrain(Truncate(anchor0));
            _activeMaxs = WorldSpaceToTerrain(Truncate(anchor1));
            _flags = _flags & ~(1<<0);    // start inactive
            i->Erosion_End();
        }
    }

    void    ErosionManipulator::Render(RenderCore::IThreadContext& context, RenderCore::Techniques::ParsingContext& parserContext)
    {
        RectangleManipulator::Render(context, parserContext);

            //  Doing the erosion tick here is most convenient (because this is the 
            //  only place we get regular updates).
        if (_flags & (1<<0)) {
            auto *i = _terrainManager->GetHeightsInterface();
            if (i) {
                if (_activeMaxs[0] > _activeMins[0] && _activeMaxs[1] > _activeMins[1]) {
                    if (!i->Erosion_IsPrepared()) {
                        i->Erosion_Begin(context, _activeMins, _activeMaxs, _terrainManager->GetConfig());
                    }

                    i->Erosion_Tick(context, _params);
                }
            }
        }

        if (_flags & (1<<1)) {
            auto *i = _terrainManager->GetHeightsInterface();
            if (i && i->Erosion_IsPrepared()) {
                i->Erosion_RenderDebugging(context, parserContext, _terrainManager->GetCoords());
            }
        }
    }

    size_t ErosionManipulator::OffsetOf(const void* member) const
    {
        auto offset = size_t(member) - size_t((const IManipulator*)this);
        assert(offset < sizeof(*this));
        return offset;
    }

    auto ErosionManipulator::GetFloatParameters() const -> IteratorRange<const FloatParameter*>
    {
        static FloatParameter parameters[] = 
        {
            FloatParameter{OffsetOf(&_params._rainQuantityPerFrame), 0.f, 1.f, FloatParameter::ScaleType::Linear, "Rain quantity per frame"},
            FloatParameter{OffsetOf(&_params._evaporationConstant), 0.f, 1.f, FloatParameter::ScaleType::Linear, "Evaporation constant"},
            FloatParameter{OffsetOf(&_params._pressureConstant), 0.f, 1.f, FloatParameter::ScaleType::Linear, "Pressure constant"},

            FloatParameter{OffsetOf(&_params._kConstant), 0.f, 1.f, FloatParameter::ScaleType::Linear, "K constant"},
            FloatParameter{OffsetOf(&_params._erosionRate), 0.f, 1.f, FloatParameter::ScaleType::Linear, "Erosion rate"},
            FloatParameter{OffsetOf(&_params._settlingRate), 0.f, 1.f, FloatParameter::ScaleType::Linear, "Settling rate"},
            FloatParameter{OffsetOf(&_params._maxSediment), 0.f, 1.f, FloatParameter::ScaleType::Linear, "Max sediment"},
            FloatParameter{OffsetOf(&_params._depthMax), 0.f, 1.f, FloatParameter::ScaleType::Linear, "Depth max"},
            FloatParameter{OffsetOf(&_params._sedimentShiftScalar), 0.f, 1.f, FloatParameter::ScaleType::Linear, "Sediment shift scalar"},

            FloatParameter{OffsetOf(&_params._thermalSlopeAngle), 0.f, 1.f, FloatParameter::ScaleType::Linear, "Thermal slope angle"},
			FloatParameter{OffsetOf(&_params._thermalErosionRate), 0.f, 1.f, FloatParameter::ScaleType::Linear, "Thermal erosion rate"}
        };
        return MakeIteratorRange(parameters);
    }

    auto ErosionManipulator::GetBoolParameters() const -> IteratorRange<const BoolParameter*>
    {
        static BoolParameter parameters[] = 
        {
			BoolParameter{ManipulatorParameterOffset(&ErosionManipulator::_flags), 0, "Active"},
			BoolParameter{ManipulatorParameterOffset(&ErosionManipulator::_flags), 1, "Draw water"},
        };
        return MakeIteratorRange(parameters);
    }

    ErosionManipulator::ErosionManipulator(
        std::shared_ptr<SceneEngine::TerrainManager> terrainManager, 
        std::shared_ptr<TerrainManipulatorContext> manipulatorContext)
    : RectangleManipulator(std::move(terrainManager), std::move(manipulatorContext))
    {
        _flags = 0;
        _activeMins = _activeMaxs = Float2(0.f, 0.f);
    }


    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

    class RotateManipulator : public RectangleManipulator
    {
    public:
        virtual void    PerformAction(const Float3& anchor0, const Float3& anchor1);
        virtual const char* GetName() const { return "Rotate"; }
        virtual IteratorRange<const FloatParameter*>  GetFloatParameters() const;
		virtual IteratorRange<const BoolParameter*>   GetBoolParameters() const { return {}; }
		virtual IteratorRange<const IntParameter*>   GetIntParameters() const { return {}; }
        RotateManipulator(std::shared_ptr<SceneEngine::TerrainManager> terrainManager, std::shared_ptr<TerrainManipulatorContext> manipulatorContext);

    private:
        float _rotationDegrees;
    };

    void    RotateManipulator::PerformAction(const Float3&, const Float3&)
    {
        auto *i = _terrainManager->GetHeightsInterface();
        if (i) {
                // we can't use the parameters passed into this function (because they've
                //  been adjusted to the mins/maxs of a rectangular area, and we've lost
                //  directional information)
            Float2 rotationOrigin = RoundDownToInteger(WorldSpaceToTerrain(Truncate(_firstAnchor)));
            Float2 farPoint = RoundDownToInteger(WorldSpaceToTerrain(Truncate(_secondAnchor.first)));

            float radius = Magnitude(rotationOrigin - farPoint);
            Float2 A(farPoint[0] - rotationOrigin[0], farPoint[1] - rotationOrigin[1]);
            Float3 rotationAxis = Normalize(Float3(A[1], -A[0], 0.f));
            auto result = i->Rotate(*RenderCore::Techniques::GetThreadContext(), rotationOrigin, radius, rotationAxis, float(_rotationDegrees * M_PI / 180.f));
            if (result != SceneEngine::TerrainToolResult::Success)
                throw TerrainManipulatorException(result);
        }
    }

    auto RotateManipulator::GetFloatParameters() const -> IteratorRange<const FloatParameter*>
    {
        static FloatParameter parameters[] = 
        {
			FloatParameter{ManipulatorParameterOffset(&RotateManipulator::_rotationDegrees), 1.0f, 70.f, FloatParameter::ScaleType::Logarithmic, "RotationDegrees"}
        };
        return MakeIteratorRange(parameters);
    }

    RotateManipulator::RotateManipulator(
        std::shared_ptr<SceneEngine::TerrainManager> terrainManager, 
        std::shared_ptr<TerrainManipulatorContext> manipulatorContext)
    : RectangleManipulator(std::move(terrainManager), std::move(manipulatorContext))
    {
        _rotationDegrees = 10.f;
    }

    std::vector<std::unique_ptr<IManipulator>> CreateTerrainManipulators(
        std::shared_ptr<SceneEngine::TerrainManager> terrainManager, 
        std::shared_ptr<TerrainManipulatorContext> manipulatorContext)
    {
        std::vector<std::unique_ptr<IManipulator>> result;
        result.emplace_back(std::make_unique<RaiseLowerManipulator>(terrainManager, manipulatorContext));
        result.emplace_back(std::make_unique<SmoothManipulator>(terrainManager, manipulatorContext));
        result.emplace_back(std::make_unique<NoiseManipulator>(terrainManager, manipulatorContext));
        result.emplace_back(std::make_unique<FillNoiseManipulator>(terrainManager, manipulatorContext));
        result.emplace_back(std::make_unique<CopyHeight>(terrainManager, manipulatorContext));
        result.emplace_back(std::make_unique<FineTuneManipulator>(terrainManager, manipulatorContext));
        result.emplace_back(std::make_unique<RotateManipulator>(terrainManager, manipulatorContext));
        result.emplace_back(std::make_unique<ErosionManipulator>(terrainManager, manipulatorContext));
        result.emplace_back(std::make_unique<PaintCoverageManipulator>(terrainManager, manipulatorContext));
        return result;
    }

    TerrainManipulatorContext::TerrainManipulatorContext()
    {
        _activeLayer = SceneEngine::CoverageId_Heights;
        _showLockedArea = true;
		_showCoverage = true;
    }

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

    const char* TerrainManipulatorException::what() const
    {
        switch (_errorCode) {
        case SceneEngine::TerrainToolResult::OutsideLock:   return "Outside of locked area";
        case SceneEngine::TerrainToolResult::PendingAsset:  return "Shader compile still pending";
        case SceneEngine::TerrainToolResult::InvalidAsset:  return "Shader compile failed";
        default: return "Unknown error";
        }
    }

    SceneEngine::TerrainToolResult TerrainManipulatorException::GetErrorCode() const { return _errorCode; }

    TerrainManipulatorException::TerrainManipulatorException(SceneEngine::TerrainToolResult errorCode)
    : _errorCode(errorCode) {}
    TerrainManipulatorException::~TerrainManipulatorException() {}
}

