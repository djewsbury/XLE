// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "ShapesRendering.h"
#include "FontRendering.h"
#include "OverlayContext.h"
#include "DrawText.h"
#include "ShapesInternal.h"
#include "../RenderCore/Techniques/ImmediateDrawables.h"
#include "../RenderCore/Techniques/CommonBindings.h"
#include "../RenderCore/Techniques/TechniqueDelegates.h"
#include "../RenderCore/Techniques/PipelineAccelerator.h"
#include "../RenderCore/Techniques/PipelineLayoutDelegate.h"
#include "../RenderCore/Techniques/CommonResources.h"
#include "../RenderCore/Assets/PredefinedPipelineLayout.h"
#include "../RenderCore/Assets/RawMaterial.h"
#include "../RenderCore/Format.h"
#include "../RenderCore/UniformsStream.h"
#include "../Tools/EntityInterface/MountedData.h"
#include "../Formatters/IDynamicFormatter.h"
#include "../Formatters/FormatterUtils.h"
#include "../Math/Geometry.h"
#include "../ConsoleRig/ResourceBox.h"
#include "../Assets/Continuation.h"
#include "../Assets/Marker.h"
#include "../Assets/Assets.h"
#include "../xleres/FileList.h"

using namespace Utility::Literals;

namespace RenderOverlays
{
    class StandardResources
    {
    public:
        RenderCore::Techniques::ImmediateDrawableMaterial _fillRoundedRect;
        RenderCore::Techniques::ImmediateDrawableMaterial _outlineRoundedRect;
        RenderCore::Techniques::ImmediateDrawableMaterial _fillRaisedRect;
        RenderCore::Techniques::ImmediateDrawableMaterial _fillAndOutlineRoundedRect;
        RenderCore::Techniques::ImmediateDrawableMaterial _fillRaisedRoundedRect;
        RenderCore::Techniques::ImmediateDrawableMaterial _fillReverseRaisedRoundedRect;
        RenderCore::Techniques::ImmediateDrawableMaterial _fillEllipse;
        RenderCore::Techniques::ImmediateDrawableMaterial _outlineEllipse;
        RenderCore::Techniques::ImmediateDrawableMaterial _softShadowRect;
        RenderCore::Techniques::ImmediateDrawableMaterial _dashLine;
        RenderCore::Techniques::ImmediateDrawableMaterial _solidNoBorder;
        RenderCore::Techniques::ImmediateDrawableMaterial _fillColorAdjust;
        RenderCore::UniformsStreamInterface _roundedRectUSI;
        RenderCore::UniformsStreamInterface _colorAdjustUSI;

        const ::Assets::DependencyValidation& GetDependencyValidation() const { return _depVal; };
        ::Assets::DependencyValidation _depVal;

        StandardResources(
            const RenderCore::Assets::ResolvedMaterial& fillRoundedRect,
            const RenderCore::Assets::ResolvedMaterial& fillAndOutlineRoundedRect,
            const RenderCore::Assets::ResolvedMaterial& outlineRoundedRect,
            const RenderCore::Assets::ResolvedMaterial& fillRaisedRect,
            const RenderCore::Assets::ResolvedMaterial& fillRaisedRoundedRect,
            const RenderCore::Assets::ResolvedMaterial& fillReverseRaisedRoundedRect,
            const RenderCore::Assets::ResolvedMaterial& fillEllipse,
            const RenderCore::Assets::ResolvedMaterial& outlineEllipse,
            const RenderCore::Assets::ResolvedMaterial& softShadowRect,
            const RenderCore::Assets::ResolvedMaterial& dashLine,
            const RenderCore::Assets::ResolvedMaterial& solidNoBorder,
            const RenderCore::Assets::ResolvedMaterial& fillColorAdjust)
        {
            _fillRoundedRect = BuildImmediateDrawableMaterial(fillRoundedRect);
            _fillAndOutlineRoundedRect = BuildImmediateDrawableMaterial(fillAndOutlineRoundedRect);
            _outlineRoundedRect = BuildImmediateDrawableMaterial(outlineRoundedRect);
            _fillRaisedRect = BuildImmediateDrawableMaterial(fillRaisedRect);
            _fillRaisedRoundedRect = BuildImmediateDrawableMaterial(fillRaisedRoundedRect);
            _fillReverseRaisedRoundedRect = BuildImmediateDrawableMaterial(fillReverseRaisedRoundedRect);
            _fillEllipse = BuildImmediateDrawableMaterial(fillEllipse);
            _outlineEllipse = BuildImmediateDrawableMaterial(outlineEllipse);
            _softShadowRect = BuildImmediateDrawableMaterial(softShadowRect);
            _dashLine = BuildImmediateDrawableMaterial(dashLine);
            _solidNoBorder = BuildImmediateDrawableMaterial(solidNoBorder);
            _fillColorAdjust = BuildImmediateDrawableMaterial(fillColorAdjust);

            _roundedRectUSI.BindImmediateData(0, "RoundedRectSettings"_h);
            _fillRoundedRect._uniformStreamInterface = &_roundedRectUSI;
            _fillAndOutlineRoundedRect._uniformStreamInterface = &_roundedRectUSI;
            _outlineRoundedRect._uniformStreamInterface = &_roundedRectUSI;
            _fillRaisedRoundedRect._uniformStreamInterface = &_roundedRectUSI;
            _fillReverseRaisedRoundedRect._uniformStreamInterface = &_roundedRectUSI;

            _colorAdjustUSI.BindImmediateData(0, "ColorAdjustSettings"_h);
            _colorAdjustUSI.BindResourceView(0, "DiffuseTexture"_h);
            _fillColorAdjust._uniformStreamInterface = &_colorAdjustUSI;

            ::Assets::DependencyValidationMarker depVals[] {
                fillRoundedRect.GetDependencyValidation(),
                fillAndOutlineRoundedRect.GetDependencyValidation(),
                outlineRoundedRect.GetDependencyValidation(),
                fillRaisedRect.GetDependencyValidation(),
                fillRaisedRoundedRect.GetDependencyValidation(),
                fillReverseRaisedRoundedRect.GetDependencyValidation(),
                fillEllipse.GetDependencyValidation(),
                outlineEllipse.GetDependencyValidation(),
                softShadowRect.GetDependencyValidation(),
                dashLine.GetDependencyValidation(),
                solidNoBorder.GetDependencyValidation(),
                fillColorAdjust.GetDependencyValidation()
            };
            _depVal = ::Assets::GetDepValSys().MakeOrReuse(depVals);
        }

