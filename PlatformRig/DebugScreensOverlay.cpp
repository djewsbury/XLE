// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "DebugScreensOverlay.h"
#include "OverlaySystem.h"
#include "MainInputHandler.h"
#include "../RenderOverlays/DebuggingDisplay.h"
#include "../RenderOverlays/OverlayContext.h"
#include "../RenderCore/IDevice.h"
#include "../RenderCore/Techniques/RenderPassUtils.h"
#include "../RenderCore/Techniques/RenderPass.h"
#include "../RenderCore/Techniques/ImmediateDrawables.h"
#include "../RenderCore/Techniques/ParsingContext.h"
#include "../Math/Vector.h"

namespace PlatformRig
{
    
    class DebugScreensOverlay : public IOverlaySystem
    {
    public:
        DebugScreensOverlay(
            std::shared_ptr<RenderOverlays::DebuggingDisplay::DebugScreensSystem> debugScreensSystem,
            std::shared_ptr<RenderCore::Techniques::IImmediateDrawables> immediateDrawables,
            std::shared_ptr<RenderOverlays::FontRenderingManager> fontRenderer)
        : _debugScreensSystem(debugScreensSystem)
        , _inputListener(std::make_shared<PlatformRig::DebugScreensInputHandler>(std::move(debugScreensSystem)))
        , _immediateDrawables(std::move(immediateDrawables))
        , _fontRenderer(std::move(fontRenderer))
        {
        }

        std::shared_ptr<IInputListener> GetInputListener() override  { return _inputListener; }

        void Render(
            RenderCore::IThreadContext& threadContext,
            RenderCore::Techniques::ParsingContext& parserContext) override
        {
            TRY {
                auto overlayContext = RenderOverlays::MakeImmediateOverlayContext(threadContext, *_immediateDrawables, _fontRenderer.get());
            
                // todo -- we need the viewport that we're going to get when we begin the presentation target; not the viewport that we have now
                Int2 viewportDims{ parserContext.GetViewport()._width, parserContext.GetViewport()._height };
                assert(viewportDims[0] * viewportDims[1]);
                _debugScreensSystem->Render(*overlayContext, RenderOverlays::DebuggingDisplay::Rect{ {0,0}, viewportDims });

                auto rpi = RenderCore::Techniques::RenderPassToPresentationTarget(threadContext, parserContext);
                parserContext.RequireCommandList(overlayContext->GetRequiredBufferUploadsCommandList());
                _immediateDrawables->ExecuteDraws(threadContext, parserContext, rpi);
            } CATCH (...) {
                _immediateDrawables->AbandonDraws();
                throw;
            } CATCH_END
        }

    private:
        std::shared_ptr<RenderOverlays::DebuggingDisplay::DebugScreensSystem> _debugScreensSystem;
        std::shared_ptr<DebugScreensInputHandler> _inputListener;
        std::shared_ptr<RenderCore::Techniques::IImmediateDrawables> _immediateDrawables;
        std::shared_ptr<RenderOverlays::FontRenderingManager> _fontRenderer;
    };

    std::shared_ptr<IOverlaySystem> CreateDebugScreensOverlay(
        std::shared_ptr<RenderOverlays::DebuggingDisplay::DebugScreensSystem> debugScreensSystem,
        std::shared_ptr<RenderCore::Techniques::IImmediateDrawables> immediateDrawables,
        std::shared_ptr<RenderOverlays::FontRenderingManager> fontRenderer)
    {
        return std::make_shared<DebugScreensOverlay>(std::move(debugScreensSystem), std::move(immediateDrawables), std::move(fontRenderer));
    }

}

