// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "../../RenderOverlays/OverlayContext.h"
#include "../../RenderCore/Techniques/CommonResources.h"
#include "../../RenderCore/IThreadContext.h"
#include "../../RenderCore/ResourceList.h"
#include "../../RenderCore/Metal/DeviceContext.h"
#include "../../SceneEngine/MetalStubs.h"
#include "../../ConsoleRig/ResourceBox.h"
#include "../../Utility/StringUtils.h"

namespace Sample
{

    /// <summary>Resource box example</summary>
    /// This is an example of a cached resource box. These boxes are used to store
    /// generic resources in a convenient way.
    ///
    /// There are a number of useful advantages to this pattern:
    /// <list>
    ///  <item> Boxes are ostensibly global, but we can control over the life
    ///         time, and we can build a list of all of the existing boxes at
    ///         any point. That means we can build metrics for memory usage, etc.
    ///
    ///  <item> This is really convenient when we want to have a bunch of resources
    ///         that are shared by a large number of objects (a good example is
    ///         the resource box in SceneEngine/CommonResources.h).
    ///
    ///  <item> We can also collect resources that are related. This is handy for
    ///         invalidation. A resource box can have an Assets::DependencyValidation
    ///         object associated. This can be invalidated by any contained resource,
    ///         and when it's invalidated, the entire box will be rebuild. This is
    ///         great for vertex input layout objects -- because if the vertex shader
    ///         is invalidated, we also want to rebuild the input layout.
    ///
    ///  <item> Some resource need to be invalidated when the main window changes size
    ///         (for back buffers, etc). We can use this abstraction to destroy all
    ///         these types of resources in a convenient way.
    ///
    ///  <item> It's just a handy way to avoid cluttering up classes with pointers to
    ///         this and that... Without a system like this, we would need a lot of shared
    ///         pointers to things, and would need to pass this from function to function,
    ///         class to class. It's inconvenient. This pattern allows us to access the
    ///         resources we need with minimal extra code.
    /// </list>
    ///
    /// Mostly it's just for convenience. It's a way to share and reuse resources with
    /// minimal boring code.
    class RenderPostSceneResources
    {
    public:
		std::shared_ptr<RenderOverlays::Font> _font;
        RenderPostSceneResources(unsigned fontSize);
    };

    RenderPostSceneResources::RenderPostSceneResources(unsigned fontSize)
    {
        _font = RenderOverlays::GetX2Font("DosisExtraBold", fontSize);
    }

    /// <summary>Renders some text to a device</summary>
    /// This is intended as a simple first-steps rendering example.
    /// We show 2 ways to render text onto the screen.
    void RenderPostScene(RenderCore::IThreadContext& context)
    {
            //
            //  Let's render some text in the middle of the screen.
            //  I'll show 2 ways to do it.
            //      1. using a IOverlayContext
            //      2. using RenderOverlay::Font directly
            //
        const char text[] = "Hello World!... It's me, XLE!";
        const bool textRenderingMethod = 1;

        using namespace RenderOverlays;
        auto& res = ConsoleRig::FindCachedBox<RenderPostSceneResources>(64);
		TextStyle style{};
        ColorB col(0xffffffff);

        auto contextStateDesc = context.GetStateDesc();
        if (constant_expression<textRenderingMethod==0>::result()) {

                //      Render text using a IOverlayContext
            ImmediateOverlayContext overlayContext(context);
            overlayContext.CaptureState();
            overlayContext.DrawText(
                std::make_tuple(
                    Float3(0.f, 0.f, 0.f), 
                    Float3(float(contextStateDesc._viewportDimensions[0]), float(contextStateDesc._viewportDimensions[1]), 0.f)),
                res._font, style, col, TextAlignment::Center, text);

        } else {

                //      Render text directly to the scene using RenderOverlays stuff.
                //      This requires some more low-level code, so it's less convenient

            auto metalContext = RenderCore::Metal::DeviceContext::Get(context);
            if (metalContext) {
                auto& commonRes = RenderCore::Techniques::CommonResources();
                metalContext->Bind(commonRes._blendStraightAlpha);
                metalContext->Bind(commonRes._dssReadWrite);
                metalContext->Bind(commonRes._defaultRasterizer);
                SceneEngine::MetalStubs::GetGlobalNumericUniforms(*metalContext, RenderCore::ShaderStage::Pixel).Bind(
					RenderCore::MakeResourceList(commonRes._defaultSampler));
            }

            ucs4 buffer[1024];
            utf8_2_ucs4((const utf8*)text, XlStringSize(text), buffer, dimof(buffer));
            Quad quad = Quad::MinMax(Float2(0.f, 0.f), Float2(float(contextStateDesc._viewportDimensions[0]), float(contextStateDesc._viewportDimensions[1])));

            auto alignment = AlignText(*res._font, quad, TextAlignment::Center, buffer);
            Draw(
                context, *res._font, style, alignment[0], alignment[1],
                buffer,
                0.f, 1.f, 0.f, 0.f,
                col.AsUInt32(), 0.f, &quad);

        }
    }

}