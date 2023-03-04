// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "OverlaySystem.h"
#include "../PlatformRig/DebuggingDisplays/ConsoleDisplay.h"
#include "../RenderOverlays/OverlayApparatus.h"
#include "../RenderOverlays/OverlayContext.h"
#include "../RenderOverlays/OverlayEffects.h"
#include "../RenderOverlays/DebuggingDisplay.h"
#include "../RenderOverlays/ShapesRendering.h"
#include "../RenderCore/IDevice.h"
#include "../RenderCore/Techniques/ParsingContext.h"
#include "../RenderCore/Techniques/RenderPassUtils.h"
#include "../RenderCore/Techniques/RenderPass.h"
#include "../RenderCore/Techniques/ImmediateDrawables.h"
#include "../RenderCore/Techniques/Apparatuses.h"
#include "../Assets/Assets.h"
#include "../ConsoleRig/Console.h"

using namespace PlatformRig::Literals;

namespace PlatformRig
{
///////////////////////////////////////////////////////////////////////////////////////////////////

    ProcessInputResult    OverlaySystemSwitch::ProcessInput(const InputContext& context, const OSServices::InputSnapshot& evnt)
    {
        using namespace RenderOverlays::DebuggingDisplay;
        constexpr auto shiftKey = "shift"_key;
        if (evnt.IsHeld(shiftKey)) {
            for (auto i=_childSystems.cbegin(); i!=_childSystems.cend(); ++i) {
                if (evnt.IsPress(i->first)) {
                    auto newIndex = std::distance(_childSystems.cbegin(), i);

                    if (_activeChildIndex >= 0 && _activeChildIndex < signed(_childSystems.size())) {
                        _childSystems[_activeChildIndex].second->SetActivationState(false);
                    }
                        
                    if (signed(newIndex) != _activeChildIndex) {
                        _activeChildIndex = signed(newIndex);
                        if (_activeChildIndex >= 0 && _activeChildIndex < signed(_childSystems.size())) {
                            _childSystems[_activeChildIndex].second->SetActivationState(true);
                        }
                    } else {
                        _activeChildIndex = -1;
                    }

                    return ProcessInputResult::Consumed;
                }
            }
        }

        if (_activeChildIndex >= 0 && _activeChildIndex < signed(_childSystems.size())) {

                //  if we have an active overlay system, we always consume all input!
                //  Nothing gets through to the next level
            return _childSystems[_activeChildIndex].second->ProcessInput(context, evnt);
        }

        return ProcessInputResult::Passthrough;
    }

    void OverlaySystemSwitch::Render(
        RenderCore::Techniques::ParsingContext& parserContext) 
    {
        if (_activeChildIndex >= 0 && _activeChildIndex < signed(_childSystems.size())) {
            _childSystems[_activeChildIndex].second->Render(parserContext);
        }
    }

    void OverlaySystemSwitch::SetActivationState(bool newState) 
    {
        if (!newState) {
            if (_activeChildIndex >= 0 && _activeChildIndex < signed(_childSystems.size())) {
                _childSystems[_activeChildIndex].second->SetActivationState(false);
            }
            _activeChildIndex = -1;
        }
    }

	auto OverlaySystemSwitch::GetOverlayState() const -> OverlayState
	{
		if (_activeChildIndex >= 0 && _activeChildIndex < signed(_childSystems.size()))
            return _childSystems[_activeChildIndex].second->GetOverlayState();
        return {};
	}

    void OverlaySystemSwitch::AddSystem(uint32_t activator, std::shared_ptr<IOverlaySystem> system)
    {
        auto* sys = system.get();
        _childSystems.push_back(std::make_pair(activator, std::move(system)));

        if (!_preregisteredAttachments.empty())
            sys->OnRenderTargetUpdate(_preregisteredAttachments, _fbProps, _systemAttachmentFormats);
    }

    void OverlaySystemSwitch::OnRenderTargetUpdate(
        IteratorRange<const RenderCore::Techniques::PreregisteredAttachment*> preregAttachments,
        const RenderCore::FrameBufferProperties& fbProps,
        IteratorRange<const RenderCore::Format*> systemAttachmentFormats)
    {
        // We could potentially avoid calling this on inactive children; but we would then have to 
        // call it when they become active
        for (const auto&c:_childSystems)
            c.second->OnRenderTargetUpdate(preregAttachments, fbProps, systemAttachmentFormats);

        _preregisteredAttachments = {preregAttachments.begin(), preregAttachments.end()};
        _fbProps = fbProps;
        _systemAttachmentFormats = {systemAttachmentFormats.begin(), systemAttachmentFormats.end()};
    }

