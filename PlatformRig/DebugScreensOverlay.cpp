// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "DebugScreensOverlay.h"
#include "OverlaySystem.h"
#include "MainInputHandler.h"
#include "TopBar.h"
#include "../RenderOverlays/DebuggingDisplay.h"
#include "../RenderOverlays/OverlayContext.h"
#include "../RenderOverlays/OverlayEffects.h"
#include "../RenderOverlays/ShapesRendering.h"
#include "../RenderCore/IDevice.h"
#include "../RenderCore/Techniques/RenderPassUtils.h"
#include "../RenderCore/Techniques/RenderPass.h"
#include "../RenderCore/Techniques/ImmediateDrawables.h"
#include "../RenderCore/Techniques/ParsingContext.h"
#include "../RenderCore/Techniques/CommonBindings.h"
#include "../Math/Vector.h"

using namespace PlatformRig::Literals;

namespace PlatformRig
{
    class DebugScreensOverlay : public IOverlaySystem
    {
    public:
        DebugScreensOverlay(
            std::shared_ptr<RenderOverlays::DebuggingDisplay::DebugScreensSystem> debugScreensSystem,
            std::shared_ptr<RenderCore::Techniques::IImmediateDrawables> immediateDrawables,
            std::shared_ptr<RenderOverlays::ShapesRenderingDelegate> sequencerConfigSet,
            std::shared_ptr<RenderOverlays::FontRenderingManager> fontRenderer)
        : _debugScreensSystem(debugScreensSystem)
        , _immediateDrawables(std::move(immediateDrawables))
        , _sequencerConfigSet(std::move(sequencerConfigSet))
        , _fontRenderer(std::move(fontRenderer))
        {
        }

        ProcessInputResult ProcessInput(const InputContext& context, const OSServices::InputSnapshot& evnt) override
        {
            constexpr auto escape = "escape"_key;
            if (evnt.IsPress(escape)) {
                if (_debugScreensSystem && _debugScreensSystem->CurrentScreen(0)) {
                    _debugScreensSystem->SwitchToScreen(0, StringSection<>{});
                    return ProcessInputResult::Consumed;
                }
            }

            if (_debugScreensSystem)
                return _debugScreensSystem->OnInputEvent(context, evnt);
            return ProcessInputResult::Passthrough;
        }

        void Render(
            RenderCore::Techniques::ParsingContext& parserContext) override
        {
            TRY {
                Int2 viewportDims{ parserContext.GetViewport()._width, parserContext.GetViewport()._height };
                assert(viewportDims[0] * viewportDims[1]);

                auto overlayContext = RenderOverlays::MakeImmediateOverlayContext(parserContext.GetThreadContext(), *_immediateDrawables, _fontRenderer.get());

                RenderOverlays::BlurryBackgroundEffect blurryBackground { parserContext };
                overlayContext->AttachService2(blurryBackground);
                overlayContext->AttachService2(parserContext);

                auto topBarManager = CreateTopBarManager({{0,0}, viewportDims});
                overlayContext->AttachService2(*topBarManager);
            
                _debugScreensSystem->Render(*overlayContext, RenderOverlays::Rect{ {0,0}, viewportDims });
                if (_debugScreensSystem->IsAnyPanelActive())        // Since we sometimes use this in GUI tools, don't force rendering of this if there are no debugging screens open currently
                    topBarManager->RenderFrame(*overlayContext);

                RenderCore::Techniques::RenderPassInstance rpi;
                auto i = std::find_if(
                    parserContext.GetFragmentStitchingContext().GetPreregisteredAttachments().begin(), parserContext.GetFragmentStitchingContext().GetPreregisteredAttachments().end(),
                    [](const auto& c) { return c._semantic == RenderCore::Techniques::AttachmentSemantics::MultisampleDepth; });
                if (i != parserContext.GetFragmentStitchingContext().GetPreregisteredAttachments().end()) {
                    rpi = RenderCore::Techniques::RenderPassToPresentationTargetWithDepthStencil(parserContext);
                } else {
                    rpi = RenderCore::Techniques::RenderPassToPresentationTarget(parserContext);
                }
                parserContext.RequireCommandList(overlayContext->GetRequiredBufferUploadsCommandList());
                _immediateDrawables->ExecuteDraws(parserContext, _sequencerConfigSet->GetTechniqueDelegate(), rpi);
            } CATCH (...) {
                _immediateDrawables->AbandonDraws();
                throw;
            } CATCH_END
        }

    private:
        std::shared_ptr<RenderOverlays::DebuggingDisplay::DebugScreensSystem> _debugScreensSystem;
        std::shared_ptr<RenderCore::Techniques::IImmediateDrawables> _immediateDrawables;
        std::shared_ptr<RenderOverlays::FontRenderingManager> _fontRenderer;
        std::shared_ptr<RenderOverlays::ShapesRenderingDelegate> _sequencerConfigSet;
    };

    std::shared_ptr<IOverlaySystem> CreateDebugScreensOverlay(
        std::shared_ptr<RenderOverlays::DebuggingDisplay::DebugScreensSystem> debugScreensSystem,
        std::shared_ptr<RenderCore::Techniques::IImmediateDrawables> immediateDrawables,
        std::shared_ptr<RenderOverlays::ShapesRenderingDelegate> sequencerConfigSet,
        std::shared_ptr<RenderOverlays::FontRenderingManager> fontRenderer)
    {
        return std::make_shared<DebugScreensOverlay>(std::move(debugScreensSystem), std::move(immediateDrawables), std::move(sequencerConfigSet), std::move(fontRenderer));
    }

}