        static void ConstructToPromise(std::promise<std::shared_ptr<StandardResources>>&& promise)
        {
            auto fillRoundedRect = ::Assets::MakeAsset<RenderCore::Assets::ResolvedMaterial>(RENDEROVERLAYS_SHAPES_MATERIAL ":FillRoundedRect");
            auto fillAndOutlineRoundedRect = ::Assets::MakeAsset<RenderCore::Assets::ResolvedMaterial>(RENDEROVERLAYS_SHAPES_MATERIAL ":FillAndOutlineRoundedRect");
            auto outlineRoundedRect = ::Assets::MakeAsset<RenderCore::Assets::ResolvedMaterial>(RENDEROVERLAYS_SHAPES_MATERIAL ":OutlineRoundedRect");
            auto fillRaisedRect = ::Assets::MakeAsset<RenderCore::Assets::ResolvedMaterial>(RENDEROVERLAYS_SHAPES_MATERIAL ":FillRaisedRect");
            auto fillRaisedRoundedRect = ::Assets::MakeAsset<RenderCore::Assets::ResolvedMaterial>(RENDEROVERLAYS_SHAPES_MATERIAL ":FillRaisedRoundedRect");
            auto fillReverseRaisedRoundedRect = ::Assets::MakeAsset<RenderCore::Assets::ResolvedMaterial>(RENDEROVERLAYS_SHAPES_MATERIAL ":FillReverseRaisedRoundedRect");
            auto fillEllipse = ::Assets::MakeAsset<RenderCore::Assets::ResolvedMaterial>(RENDEROVERLAYS_SHAPES_MATERIAL ":FillEllipse");
            auto outlineEllipse = ::Assets::MakeAsset<RenderCore::Assets::ResolvedMaterial>(RENDEROVERLAYS_SHAPES_MATERIAL ":OutlineEllipse");
            auto softShadowRect = ::Assets::MakeAsset<RenderCore::Assets::ResolvedMaterial>(RENDEROVERLAYS_SHAPES_MATERIAL ":SoftShadowRect");
            auto dashLine = ::Assets::MakeAsset<RenderCore::Assets::ResolvedMaterial>(RENDEROVERLAYS_SHAPES_MATERIAL ":DashLine");
            auto solidNoBorder = ::Assets::MakeAsset<RenderCore::Assets::ResolvedMaterial>(RENDEROVERLAYS_SHAPES_MATERIAL ":SolidNoBorder");
            auto fillColorAdjust = ::Assets::MakeAsset<RenderCore::Assets::ResolvedMaterial>(RENDEROVERLAYS_SHAPES_MATERIAL ":FillColorAdjust");

            ::Assets::WhenAll(fillRoundedRect, fillAndOutlineRoundedRect, outlineRoundedRect, fillRaisedRect, fillRaisedRoundedRect, fillReverseRaisedRoundedRect, fillEllipse, outlineEllipse, softShadowRect, dashLine, solidNoBorder, fillColorAdjust).ThenConstructToPromise(std::move(promise));
        }

        std::vector<std::unique_ptr<ParameterBox>> _retainedParameterBoxes;
        RenderCore::Techniques::ImmediateDrawableMaterial BuildImmediateDrawableMaterial(
            const RenderCore::Assets::ResolvedMaterial& rawMat)
        {
            RenderCore::Techniques::ImmediateDrawableMaterial result;
            // somewhat awkwardly, we need to protect the lifetime of the shader selector box so it lives as long as the result
            if (rawMat._selectors.GetCount() != 0) {
                auto newBox = std::make_unique<ParameterBox>(rawMat._selectors);
                result._shaderSelectors = newBox.get();
                _retainedParameterBoxes.emplace_back(std::move(newBox));
            }
            result._stateSet = rawMat._stateSet;
            result._patchCollection = std::make_shared<RenderCore::Assets::ShaderPatchCollection>(rawMat._patchCollection);
            return result;
        }
    };

    namespace Internal
    {
        struct Vertex_PCCTT
        {
            Float3 _position; unsigned _colour0; unsigned _colour1; Float2 _texCoord0; Float2 _texCoord1;
            Vertex_PCCTT(Float3 position, unsigned colour0, unsigned colour1, Float2 texCoord0, Float2 texCoord1) 
            : _position(position), _colour0(colour0), _colour1(colour1), _texCoord0(texCoord0), _texCoord1(texCoord1) {}
            static RenderCore::MiniInputElementDesc inputElements2D[];
        };

        struct Vertex_PCT
        {
            Float3 _position; unsigned _colour; Float2 _texCoord;
            Vertex_PCT(Float3 position, unsigned colour, Float2 texCoord) : _position(position), _colour(colour), _texCoord(texCoord) {}
            static RenderCore::MiniInputElementDesc inputElements2D[];
        };

        RenderCore::MiniInputElementDesc Vertex_PCCTT::inputElements2D[] = 
        {
            RenderCore::MiniInputElementDesc{ RenderCore::Techniques::CommonSemantics::PIXELPOSITION, RenderCore::Format::R32G32B32_FLOAT },
            RenderCore::MiniInputElementDesc{ RenderCore::Techniques::CommonSemantics::COLOR, RenderCore::Format::R8G8B8A8_UNORM },
            RenderCore::MiniInputElementDesc{ RenderCore::Techniques::CommonSemantics::COLOR + 1, RenderCore::Format::R8G8B8A8_UNORM },
            RenderCore::MiniInputElementDesc{ RenderCore::Techniques::CommonSemantics::TEXCOORD, RenderCore::Format::R32G32_FLOAT },
            RenderCore::MiniInputElementDesc{ RenderCore::Techniques::CommonSemantics::TEXCOORD + 1, RenderCore::Format::R32G32_FLOAT }
        };

        RenderCore::MiniInputElementDesc Vertex_PCT::inputElements2D[] = 
        {
            { RenderCore::Techniques::CommonSemantics::PIXELPOSITION, RenderCore::Format::R32G32B32_FLOAT },
            { RenderCore::Techniques::CommonSemantics::COLOR, RenderCore::Format::R8G8B8A8_UNORM },
            { RenderCore::Techniques::CommonSemantics::TEXCOORD, RenderCore::Format::R32G32_FLOAT }
        };

        void DrawPCCTTQuad(
            IOverlayContext& context,
            const Float3& mins, const Float3& maxs, 
            ColorB color0, ColorB color1,
            const Float2& minTex0, const Float2& maxTex0, 
            const Float2& minTex1, const Float2& maxTex1,
            RenderCore::Techniques::ImmediateDrawableMaterial&& material)
        {
            auto data = context.DrawGeometry(6, Vertex_PCCTT::inputElements2D, std::move(material)).Cast<Vertex_PCCTT*>();
            if (data.empty()) return;
            assert(data.size() == 6);
            auto col0 = HardwareColor(color0);
            auto col1 = HardwareColor(color1);
            data[0] = { Float3(mins[0], mins[1], mins[2]), col0, col1, Float2(minTex0[0], minTex0[1]), Float2(minTex1[0], minTex1[1]) };
            data[1] = { Float3(mins[0], maxs[1], mins[2]), col0, col1, Float2(minTex0[0], maxTex0[1]), Float2(minTex1[0], maxTex1[1]) };
            data[2] = { Float3(maxs[0], mins[1], mins[2]), col0, col1, Float2(maxTex0[0], minTex0[1]), Float2(maxTex1[0], minTex1[1]) };
            data[3] = { Float3(maxs[0], mins[1], mins[2]), col0, col1, Float2(maxTex0[0], minTex0[1]), Float2(maxTex1[0], minTex1[1]) };
            data[4] = { Float3(mins[0], maxs[1], mins[2]), col0, col1, Float2(minTex0[0], maxTex0[1]), Float2(minTex1[0], maxTex1[1]) };
            data[5] = { Float3(maxs[0], maxs[1], mins[2]), col0, col1, Float2(maxTex0[0], maxTex0[1]), Float2(maxTex1[0], maxTex1[1]) };
        }
    }

