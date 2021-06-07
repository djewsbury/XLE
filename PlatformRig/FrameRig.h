// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../RenderCore/IDevice_Forward.h"
#include "../Utility/FunctionUtils.h"
#include <functional>
#include <memory>

namespace RenderCore { class IThreadContext; }
namespace RenderCore { namespace Techniques { class ParsingContext; class IImmediateDrawables; class SubFrameEvents; class FrameRenderingApparatus; class DrawingApparatus; }}
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
            float _elapsedTime = 0.f;
            bool _hasPendingResources = false;
        };

        FrameResult ExecuteFrame(
            WindowApparatus& windowApparatus,
			RenderCore::Techniques::FrameRenderingApparatus& frameRenderingApparatus,
            RenderCore::Techniques::DrawingApparatus* drawingApparatus = nullptr);

        FrameResult ExecuteFrame(
            std::shared_ptr<RenderCore::IThreadContext> context,
            RenderCore::IPresentationChain& presChain,
			RenderCore::Techniques::ParsingContext& parserContext,
            Utility::HierarchicalCPUProfiler* profiler);

        void SetFrameLimiter(unsigned maxFPS);
        void UpdatePresentationChain(RenderCore::IPresentationChain& presChain);

        void SetMainOverlaySystem(std::shared_ptr<IOverlaySystem>);
		void SetDebugScreensOverlaySystem(std::shared_ptr<IOverlaySystem>);

        const std::shared_ptr<IOverlaySystem>& GetMainOverlaySystem() { return _mainOverlaySys; }
		const std::shared_ptr<IOverlaySystem>& GetDebugScreensOverlaySystem() { return _debugScreenOverlaySystem; }
        const std::shared_ptr<RenderCore::Techniques::SubFrameEvents>& GetSubFrameEvents() { return _subFrameEvents; }

        auto CreateDisplay(std::shared_ptr<RenderOverlays::DebuggingDisplay::DebugScreensSystem> debugSystem) -> std::shared_ptr<RenderOverlays::DebuggingDisplay::IWidget>;

        FrameRig(
            const std::shared_ptr<RenderCore::Techniques::SubFrameEvents>& subFrameEvents);
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
