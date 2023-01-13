// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../RenderCore/IDevice_Forward.h"
#include "../Utility/FunctionUtils.h"
#include <functional>
#include <memory>

namespace RenderCore { class IThreadContext; }
namespace RenderCore { namespace Techniques { class ParsingContext; class IImmediateDrawables; class SubFrameEvents; class FrameRenderingApparatus; class DrawingApparatus; class TechniqueContext; }}
namespace RenderOverlays { class FontRenderingManager; }
namespace RenderOverlays { namespace DebuggingDisplay { class DebugScreensSystem; class IWidget; }}
namespace Utility { class HierarchicalCPUProfiler; }

namespace PlatformRig
{
    class IOverlaySystem;
    class WindowApparatus;

    class FrameRig
    {
    public:
        struct FrameResult
        {
            float _intervalTime = 0.f;
            bool _hasPendingResources = false;
        };

        FrameResult ExecuteFrame(
            WindowApparatus& windowApparatus);

        FrameResult ExecuteFrame(
            std::shared_ptr<RenderCore::IThreadContext> context,
            std::shared_ptr<RenderCore::IPresentationChain> presChain);

        auto ExecuteFrame(
            std::shared_ptr<RenderCore::IThreadContext> context,
            std::shared_ptr<RenderCore::IPresentationChain> presChain,
            RenderCore::Techniques::ParsingContext& parserContext) -> FrameResult;

        void IntermedialSleep(
            RenderCore::IThreadContext& threadContext,
            bool inBackground,
            const FrameResult& lastFrameResult);

        void IntermedialSleep(
            WindowApparatus& windowApparatus,
            bool inBackground,
            const FrameResult& lastFrameResult);

        void UpdatePresentationChain(RenderCore::IPresentationChain& presChain);
        void ReleaseDoubleBufferAttachments();

        void SetMainOverlaySystem(std::shared_ptr<IOverlaySystem>);
		void SetDebugScreensOverlaySystem(std::shared_ptr<IOverlaySystem>);

        const std::shared_ptr<IOverlaySystem>& GetMainOverlaySystem() { return _mainOverlaySys; }
		const std::shared_ptr<IOverlaySystem>& GetDebugScreensOverlaySystem() { return _debugScreenOverlaySystem; }
        const std::shared_ptr<RenderCore::Techniques::SubFrameEvents>& GetSubFrameEvents() { return _subFrameEvents; }
        RenderCore::Techniques::TechniqueContext& GetTechniqueContext();

        auto CreateDisplay(std::shared_ptr<RenderOverlays::DebuggingDisplay::DebugScreensSystem> debugSystem) -> std::shared_ptr<RenderOverlays::DebuggingDisplay::IWidget>;

        FrameRig(
            RenderCore::Techniques::FrameRenderingApparatus& frameRenderingApparatus,
            RenderCore::Techniques::DrawingApparatus* drawingApparatus = nullptr);
        ~FrameRig();

        FrameRig(const FrameRig&) = delete;
        FrameRig& operator=(const FrameRig& cloneFrom) = delete;

    protected:
        std::shared_ptr<IOverlaySystem> _mainOverlaySys;
		std::shared_ptr<IOverlaySystem> _debugScreenOverlaySystem;
        std::shared_ptr<RenderCore::Techniques::SubFrameEvents> _subFrameEvents;

        class Pimpl;
        std::unique_ptr<Pimpl> _pimpl;
    };
}