    void OutlineEllipse(IOverlayContext& context, const Rect& rect, ColorB colour)
    {
        if (rect._bottomRight[0] <= rect._topLeft[0] || rect._bottomRight[1] <= rect._topLeft[1])
            return;

        auto* res = ConsoleRig::TryActualizeCachedBox<StandardResources>();
        if (!res) return;

        const float borderWidthPix = 1.f;
        Internal::DrawPCCTTQuad(
            context,
            AsPixelCoords(rect._topLeft),
            AsPixelCoords(rect._bottomRight),
            ColorB::Zero, colour,
            Float2(0.f, 0.f), Float2(1.f, 1.f), 
            Float2(borderWidthPix, 0.f), Float2(borderWidthPix, 0.f),
            RenderCore::Techniques::ImmediateDrawableMaterial{res->_outlineEllipse});
    }

    void FillEllipse(IOverlayContext& context, const Rect& rect, ColorB colour)
    {
        if (rect._bottomRight[0] <= rect._topLeft[0] || rect._bottomRight[1] <= rect._topLeft[1])
            return;

        auto* res = ConsoleRig::TryActualizeCachedBox<StandardResources>();
        if (!res) return;

        const float borderWidthPix = 1.f;
        Internal::DrawPCCTTQuad(
            context,
            AsPixelCoords(rect._topLeft),
            AsPixelCoords(rect._bottomRight),
            colour, ColorB::Zero,
            Float2(0.f, 0.f), Float2(1.f, 1.f), 
            Float2(borderWidthPix, 0.f), Float2(borderWidthPix, 0.f),
            RenderCore::Techniques::ImmediateDrawableMaterial{res->_fillEllipse});
    }

    void OutlineRoundedRectangle(
        IOverlayContext& context, const Rect & rect, 
        ColorB colour, 
        float width, float roundedProportion,
        Corner::BitField cornerFlags)
    {
        if (rect._bottomRight[0] <= rect._topLeft[0] || rect._bottomRight[1] <= rect._topLeft[1])
            return;

        auto* res = ConsoleRig::TryActualizeCachedBox<StandardResources>();
        if (!res) return;

        RenderCore::Techniques::ImmediateDrawableMaterial mat = res->_outlineRoundedRect;
        mat._uniforms._immediateData.push_back(RenderCore::MakeSharedPkt(Internal::CB_RoundedRectSettings { roundedProportion, cornerFlags }));

        Internal::DrawPCCTTQuad(
            context,
            AsPixelCoords(rect._topLeft),
            AsPixelCoords(rect._bottomRight),
            ColorB::Zero, colour,
            Float2(0.f, 0.f), Float2(1.f, 1.f), 
            Float2(width, roundedProportion), Float2(width, roundedProportion),
            std::move(mat));
    }

    void FillRoundedRectangle(
        IOverlayContext& context, const Rect& rect, 
        ColorB fillColor,
        float roundedProportion,
        Corner::BitField cornerFlags)
    {
        if (rect._bottomRight[0] <= rect._topLeft[0] || rect._bottomRight[1] <= rect._topLeft[1])
            return;

        auto* res = ConsoleRig::TryActualizeCachedBox<StandardResources>();
        if (!res) return;

        RenderCore::Techniques::ImmediateDrawableMaterial mat = res->_fillRoundedRect;
        mat._uniforms._immediateData.push_back(RenderCore::MakeSharedPkt(Internal::CB_RoundedRectSettings { roundedProportion, cornerFlags }));

        Internal::DrawPCCTTQuad(
            context,
            AsPixelCoords(rect._topLeft),
            AsPixelCoords(rect._bottomRight),
            fillColor, ColorB::Zero,
            Float2(0.f, 0.f), Float2(1.f, 1.f), 
            Float2(1.f, roundedProportion), Float2(1.f, roundedProportion),
            std::move(mat));
    }

    void FillAndOutlineRoundedRectangle(
        IOverlayContext& context, 
        const Rect & rect,
        ColorB fillColor, ColorB outlineColour,
        float borderWidth, float roundedProportion,
        Corner::BitField cornerFlags)
    {
        if (rect._bottomRight[0] <= rect._topLeft[0] || rect._bottomRight[1] <= rect._topLeft[1])
            return;

        auto* res = ConsoleRig::TryActualizeCachedBox<StandardResources>();
        if (!res) return;

        RenderCore::Techniques::ImmediateDrawableMaterial mat = res->_fillAndOutlineRoundedRect;
        mat._uniforms._immediateData.push_back(RenderCore::MakeSharedPkt(Internal::CB_RoundedRectSettings { roundedProportion, cornerFlags }));

        Internal::DrawPCCTTQuad(
            context,
            AsPixelCoords(rect._topLeft),
            AsPixelCoords(rect._bottomRight),
            fillColor, outlineColour,
            Float2(0.f, 0.f), Float2(1.f, 1.f), 
            Float2(borderWidth, roundedProportion), Float2(borderWidth, roundedProportion),
            std::move(mat));
    }

    void FillRaisedRoundedRectangle(
        IOverlayContext& context, const Rect& rect,
        ColorB fillColor,
        float roundedProportion,
        Corner::BitField cornerFlags)
    {
        if (rect._bottomRight[0] <= rect._topLeft[0] || rect._bottomRight[1] <= rect._topLeft[1])
            return;

        auto* res = ConsoleRig::TryActualizeCachedBox<StandardResources>();
        if (!res) return;

        RenderCore::Techniques::ImmediateDrawableMaterial mat = res->_fillRaisedRoundedRect;
        mat._uniforms._immediateData.push_back(RenderCore::MakeSharedPkt(Internal::CB_RoundedRectSettings { roundedProportion, cornerFlags }));

        Internal::DrawPCCTTQuad(
            context,
            AsPixelCoords(rect._topLeft),
            AsPixelCoords(rect._bottomRight),
            fillColor, ColorB::Zero,
            Float2(0.f, 0.f), Float2(1.f, 1.f), 
            Float2(1.f, roundedProportion), Float2(1.f, roundedProportion),
            std::move(mat));
    }

