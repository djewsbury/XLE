// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "DynamicImpostersDisplay.h"
#include "../../SceneEngine/DynamicImposters.h"
#include "../../SceneEngine/SceneEngineUtils.h"     // (for ShaderBasedCopy)
#include "../../RenderCore/Metal/TextureView.h"
#include "../../RenderCore/Metal/DeviceContext.h"
#include "../../RenderCore/Techniques/CommonResources.h"
#include "../../Utility/FunctionUtils.h"

#include "../../RenderCore/DX11/Metal/DX11Utils.h"

namespace PlatformRig { namespace Overlays
{

    void DynamicImpostersDisplay::Render( 
        IOverlayContext& context, Layout& layout, Interactables& interactables, 
        InterfaceState& interfaceState)
    {
		assert(0);		// cannot access the DeviceContext from IOverlayContext
#if 0
        auto man = _manager.lock();
        if (!man) return;

        const auto metrics = man->GetMetrics();

        Layout statsArea = layout.AllocateFullHeight(300);
        Rect textureArea = layout.AllocateFullHeightFraction(1.f);

        float borderSize = 1.f, roundedProportion = 1.f / 12.f;
        context.DrawQuad(
            ProjectionMode::P2D,
            AsPixelCoords(Coord2(statsArea.GetMaximumSize()._topLeft[0], statsArea.GetMaximumSize()._topLeft[1])),
            AsPixelCoords(Coord2(statsArea.GetMaximumSize()._bottomRight[0], statsArea.GetMaximumSize()._bottomRight[1])),
            ColorB(0x4f7f7f7f), ColorB::White,
            Float2(0.f, 0.f), Float2(1.f, 1.f), Float2(borderSize, roundedProportion), Float2(borderSize, roundedProportion),
            RENDEROVERLAYS_SHAPES_HLSL ":Paint,Shape=RoundedRectShape,Fill=RaisedRefactiveFill,Outline=SolidFill");

        const unsigned lineHeight = 18;
        statsArea.AllocateFullWidth(5 * lineHeight);
        DrawFormatText(
            &context, 
            statsArea.AllocateFullWidth(lineHeight),
            nullptr, ColorB(0xffffffff),
            "SpriteCount: %i/%i", metrics._spriteCount, metrics._maxSpriteCount);
        statsArea.AllocateFullWidth(lineHeight);
        DrawFormatText(
            &context, 
            statsArea.AllocateFullWidth(lineHeight),
            nullptr, ColorB(0xffffffff),
            "Pixels allocated: %.1f%%", 100.f * metrics._pixelsAllocated / float(metrics._pixelsTotal));
        DrawFormatText(
            &context, 
            statsArea.AllocateFullWidth(lineHeight),
            nullptr, ColorB(0xffffffff),
            "Largest free 0: (%i, %i)", metrics._largestFreeBlockArea[0], metrics._largestFreeBlockArea[1]);
        DrawFormatText(
            &context, 
            statsArea.AllocateFullWidth(lineHeight),
            nullptr, ColorB(0xffffffff),
            "Largest free 1: (%i, %i)", metrics._largestFreeBlockSide[0], metrics._largestFreeBlockSide[1]);
        DrawFormatText(
            &context, 
            statsArea.AllocateFullWidth(lineHeight),
            nullptr, ColorB(0xffffffff),
            "Overflow: (%i), Pending: (%i)", metrics._overflowCounter, metrics._pendingCounter);
        DrawFormatText(
            &context, 
            statsArea.AllocateFullWidth(lineHeight),
            nullptr, ColorB(0xffffffff),
            "Evictions: (%i)", metrics._evictionCounter);
        statsArea.AllocateFullWidth(lineHeight);
        DrawFormatText(
            &context, 
            statsArea.AllocateFullWidth(lineHeight),
            nullptr, ColorB(0xffffffff),
            "Bytes/pixel (%i) in (%i) layers", metrics._bytesPerPixel, metrics._layerCount);
        
        UInt2 atlasSize(0,0);
        {
            unsigned visibleLayer = 0;
            context.ReleaseState();
            auto cleanup = MakeAutoCleanup(
                [&context]() {context.CaptureState();});

            auto threadContext = context.GetDeviceContext();
            auto metalContext = RenderCore::Metal::DeviceContext::Get(*threadContext);
            RenderCore::Metal::RenderTargetView rtv(*metalContext);
            auto srv = man->GetAtlasResource(visibleLayer);
            auto desc = RenderCore::Metal::ExtractDesc(srv);
            metalContext->Bind(RenderCore::Techniques::CommonResources()._blendOneSrcAlpha);
            SceneEngine::ShaderBasedCopy(
                *metalContext, rtv, srv,
                std::make_pair(textureArea._topLeft, textureArea._bottomRight),
                std::make_pair(UInt2(0,0), UInt2(desc._textureDesc._width, desc._textureDesc._height)),
                SceneEngine::CopyFilter::Bilinear,
                SceneEngine::ProtectState::States::RenderTargets);
            atlasSize = UInt2(desc._textureDesc._width, desc._textureDesc._height);
        }

        {
                // Draw rectangles showing the "heat-map" for the sprites
                // This is how long it's been since this sprite has been used
            for (unsigned sprite=0; sprite<metrics._maxSpriteCount; ++sprite) {
                auto sm = man->GetSpriteMetrics(sprite);
                for (unsigned m=0; m<dimof(sm._mipMaps); ++m) {
                    if (sm._mipMaps[m].second[0] > sm._mipMaps[m].first[0]) {
                        const auto& r = sm._mipMaps[m];
                        UInt2 topLeft(
                            (unsigned)LinearInterpolate(textureArea._topLeft[0], textureArea._bottomRight[0], r.first[0] / float(atlasSize[0])),
                            (unsigned)LinearInterpolate(textureArea._topLeft[1], textureArea._bottomRight[1], r.first[1] / float(atlasSize[1])));
                        UInt2 bottomRight(
                            (unsigned)LinearInterpolate(textureArea._topLeft[0], textureArea._bottomRight[0], r.second[0] / float(atlasSize[0])),
                            (unsigned)LinearInterpolate(textureArea._topLeft[1], textureArea._bottomRight[1], r.second[1] / float(atlasSize[1])));
                            
                        float age = Clamp(sm._timeSinceUsage/60.f, 0.f, 1.f);
                        OutlineRoundedRectangle(
                            &context, Rect(topLeft, bottomRight),
                            ColorB::FromNormalized(LinearInterpolate(0.f, 1.f, age), 0.f, LinearInterpolate(1.f, 0.f, age), 1.f),
                            2.f, 0.05f);
                    }
                }
            }
        }
#endif
    }

    DynamicImpostersDisplay::DynamicImpostersDisplay(
        std::weak_ptr<SceneEngine::DynamicImposters> manager)
    : _manager(std::move(manager))
    {

    }

    DynamicImpostersDisplay::~DynamicImpostersDisplay() {}

}}

