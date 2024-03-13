// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../RenderCore/FrameBufferDesc.h"      // for FrameBufferProperties
#include "../RenderOverlays/DebuggingDisplay.h"
#include <memory>
#include <vector>

namespace RenderCore { class IThreadContext; }
namespace RenderCore { namespace Techniques { class ParsingContext; class SubFrameEvents; class FrameRenderingApparatus; class DrawingApparatus; class TechniqueContext; struct PreregisteredAttachment; }}
namespace RenderOverlays { class OverlayApparatus; }
namespace RenderOverlays { namespace DebuggingDisplay { class DebugScreensSystem; class IWidget;  }}
namespace Utility { class HierarchicalCPUProfiler; }
namespace Assets { class OperationContext; }

namespace PlatformRig
{
    class IOverlaySystem;
    class WindowApparatus;
    class IFrameRigDisplay;

    class FrameRig
    {
    public:
        struct FrameResult
        {
            float _intervalTime = 0.f;
            bool _hasPendingResources = false;
        };

        RenderCore::Techniques::ParsingContext StartupFrame(
            WindowApparatus& windowApparatus);
        RenderCore::Techniques::ParsingContext StartupFrame(
            std::shared_ptr<RenderCore::IThreadContext> context,
            std::shared_ptr<RenderCore::IPresentationChain> presChain);

        FrameResult ShutdownFrame(
            RenderCore::Techniques::ParsingContext& parsingContext,
            std::ostream* appLog = nullptr);

        void IntermedialSleep(
            RenderCore::IThreadContext& threadContext,
            bool inBackground,
            const FrameResult& lastFrameResult);

        void IntermedialSleep(
            WindowApparatus& windowApparatus,
            bool inBackground,
            const FrameResult& lastFrameResult);

        float GetSmoothedDeltaTime();

        void UpdatePresentationChain(RenderCore::IPresentationChain& presChain);
        void ReleaseDoubleBufferAttachments();

        struct OverlayConfiguration
        {
            std::vector<RenderCore::Techniques::PreregisteredAttachment> _preregAttachments;
            RenderCore::FrameBufferProperties _fbProps;
            std::vector<RenderCore::Format> _systemAttachmentFormats;
            uint64_t _hash = 0ull;
        };
        OverlayConfiguration GetOverlayConfiguration(RenderCore::IPresentationChain& presChain) const;

        RenderCore::Techniques::TechniqueContext& GetTechniqueContext();

        auto CreateDisplay(std::shared_ptr<RenderOverlays::DebuggingDisplay::DebugScreensSystem>, std::shared_ptr<Assets::OperationContext>)
            -> std::shared_ptr<IFrameRigDisplay>;

        FrameRig(
            RenderCore::Techniques::FrameRenderingApparatus& frameRenderingApparatus,
            RenderCore::Techniques::DrawingApparatus* drawingApparatus = nullptr);
        ~FrameRig();

        FrameRig(const FrameRig&) = delete;
        FrameRig& operator=(const FrameRig& cloneFrom) = delete;

    protected:
        std::shared_ptr<RenderCore::Techniques::SubFrameEvents> _subFrameEvents;

        class Pimpl;
        std::unique_ptr<Pimpl> _pimpl;
    };

    class IFrameRigDisplay : public RenderOverlays::DebuggingDisplay::IWidget
    {
    public:
        enum class Style { Normal, NonInteractive };
        virtual void SetStyle(Style) = 0;
        virtual void EnableMainStates(bool) = 0;
        virtual void SetLoadingContext(std::shared_ptr<Assets::OperationContext>) = 0;
    };
}