    void FillDepressedRoundedRectangle(
        IOverlayContext& context, const Rect& rect,
        ColorB fillColor,
        float roundedProportion,
        Corner::BitField cornerFlags)
    {
        if (rect._bottomRight[0] <= rect._topLeft[0] || rect._bottomRight[1] <= rect._topLeft[1])
            return;

        auto* res = ConsoleRig::TryActualizeCachedBox<StandardResources>();
        if (!res) return;

        RenderCore::Techniques::ImmediateDrawableMaterial mat = res->_fillReverseRaisedRoundedRect;
        mat._uniforms._immediateData.push_back(RenderCore::MakeSharedPkt(Internal::CB_RoundedRectSettings { roundedProportion, cornerFlags }));

        Internal::DrawPCCTTQuad(
            context,
            AsPixelCoords(rect._topLeft),
            AsPixelCoords(rect._bottomRight),
            fillColor, ColorB::Zero,
            Float2(0.f, 0.f), Float2(1.f, 1.f), 
            Float2(1.f, roundedProportion), Float2(1.f, roundedProportion),
            std::move(mat));
    }

    void FillRectangle(IOverlayContext& context, const Rect& rect, ColorB colour)
    {
        if (rect._bottomRight[0] <= rect._topLeft[0] || rect._bottomRight[1] <= rect._topLeft[1]) {
            return;
        }

        context.DrawTriangle(
            ProjectionMode::P2D, 
            AsPixelCoords(Coord2(rect._topLeft[0], rect._topLeft[1])), colour,
            AsPixelCoords(Coord2(rect._topLeft[0], rect._bottomRight[1])), colour,
            AsPixelCoords(Coord2(rect._bottomRight[0]-1, rect._topLeft[1])), colour);

        context.DrawTriangle(
            ProjectionMode::P2D, 
            AsPixelCoords(Coord2(rect._bottomRight[0]-1, rect._topLeft[1])), colour,
            AsPixelCoords(Coord2(rect._topLeft[0], rect._bottomRight[1])), colour,
            AsPixelCoords(Coord2(rect._bottomRight[0]-1, rect._bottomRight[1])), colour);
    }

    void OutlineRectangle(IOverlayContext& context, const Rect& rect, ColorB colour, float outlineWidth)
    {
        if (rect._bottomRight[0] <= rect._topLeft[0] || rect._bottomRight[1] <= rect._topLeft[1]) {
            return;
        }
        assert(outlineWidth == 1.f);        // resizing border not currently supported

        Float3 lines[8];
        lines[0] = AsPixelCoords(Float2(rect._topLeft[0],       rect._topLeft[1]));
        lines[1] = AsPixelCoords(Float2(rect._bottomRight[0],   rect._topLeft[1]));
        lines[2] = AsPixelCoords(Float2(rect._bottomRight[0],   rect._topLeft[1]));
        lines[3] = AsPixelCoords(Float2(rect._bottomRight[0],   rect._bottomRight[1]));
        lines[4] = AsPixelCoords(Float2(rect._bottomRight[0],   rect._bottomRight[1]));
        lines[5] = AsPixelCoords(Float2(rect._topLeft[0],       rect._bottomRight[1]));
        lines[6] = AsPixelCoords(Float2(rect._topLeft[0],       rect._bottomRight[1]));
        lines[7] = AsPixelCoords(Float2(rect._topLeft[0],       rect._topLeft[1]));
        context.DrawLines(ProjectionMode::P2D, lines, dimof(lines), colour);
    }

    void        FillAndOutlineRectangle(IOverlayContext& context, const Rect& rect, ColorB fillColour, ColorB outlineColour, float outlineWidth)
    {
        FillRectangle(context, rect, fillColour);
        OutlineRectangle(context, rect, outlineColour, outlineWidth);
    }

    void FillRaisedRectangle(
        IOverlayContext& context, const Rect& rect,
        ColorB fillColor)
    {
        auto* res = ConsoleRig::TryActualizeCachedBox<StandardResources>();
        if (!res) return;

        Internal::DrawPCCTTQuad(
            context,
            AsPixelCoords(rect._topLeft), AsPixelCoords(rect._bottomRight),
            fillColor, fillColor, 
            Float2(0.f, 0.f), Float2(1.f, 1.f),
            Float2(0.f, 0.f), Float2(0.f, 0.f),
            RenderCore::Techniques::ImmediateDrawableMaterial{res->_fillRaisedRect});
    }

    void        SoftShadowRectangle(IOverlayContext& context, const Rect& rect, unsigned softnessRadius)
    {
        if (rect._bottomRight[0] <= rect._topLeft[0] || rect._bottomRight[1] <= rect._topLeft[1])
            return;

        auto* res = ConsoleRig::TryActualizeCachedBox<StandardResources>();
        if (!res) return;

        RenderCore::Techniques::ImmediateDrawableMaterial mat = res->_softShadowRect;
        const int radiusX = softnessRadius, radiusY = softnessRadius;
        Internal::DrawPCCTTQuad(
            context,
            AsPixelCoords(Coord2(rect._topLeft - Coord2{radiusX, radiusY})),
            AsPixelCoords(Coord2(rect._bottomRight + Coord2{radiusX, radiusY})),
            ColorB::Black, ColorB::Zero,
            Float2(    - radiusX / float(rect.Width()),     - radiusY / float(rect.Height())),
            Float2(1.f + radiusX / float(rect.Width()), 1.f + radiusY / float(rect.Height())),
            Float2(radiusX, radiusY), Float2(radiusX, radiusY),
            std::move(mat));
    }

    void        ColorAdjustRectangle(
        IOverlayContext& context, const Rect& rect,
        Float2 texCoordMin, Float2 texCoordMax,
        std::shared_ptr<RenderCore::IResourceView> tex, const ColorAdjust& colorAdjust, ColorB modulation)
    {
        if (rect._bottomRight[0] <= rect._topLeft[0] || rect._bottomRight[1] <= rect._topLeft[1])
            return;

        auto* res = ConsoleRig::TryActualizeCachedBox<StandardResources>();
        if (!res) return;

        RenderCore::Techniques::ImmediateDrawableMaterial mat = res->_fillColorAdjust;
        mat._uniforms._immediateData.emplace_back(RenderCore::MakeSharedPkt(colorAdjust));
        mat._uniforms._resourceViews.emplace_back(std::move(tex));
        Internal::DrawPCCTTQuad(
            context,
            AsPixelCoords(rect._topLeft),
            AsPixelCoords(rect._bottomRight),
            modulation, ColorB::Zero,
            texCoordMin, texCoordMax,
            Float2(0.f, 0.f), Float2(0.f, 0.f),
            std::move(mat));
    }

