// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "ShadowFrustumDebugger.h"
#include "../IOverlayContext.h"
#include "../../SceneEngine/SceneParser.h"
#include "../../SceneEngine/RenderStep_PrepareShadows.h"
#include "../../SceneEngine/LightingParser.h"
#include "../../SceneEngine/LightingParserContext.h"
#include "../../RenderCore/Techniques/CommonResources.h"
#include "../../RenderCore/Metal/DeviceContext.h"
#include "../../Math/Transformations.h"
#include "../../ConsoleRig/Console.h"

namespace Overlays
{
    using namespace RenderOverlays;
    using namespace RenderCore;

    void ShadowFrustumDebugger::Render( 
        IOverlayContext& context, Layout& layout, 
        Interactables& interactables, InterfaceState& interfaceState)
    {
		assert(0);	// this is broken because we can no longer access the projectionDesc, etc, via IOverlayContext
#if 0
        assert(_scene.get());

        if (!_scene->GetShadowProjectionCount()) {
            return;
        }

		RenderCore::Techniques::ParsingContext& parserContext = /* ... */;
        static SceneEngine::StandardShadowProjection projectionDesc;
        if (!Tweakable("ShadowDebugLock", false)) {
            projectionDesc = _scene->GetShadowProjectionDesc(0, context.GetProjectionDesc());
        }

        auto devContext = Metal::DeviceContext::Get(*context.GetDeviceContext());
        context.ReleaseState();
        SceneEngine::ShadowGen_DrawShadowFrustums(
            *devContext, context, SceneEngine::MainTargets{},
			projectionDesc);
        context.CaptureState();
        
            //  Get the first shadow projection from the scene, and draw an
            //  outline of all sub-projections with in.
            //  We could also add a control to select different projections
            //  when there are more than one...
        devContext->Bind(Techniques::CommonResources()._dssReadOnly);

        ColorB cols[]= {
            ColorB(196, 230, 230),
            ColorB(255, 128, 128),
            ColorB(128, 255, 128),
            ColorB(128, 128, 255),
            ColorB(255, 255, 128),
            ColorB(128, 255, 255)
        };

        const auto& projections = projectionDesc._projections;
        for (unsigned c=0; c<projections._normalProjCount; ++c) {
            DebuggingDisplay::DrawFrustum(
                &context, Combine(projections._fullProj[c]._viewMatrix, projections._fullProj[c]._projectionMatrix),
                cols[std::min((unsigned)dimof(cols), c)], 0x1);
        }

        if (projections._useNearProj) {
            DebuggingDisplay::DrawFrustum(
                &context, projections._specialNearProjection,
                cols[std::min((unsigned)dimof(cols), projections._normalProjCount)], 0x1);
        }

        for (unsigned c=0; c<projections._normalProjCount; ++c) {
            DebuggingDisplay::DrawFrustum(
                &context, Combine(projections._fullProj[c]._viewMatrix, projections._fullProj[c]._projectionMatrix),
                cols[std::min((unsigned)dimof(cols), c)], 0x2);
        }

        if (projections._useNearProj) {
            DebuggingDisplay::DrawFrustum(
                &context, projections._specialNearProjection,
                cols[std::min((unsigned)dimof(cols), projections._normalProjCount)], 0x2);
        }
#endif
    }

    ShadowFrustumDebugger::ShadowFrustumDebugger(std::shared_ptr<SceneEngine::ILightingStateDelegate> scene)
    : _scene(std::move(scene))
    {}

    ShadowFrustumDebugger::~ShadowFrustumDebugger()
    {
    }

}