    OverlaySystemSwitch::OverlaySystemSwitch() 
    : _activeChildIndex(-1)
    {}

    OverlaySystemSwitch::~OverlaySystemSwitch() {}

///////////////////////////////////////////////////////////////////////////////////////////////////

    ProcessInputResult OverlaySystemSet::ProcessInput(
        const InputContext& context,
        const OSServices::InputSnapshot& evnt)
    {
        for (auto i=_childSystems.begin(); i!=_childSystems.end(); ++i) {
            auto c = (*i)->ProcessInput(context, evnt);
            if (c != ProcessInputResult::Passthrough)
                return c;
        }

        return ProcessInputResult::Passthrough;
    }

    void OverlaySystemSet::Render(
        RenderCore::Techniques::ParsingContext& parsingContext) 
    {
        for (auto i=_childSystems.begin(); i!=_childSystems.end(); ++i) {
            (*i)->Render(parsingContext);
        }
    }

    void OverlaySystemSet::SetActivationState(bool newState) 
    {
        for (auto i=_childSystems.begin(); i!=_childSystems.end(); ++i) {
            (*i)->SetActivationState(newState);
        }
    }

	auto OverlaySystemSet::GetOverlayState() const -> OverlayState
	{
		OverlayState result;
		for (auto i=_childSystems.begin(); i!=_childSystems.end(); ++i) {
			auto childState = (*i)->GetOverlayState();
			if (childState._refreshMode == RefreshMode::RegularAnimation)
				result._refreshMode = RefreshMode::RegularAnimation;
		}
		return result;
	}

    void OverlaySystemSet::AddSystem(std::shared_ptr<IOverlaySystem> system)
    {
        auto* sys = system.get();
        _childSystems.push_back(std::move(system));
            // todo -- do we need to call SetActivationState() here?

        if (!_preregisteredAttachments.empty())
            sys->OnRenderTargetUpdate(_preregisteredAttachments, _fbProps, _systemAttachmentFormats);
    }

	void OverlaySystemSet::RemoveSystem(IOverlaySystem& system)
    {
		for (auto i=_childSystems.begin(); i!=_childSystems.end(); ++i)
			if (i->get() == &system) {
				_childSystems.erase(i);
				return;
			}
	}

    void OverlaySystemSet::OnRenderTargetUpdate(
        IteratorRange<const RenderCore::Techniques::PreregisteredAttachment*> preregAttachments,
        const RenderCore::FrameBufferProperties& fbProps,
        IteratorRange<const RenderCore::Format*> systemAttachmentFormats)
    {
        for (const auto&c:_childSystems)
            c->OnRenderTargetUpdate(preregAttachments, fbProps, systemAttachmentFormats);

        _preregisteredAttachments = {preregAttachments.begin(), preregAttachments.end()};
        _fbProps = fbProps;
        _systemAttachmentFormats = {systemAttachmentFormats.begin(), systemAttachmentFormats.end()};
    }

    OverlaySystemSet::OverlaySystemSet() 
    : _activeChildIndex(-1)
    {
    }

    OverlaySystemSet::~OverlaySystemSet() 
    {
    }

///////////////////////////////////////////////////////////////////////////////////////////////////

	void IOverlaySystem::SetActivationState(bool newState) {}
	auto IOverlaySystem::GetOverlayState() const -> OverlayState { return {}; }
    void IOverlaySystem::OnRenderTargetUpdate(
        IteratorRange<const RenderCore::Techniques::PreregisteredAttachment*> preregAttachments,
        const RenderCore::FrameBufferProperties& fbProps,
        IteratorRange<const RenderCore::Format*> systemAttachmentFormats) {}
    ProcessInputResult IOverlaySystem::ProcessInput(
        const InputContext& context,
        const OSServices::InputSnapshot& evnt) { return ProcessInputResult::Passthrough; }
    IOverlaySystem::~IOverlaySystem() {}

///////////////////////////////////////////////////////////////////////////////////////////////////

    class ConsoleOverlaySystem : public IOverlaySystem
    {
    public:
        virtual ProcessInputResult ProcessInput(
			const InputContext& context,
			const OSServices::InputSnapshot& evnt) override;
        void Render(
            RenderCore::Techniques::ParsingContext& parserContext) override;
        void SetActivationState(bool) override;