    void WriteInLineVertices(IteratorRange<Internal::Vertex_PCT*> data, IteratorRange<const Float2*> linePts, ColorB colour, float width)
    {
		auto col0 = HardwareColor(colour);

        float x = 0;
        const float halfWidth = .5f * width;
        float prevA = 0.f;
        int nextTriangleSign = 0;
        Internal::Vertex_PCT* vIterator = data.begin();
        for (unsigned c=0; c<linePts.size()-1; ++c) {
            Float3 pt0 = AsPixelCoords(linePts[c]);
            Float3 pt1 = AsPixelCoords(linePts[c+1]);

                ///////////

            float a0 = 0.f, a1 = 0.f;
            Float2 d0{0,0}, d1 = Truncate(Float3(pt1 - pt0));
            float length = Magnitude(d1);
            d1 /= length;
            a0 = -prevA;
            int triangleSign = nextTriangleSign;
            if ((c+2) < linePts.size()) {
                Float3 pt2 = AsPixelCoords(linePts[c+2]);
                Float2 d2 = Normalize(Truncate(Float3(pt2 - pt1)));
                float cosTheta = dot(-d1, d2);
                // tan(A/2) = +/-sqrt((1-cosA)/(1+cosA))
                a1 = halfWidth / sqrt((1.f-cosTheta)/(1.f+cosTheta));
                a1 = -a1;

                nextTriangleSign = TriangleSign(Truncate(pt0), Truncate(pt1), Truncate(pt2));
            }
            prevA = a1;
            Float2 axis { -d1[1], d1[0] };
            float x2 = x + length;

            vIterator[ 0] = Internal::Vertex_PCT { pt0 + Expand(Float2(a0*d1), 0.f),                        col0, Float2(x - a0, 0) };
            vIterator[ 1] = Internal::Vertex_PCT { pt0 + Expand(Float2(a0*d1 + halfWidth * axis), 0.f),     col0, Float2(x - a0, 1.f) };
            vIterator[ 2] = Internal::Vertex_PCT { pt1 + Expand(Float2(a1*d1), 0.f),                        col0, Float2(x2 + a1, 0) };

            vIterator[ 3] = Internal::Vertex_PCT { pt1 + Expand(Float2(a1*d1), 0.f),                        col0, Float2(x2 + a1, 0) };
            vIterator[ 4] = Internal::Vertex_PCT { pt0 + Expand(Float2(a0*d1 + halfWidth * axis), 0.f),     col0, Float2(x - a0, 1.f) };
            vIterator[ 5] = Internal::Vertex_PCT { pt1 + Expand(Float2(a1*d1 + halfWidth * axis), 0.f),     col0, Float2(x2 + a1, 1.f) };

            ///////////

            vIterator[ 8] = Internal::Vertex_PCT { pt0 + Expand(Float2(a0*d1), 0.f),                        col0, Float2(x - a0, 0) };
            vIterator[ 7] = Internal::Vertex_PCT { pt0 + Expand(Float2(a0*d1 - halfWidth * axis), 0.f),     col0, Float2(x - a0, -1.f) };
            vIterator[ 6] = Internal::Vertex_PCT { pt1 + Expand(Float2(a1*d1), 0.f),                        col0, Float2(x2 + a1, 0) };

            vIterator[11] = Internal::Vertex_PCT { pt1 + Expand(Float2(a1*d1), 0.f),                        col0, Float2(x2 + a1, 0) };
            vIterator[10] = Internal::Vertex_PCT { pt0 + Expand(Float2(a0*d1 - halfWidth * axis), 0.f),     col0, Float2(x - a0, -1.f) };
            vIterator[ 9] = Internal::Vertex_PCT { pt1 + Expand(Float2(a1*d1 - halfWidth * axis), 0.f),     col0, Float2(x2 + a1, -1.f) };

            vIterator += 12;

            // wedges for the joins
            if (c!=0) {

                float B = 1.f;
                Float2 adjAxis = axis;
                if (triangleSign < 0) {
                    B = -B;
                    adjAxis = -adjAxis;
                }

                // interior side
                vIterator[ 0] = Internal::Vertex_PCT { pt0 + Expand(Float2(a0*d1), 0.f),                            col0, Float2(x,  0) };
                vIterator[ 1] = Internal::Vertex_PCT { pt0,                                                         col0, Float2(x,  0) };
                vIterator[ 2] = Internal::Vertex_PCT { pt0 + Expand(Float2(a0*d1 + halfWidth * adjAxis), 0.f),      col0, Float2(x,  B) };

                // exterior side
                vIterator[ 3] = Internal::Vertex_PCT { pt0,                                                         col0, Float2(x,  0) };
                vIterator[ 4] = Internal::Vertex_PCT { pt0 + Expand(Float2(a0*d1), 0.f),                            col0, Float2(x,  0) };
                vIterator[ 5] = Internal::Vertex_PCT { pt0 + Expand(Float2(a0*d1 - halfWidth * adjAxis), 0.f),      col0, Float2(x, -B) };

                vIterator[ 6] = Internal::Vertex_PCT { pt0,                                                         col0, Float2(x,  0) };
                vIterator[ 7] = Internal::Vertex_PCT { pt0 + Expand(Float2(a0*d1 - halfWidth * adjAxis), 0.f),      col0, Float2(x, -B) };
                vIterator[ 8] = Internal::Vertex_PCT { pt0 + Expand(Float2(-a0*d1 - halfWidth * adjAxis), 0.f),     col0, Float2(x, -B) };

                if (triangleSign < 0) {
                    // swap winding
                    std::swap(vIterator[0], vIterator[2]);
                    std::swap(vIterator[3], vIterator[5]);
                    std::swap(vIterator[6], vIterator[8]);
                }

                vIterator += 9;
            }

            if ((c+2) < linePts.size()) {

                float B = 1.f;
                Float2 adjAxis = axis;
                if (nextTriangleSign < 0) {
                    B = -B;
                    adjAxis = -adjAxis;
                }

                // interior side
                vIterator[ 0] = Internal::Vertex_PCT { pt1 + Expand(Float2(a1*d1 + halfWidth * adjAxis), 0.f),      col0, Float2(x2,  B) };
                vIterator[ 1] = Internal::Vertex_PCT { pt1,                                                         col0, Float2(x2,  0) };
                vIterator[ 2] = Internal::Vertex_PCT { pt1 + Expand(Float2(a1*d1), 0.f),                            col0, Float2(x2,  0) };

                // exterior side
                vIterator[ 3] = Internal::Vertex_PCT { pt1 + Expand(Float2(a1*d1), 0.f),                            col0, Float2(x2,  0) };
                vIterator[ 4] = Internal::Vertex_PCT { pt1,                                                         col0, Float2(x2,  0) };
                vIterator[ 5] = Internal::Vertex_PCT { pt1 + Expand(Float2(a1*d1 - halfWidth * adjAxis), 0.f),      col0, Float2(x2, -B) };

                vIterator[ 6] = Internal::Vertex_PCT { pt1 + Expand(Float2(a1*d1 - halfWidth * adjAxis), 0.f),      col0, Float2(x2, -B) };
                vIterator[ 7] = Internal::Vertex_PCT { pt1,                                                         col0, Float2(x2,  0) };
                vIterator[ 8] = Internal::Vertex_PCT { pt1 + Expand(Float2(-a1*d1 - halfWidth * adjAxis), 0.f),     col0, Float2(x2, -B) };

                if (nextTriangleSign < 0) {
                    // swap winding
                    std::swap(vIterator[0], vIterator[2]);
                    std::swap(vIterator[3], vIterator[5]);
                    std::swap(vIterator[6], vIterator[8]);
                }

                vIterator += 9;
            }

            ///////////

            x = x2;
        }
    }

    void WriteInLineVerticesInset(IteratorRange<Internal::Vertex_PCT*> data, IteratorRange<const Float2*> linePts, ColorB colour, float width)
    {
		auto col0 = HardwareColor(colour);

        float x = 0;
        float prevA = 0.f;
        int nextTriangleSign = 0;
        Internal::Vertex_PCT* vIterator = data.begin();
        for (unsigned c=0; c<linePts.size()-1; ++c) {
            Float3 pt0 = AsPixelCoords(linePts[c]);
            Float3 pt1 = AsPixelCoords(linePts[c+1]);

                ///////////

            float a0 = 0.f, a1 = 0.f;
            Float2 d0{0,0}, d1 = Truncate(Float3(pt1 - pt0));
            float length = Magnitude(d1);
            d1 /= length;
            a0 = -prevA;
            int triangleSign = nextTriangleSign;
            if ((c+2) < linePts.size()) {
                Float3 pt2 = AsPixelCoords(linePts[c+2]);
                Float2 d2 = Normalize(Truncate(Float3(pt2 - pt1)));
                float cosTheta = dot(-d1, d2);
                // tan(A/2) = +/-sqrt((1-cosA)/(1+cosA))
                a1 = width / sqrt((1.f-cosTheta)/(1.f+cosTheta));
                a1 = -a1;

                nextTriangleSign = TriangleSign(Truncate(pt0), Truncate(pt1), Truncate(pt2));
            }
            prevA = a1;
            Float2 axis { -d1[1], d1[0] };
            float x2 = x + length;

            vIterator[ 0] = Internal::Vertex_PCT { pt0 + Expand(Float2(a0*d1), 0.f),                    col0, Float2(x - a0, 0) };
            vIterator[ 1] = Internal::Vertex_PCT { pt0 + Expand(Float2(a0*d1 + width * axis), 0.f),     col0, Float2(x - a0, 1.f) };
            vIterator[ 2] = Internal::Vertex_PCT { pt1 + Expand(Float2(a1*d1), 0.f),                    col0, Float2(x2 + a1, 0) };

            vIterator[ 3] = Internal::Vertex_PCT { pt1 + Expand(Float2(a1*d1), 0.f),                    col0, Float2(x2 + a1, 0) };
            vIterator[ 4] = Internal::Vertex_PCT { pt0 + Expand(Float2(a0*d1 + width * axis), 0.f),     col0, Float2(x - a0, 1.f) };
            vIterator[ 5] = Internal::Vertex_PCT { pt1 + Expand(Float2(a1*d1 + width * axis), 0.f),     col0, Float2(x2 + a1, 1.f) };

            vIterator += 6;

            // wedges for the joins
            // technically we could do this with one fewer triangle per wedge; but it's just slightly more convenient to do it this way right now
            if (c!=0) {

                vIterator[ 0] = Internal::Vertex_PCT { pt0 + Expand(Float2(a0*d1 + width * axis), 0.f),     col0, Float2(x,  1) };
                vIterator[ 1] = Internal::Vertex_PCT { pt0 + Expand(Float2(a0*d1), 0.f),                    col0, Float2(x,  0) };
                vIterator[ 2] = Internal::Vertex_PCT { pt0,                                                 col0, Float2(x,  0) };
                vIterator += 3;

                if (triangleSign < 0) {
                    vIterator[ 0] = Internal::Vertex_PCT { pt0 + Expand(Float2(-a0*d1 + width * axis), 0.f),    col0, Float2(x,  1) };
                    vIterator[ 1] = Internal::Vertex_PCT { pt0 + Expand(Float2(a0*d1 + width * axis), 0.f),     col0, Float2(x,  1) };
                    vIterator[ 2] = Internal::Vertex_PCT { pt0,                                                 col0, Float2(x,  0) };
                } else {
                    vIterator[ 0] = vIterator[ 1] = vIterator[ 2] = Internal::Vertex_PCT { pt0, col0, Float2(x,  0) };
                }
                vIterator += 3;
            }

            if ((c+2) < linePts.size()) {

                vIterator[ 0] = Internal::Vertex_PCT { pt1 + Expand(Float2(a1*d1 + width * axis), 0.f),         col0, Float2(x2,  1) };
                vIterator[ 1] = Internal::Vertex_PCT { pt1,                                                     col0, Float2(x2,  0) };
                vIterator[ 2] = Internal::Vertex_PCT { pt1 + Expand(Float2(a1*d1), 0.f),                        col0, Float2(x2,  0) };
                vIterator += 3;

                if (nextTriangleSign < 0) {
                    vIterator[ 0] = Internal::Vertex_PCT { pt1,                                                     col0, Float2(x2,  0) };
                    vIterator[ 1] = Internal::Vertex_PCT { pt1 + Expand(Float2(a1*d1 + width * axis), 0.f),         col0, Float2(x2,  1) };
                    vIterator[ 2] = Internal::Vertex_PCT { pt1 + Expand(Float2(-a1*d1 + width * axis), 0.f),        col0, Float2(x2,  1) };
                } else {
                    vIterator[ 0] = vIterator[ 1] = vIterator[ 2] = Internal::Vertex_PCT { pt0, col0, Float2(x,  0) };
                }
                vIterator += 3;
            }

            ///////////

            x = x2;
        }
    }

    void        DashLine(IOverlayContext& context, IteratorRange<const Float2*> linePts, ColorB colour, float width)
    {
        if (linePts.size() < 2) return;

        auto* res = ConsoleRig::TryActualizeCachedBox<StandardResources>();
        if (!res) return;

        RenderCore::Techniques::ImmediateDrawableMaterial mat = res->_dashLine;
        mat._stateSet.SetDoubleSided(true);     // disable backface culling because winding depends on line direction

        const unsigned joinsVertices = 9*2;
        auto data = context.DrawGeometry(unsigned((linePts.size()-1)*3*4 + ((linePts.size()-2)*joinsVertices)), Internal::Vertex_PCT::inputElements2D, std::move(mat)).Cast<Internal::Vertex_PCT*>();
        if (data.empty()) return;

        WriteInLineVertices(data, linePts, colour, width);
    }

    void        SolidLine(IOverlayContext& context, IteratorRange<const Float2*> linePts, ColorB colour, float width)
    {
        if (linePts.size() < 2) return;

        auto* res = ConsoleRig::TryActualizeCachedBox<StandardResources>();
        if (!res) return;

        RenderCore::Techniques::ImmediateDrawableMaterial mat = res->_solidNoBorder;
        mat._stateSet.SetDoubleSided(true);     // disable backface culling because winding depends on line direction

        const unsigned joinsVertices = 9*2;
        auto data = context.DrawGeometry(unsigned((linePts.size()-1)*3*4 + ((linePts.size()-2)*joinsVertices)), Internal::Vertex_PCT::inputElements2D, std::move(mat)).Cast<Internal::Vertex_PCT*>();
        if (data.empty()) return;

        WriteInLineVertices(data, linePts, colour, width);
    }