        ConsoleOverlaySystem(
            std::shared_ptr<RenderCore::Techniques::IImmediateDrawables> immediateDrawables,
            std::shared_ptr<RenderOverlays::ShapesRenderingDelegate> sequencerConfigSet,
            std::shared_ptr<RenderOverlays::FontRenderingManager> fontRenderer);
        ~ConsoleOverlaySystem();

    private:
        typedef RenderOverlays::DebuggingDisplay::DebugScreensSystem DebugScreensSystem;
        std::shared_ptr<DebugScreensSystem> _screens;
        std::shared_ptr<RenderCore::Techniques::IImmediateDrawables> _immediateDrawables;
        std::shared_ptr<RenderOverlays::ShapesRenderingDelegate> _sequencerConfigSet;
        std::shared_ptr<RenderOverlays::FontRenderingManager> _fontRenderer;
    };

    ProcessInputResult ConsoleOverlaySystem::ProcessInput(
        const InputContext& context,
        const OSServices::InputSnapshot& evnt)
    {
        return _screens->OnInputEvent(context, evnt);
    }

    void ConsoleOverlaySystem::Render(
        RenderCore::Techniques::ParsingContext& parserContext)
    {
		auto overlayContext = RenderOverlays::MakeImmediateOverlayContext(parserContext.GetThreadContext(), *_immediateDrawables, _fontRenderer.get());

        RenderOverlays::BlurryBackgroundEffect blurryBackground { parserContext };
        overlayContext->AttachService2(blurryBackground);

        Int2 viewportDims{ parserContext.GetViewport()._width, parserContext.GetViewport()._height };
        assert(viewportDims[0] * viewportDims[1]);
        _screens->Render(*overlayContext, RenderOverlays::Rect{ {0,0}, viewportDims });

		auto rpi = RenderCore::Techniques::RenderPassToPresentationTarget(parserContext);
        _immediateDrawables->ExecuteDraws(parserContext, _sequencerConfigSet->GetTechniqueDelegate(), rpi);
    }

    void ConsoleOverlaySystem::SetActivationState(bool) {}

    ConsoleOverlaySystem::ConsoleOverlaySystem(
        std::shared_ptr<RenderCore::Techniques::IImmediateDrawables> immediateDrawables,
        std::shared_ptr<RenderOverlays::ShapesRenderingDelegate> sequencerConfigSet,
        std::shared_ptr<RenderOverlays::FontRenderingManager> fontRenderer)
    : _immediateDrawables(std::move(immediateDrawables))
    , _sequencerConfigSet(std::move(sequencerConfigSet))
    , _fontRenderer(std::move(fontRenderer))
    {
        _screens = std::make_shared<DebugScreensSystem>();

        auto consoleDisplay = std::make_shared<PlatformRig::Overlays::ConsoleDisplay>(
            std::ref(ConsoleRig::Console::GetInstance()));
        _screens->Register(consoleDisplay, "[Console] Console", DebugScreensSystem::SystemDisplay);
    }

    ConsoleOverlaySystem::~ConsoleOverlaySystem()
    {
    }

    std::shared_ptr<IOverlaySystem> CreateConsoleOverlaySystem(
        std::shared_ptr<RenderCore::Techniques::IImmediateDrawables> immediateDrawables,
        std::shared_ptr<RenderOverlays::ShapesRenderingDelegate> sequencerConfigSet,
        std::shared_ptr<RenderOverlays::FontRenderingManager> fontRenderer)
    {
        return std::make_shared<ConsoleOverlaySystem>(std::move(immediateDrawables), std::move(sequencerConfigSet), std::move(fontRenderer));
    }

    std::shared_ptr<IOverlaySystem> CreateConsoleOverlaySystem(
        RenderOverlays::OverlayApparatus& immediateDrawing)
    {
        return std::make_shared<ConsoleOverlaySystem>(immediateDrawing._immediateDrawables, immediateDrawing._shapeRenderingDelegate, immediateDrawing._fontRenderingManager);
    }

///////////////////////////////////////////////////////////////////////////////////////////////////

    class BridgingInputListener : public IInputListener
    {
    public:
        ProcessInputResult OnInputEvent(
			const InputContext& context,
			const OSServices::InputSnapshot& evnt)
        {
            return _overlays->ProcessInput(context, evnt);
        }
        BridgingInputListener(std::shared_ptr<IOverlaySystem> overlays) : _overlays(std::move(overlays)) {}
    private:
        std::shared_ptr<IOverlaySystem> _overlays;
    };

    std::shared_ptr<IInputListener> CreateInputListener(std::shared_ptr<IOverlaySystem> overlays)
    {
        return std::make_shared<BridgingInputListener>(std::move(overlays));
    }

}