    void        DashLineInset(IOverlayContext& context, IteratorRange<const Float2*> linePts, ColorB colour, float width)
    {
        if (linePts.size() < 2) return;

        auto* res = ConsoleRig::TryActualizeCachedBox<StandardResources>();
        if (!res) return;

        RenderCore::Techniques::ImmediateDrawableMaterial mat = res->_dashLine;

        const unsigned joinsVertices = 6*2;
        auto data = context.DrawGeometry(unsigned((linePts.size()-1)*3*2 + ((linePts.size()-2)*joinsVertices)), Internal::Vertex_PCT::inputElements2D, std::move(mat)).Cast<Internal::Vertex_PCT*>();
        if (data.empty()) return;

        WriteInLineVerticesInset(data, linePts, colour, width);
    }

    void        SolidLineInset(IOverlayContext& context, IteratorRange<const Float2*> linePts, ColorB colour, float width)
    {
        if (linePts.size() < 2) return;

        auto* res = ConsoleRig::TryActualizeCachedBox<StandardResources>();
        if (!res) return;

        RenderCore::Techniques::ImmediateDrawableMaterial mat = res->_solidNoBorder;

        const unsigned joinsVertices = 6*2;
        auto data = context.DrawGeometry(unsigned((linePts.size()-1)*3*2 + ((linePts.size()-2)*joinsVertices)), Internal::Vertex_PCT::inputElements2D, std::move(mat)).Cast<Internal::Vertex_PCT*>();
        if (data.empty()) return;

        WriteInLineVerticesInset(data, linePts, colour, width);
    }

	///////////////////////////////////////////////////////////////////////////////////

	Coord2 DrawText::Draw(IOverlayContext& context, const Rect& rect, StringSection<> text) const
	{
		if (_font) {
			return context.DrawText(AsPixelCoords(rect), *_font, _flags, _color, _alignment, text);
		} else {
			auto* res = ConsoleRig::TryActualizeCachedBox<Internal::DefaultFontsBox>();
			if (expect_evaluation(res != nullptr, true))
				return context.DrawText(AsPixelCoords(rect), *res->_defaultFont, _flags, _color, _alignment, text);
			return {0,0};
		}
	}
	
	Coord2 DrawText::FormatAndDraw(IOverlayContext& context, const Rect& rect, const char format[], va_list args) const
	{
		char buffer[4096];
		vsnprintf(buffer, dimof(buffer), format, args);
		return Draw(context, rect, buffer);
	}

	Coord2 DrawText::FormatAndDraw(IOverlayContext& context, const Rect& rect, const char format[], ...) const
	{
		va_list args;
		va_start(args, format);
		auto result = FormatAndDraw(context, rect, format, args);
		va_end(args);
		return result;
	}

	namespace Internal
	{
        struct DefaultFontsStaticData
        {
            std::string _defaultFont = "Petra:16";
            std::string _tableHeaderFont = "DosisExtraBold:20";
            std::string _tableValuesFont = "Petra:20";

            DefaultFontsStaticData() = default;
            template<typename Formatter>
                DefaultFontsStaticData(Formatter& fmttr)
            {
                uint64_t keyname;
                while (fmttr.TryKeyedItem(keyname)) {
                    switch (keyname) {
                    case "Default"_h: _defaultFont = Formatters::RequireStringValue(fmttr).AsString(); break;
                    case "TableHeader"_h: _tableHeaderFont = Formatters::RequireStringValue(fmttr).AsString(); break;
                    case "TableValues"_h: _tableValuesFont = Formatters::RequireStringValue(fmttr).AsString(); break;
                    default: SkipValueOrElement(fmttr); break;
                    }
                }
            }
        };

		void DefaultFontsBox::ConstructToPromise(std::promise<std::shared_ptr<DefaultFontsBox>>&& promise)
		{
            auto marker = ::Assets::MakeAssetMarker<EntityInterface::MountedData<DefaultFontsStaticData>>("cfg/displays/font");
            #if 0
                ::Assets::WhenAll(marker).ThenConstructToPromise(
                    std::move(promise),
                    [](auto&& promise, auto staticData) {
                        ::Assets::WhenAll(
                            RenderOverlays::MakeFont(staticData.get()._defaultFont),
                            RenderOverlays::MakeFont(staticData.get()._tableHeaderFont),
                            RenderOverlays::MakeFont(staticData.get()._tableValuesFont)).ThenConstructToPromise(
                                std::move(promise),
                                [depVal = staticData.GetDependencyValidation()](auto f0, auto f1, auto f2) mutable {
                                    return std::make_shared<DefaultFontsBox>(std::move(f0), std::move(f1), std::move(f2), std::move(depVal));
                                });
                    });
            #else
                ::Assets::WhenAll(marker).Then(
                    [promise=std::move(promise)](auto futureStaticData) mutable {
                        DefaultFontsStaticData staticData;
                        ::Assets::DependencyValidation depVal;
                        TRY {
                            auto sd = futureStaticData.get();
                            staticData = sd.get();
                            depVal = sd.GetDependencyValidation();
                        } CATCH(...) {
                        } CATCH_END

                        ::Assets::WhenAll(
                            RenderOverlays::MakeFont(staticData._defaultFont),
                            RenderOverlays::MakeFont(staticData._tableHeaderFont),
                            RenderOverlays::MakeFont(staticData._tableValuesFont)).ThenConstructToPromise(
                                std::move(promise),
                                [depVal = std::move(depVal)](auto f0, auto f1, auto f2) mutable {
                                    return std::make_shared<DefaultFontsBox>(std::move(f0), std::move(f1), std::move(f2), std::move(depVal));
                                });
                    });
            #endif
		}

        DefaultFontsBox* TryGetDefaultFontsBox()
        {
            return ConsoleRig::TryActualizeCachedBox<Internal::DefaultFontsBox>();
        }
	}

	    ///////////////////////////////////////////////////////////////////////////////////

	class ShapesRenderingTechniqueDelegate : public RenderCore::Techniques::ITechniqueDelegate
	{
	public:
		std::shared_ptr<RenderCore::Techniques::GraphicsPipelineDesc> GetPipelineDesc(
			const RenderCore::Techniques::CompiledShaderPatchCollection::Interface& shaderPatches,
			const RenderCore::Assets::RenderStateSet& renderStates) override
		{
            using namespace RenderCore;
			constexpr uint64_t s_patchShape = "IShape2D_Calculate"_h;
			constexpr uint64_t s_patchFill = "IFill_Calculate"_h;
			constexpr uint64_t s_patchOutline = "IOutline_Calculate"_h;
			constexpr uint64_t s_patchTwoLayersShader = "TwoLayersShader"_h;

			unsigned pipelineBase = 0;
			// We're re-purposing the _writeMask flag for depth test and write
			if (renderStates._flag & RenderCore::Assets::RenderStateSet::Flag::WriteMask) {
				bool depthWrite = renderStates._writeMask & 1<<0;
				bool depthTest = renderStates._writeMask & 1<<1;
				if (depthTest) {
					pipelineBase = depthWrite ? 0 : 1;
				} else {
					pipelineBase = 2;
				}
			}

            bool doubleSided = (renderStates._flag & RenderCore::Assets::RenderStateSet::Flag::DoubleSided) && renderStates._doubleSided;
            if (doubleSided)
                pipelineBase += 3;

			if (shaderPatches.HasPatchType(s_patchShape)) {
				auto nascentDesc = std::make_shared<Techniques::GraphicsPipelineDesc>();
				*nascentDesc = *_pipelineDesc[pipelineBase];

				nascentDesc->_shaders[(unsigned)ShaderStage::Pixel] = RENDEROVERLAYS_SHAPES_HLSL ":frameworkEntry:ps_*";
				nascentDesc->_patchExpansions.emplace_back(s_patchShape, ShaderStage::Pixel);
				nascentDesc->_patchExpansions.emplace_back(s_patchFill, ShaderStage::Pixel);
				nascentDesc->_patchExpansions.emplace_back(s_patchOutline, ShaderStage::Pixel);
				nascentDesc->_materialPreconfigurationFile = shaderPatches.GetPreconfigurationFileName();

				return nascentDesc;
			} else if (shaderPatches.HasPatchType(s_patchTwoLayersShader)) {
				auto nascentDesc = std::make_shared<Techniques::GraphicsPipelineDesc>();
				*nascentDesc = *_pipelineDesc[pipelineBase];

				nascentDesc->_shaders[(unsigned)ShaderStage::Pixel] = RENDEROVERLAYS_SHAPES_HLSL ":frameworkEntryForTwoLayersShader:ps_*";
				nascentDesc->_patchExpansions.emplace_back(s_patchTwoLayersShader, ShaderStage::Pixel);
				nascentDesc->_materialPreconfigurationFile = shaderPatches.GetPreconfigurationFileName();

				return nascentDesc;
			} else if (shaderPatches.HasPatchType(s_patchFill)) {
				auto nascentDesc = std::make_shared<Techniques::GraphicsPipelineDesc>();
				*nascentDesc = *_pipelineDesc[pipelineBase];

				nascentDesc->_shaders[(unsigned)ShaderStage::Pixel] = RENDEROVERLAYS_SHAPES_HLSL ":frameworkEntryJustFill:ps_*";
				nascentDesc->_patchExpansions.emplace_back(s_patchFill, ShaderStage::Pixel);
				nascentDesc->_materialPreconfigurationFile = shaderPatches.GetPreconfigurationFileName();
                nascentDesc->_manualSelectorFiltering.SetSelector("VSOUT_HAS_COLOR_LINEAR1", 0);
                nascentDesc->_manualSelectorFiltering.SetSelector("VSOUT_HAS_TEXCOORD1", 0);

				return nascentDesc;
			} else {
				return _pipelineDesc[pipelineBase];
			}
		}

		virtual std::shared_ptr<RenderCore::Assets::PredefinedPipelineLayout> GetPipelineLayout() override { return _pipelineLayout; }
		virtual ::Assets::DependencyValidation GetDependencyValidation() override { return _pipelineLayout->GetDependencyValidation(); }

		ShapesRenderingTechniqueDelegate(std::shared_ptr<RenderCore::Assets::PredefinedPipelineLayout> pipelineLayout) 
		: _pipelineLayout(std::move(pipelineLayout))
		{
            using namespace RenderCore;
			auto templateDesc = std::make_shared<Techniques::GraphicsPipelineDesc>();
			templateDesc->_shaders[(unsigned)ShaderStage::Vertex] = BASIC2D_VERTEX_HLSL ":frameworkEntry:vs_*";
			templateDesc->_shaders[(unsigned)ShaderStage::Pixel] = BASIC_PIXEL_HLSL ":frameworkEntry:ps_*";
			templateDesc->_techniquePreconfigurationFile = RENDEROVERLAYS_SEL_PRECONFIG;

			templateDesc->_rasterization = Techniques::CommonResourceBox::s_rsDefault;
			templateDesc->_blend.push_back(Techniques::CommonResourceBox::s_abStraightAlpha);

			DepthStencilDesc dsModes[] = {
				Techniques::CommonResourceBox::s_dsReadWrite,
				Techniques::CommonResourceBox::s_dsReadOnly,
				Techniques::CommonResourceBox::s_dsDisable
			};
            for (unsigned q=0; q<2; ++q)
                for (unsigned c=0; c<dimof(dsModes); ++c) {
                    _pipelineDesc[c+q*3] = std::make_shared<Techniques::GraphicsPipelineDesc>(*templateDesc);
                    _pipelineDesc[c+q*3]->_depthStencil = dsModes[c];
                    if (q == 1)
                        _pipelineDesc[c+q*3]->_rasterization = Techniques::CommonResourceBox::s_rsCullDisable;
                }
		}
		~ShapesRenderingTechniqueDelegate() {}
	private:
		std::shared_ptr<RenderCore::Techniques::GraphicsPipelineDesc> _pipelineDesc[6];
		std::shared_ptr<RenderCore::Assets::PredefinedPipelineLayout> _pipelineLayout;
	};

	void CreateShapesRenderingTechniqueDelegate(
		std::promise<std::shared_ptr<RenderCore::Techniques::ITechniqueDelegate>>&& promise)
	{
		auto pipelineLayoutFuture = ::Assets::MakeAssetPtr<RenderCore::Assets::PredefinedPipelineLayout>(IMMEDIATE_PIPELINE ":ImmediateDrawables");
		::Assets::WhenAll(pipelineLayoutFuture).ThenConstructToPromise(
			std::move(promise),
			[](auto pipelineLayout) { return std::make_shared<ShapesRenderingTechniqueDelegate>(std::move(pipelineLayout)); });
	}

	const std::shared_ptr<RenderCore::Techniques::ITechniqueDelegate>& ShapesRenderingDelegate::GetTechniqueDelegate()
	{
		return _futureTechniqueDelegate.get();
	}

	const std::shared_ptr<RenderCore::Techniques::IPipelineLayoutDelegate>& ShapesRenderingDelegate::GetPipelineLayoutDelegate()
	{
		return _pipelineLayoutDelegate;
	}

	ShapesRenderingDelegate::ShapesRenderingDelegate()
	{
		std::promise<std::shared_ptr<RenderCore::Techniques::ITechniqueDelegate>> promisedTechniqueDelegate;
		_futureTechniqueDelegate = promisedTechniqueDelegate.get_future();
		CreateShapesRenderingTechniqueDelegate(std::move(promisedTechniqueDelegate));
		_pipelineLayoutDelegate = RenderCore::Techniques::CreatePipelineLayoutDelegate(IMMEDIATE_PIPELINE ":ImmediateDrawables");
	}

	ShapesRenderingDelegate::~ShapesRenderingDelegate() {}

}
