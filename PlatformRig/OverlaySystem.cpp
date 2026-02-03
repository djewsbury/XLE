// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "IOverlay.h"
#include "../PlatformRig/DebuggingDisplays/ConsoleDisplay.h"
#include "../RenderOverlays/OverlayApparatus.h"
#include "../RenderOverlays/OverlayContext.h"
#include "../RenderOverlays/OverlayEffects.h"
#include "../RenderOverlays/DebuggingDisplay.h"
#include "../RenderOverlays/ShapesRendering.h"
#include "../RenderOverlays/LayoutEngine.h"
#include "../RenderCore/IDevice.h"
#include "../RenderCore/Techniques/ParsingContext.h"
#include "../RenderCore/Techniques/RenderPassUtils.h"
#include "../RenderCore/Techniques/RenderPass.h"
#include "../RenderCore/Techniques/DrawableSubmitter.h"
#include "../RenderCore/Techniques/Apparatuses.h"
#include "../Assets/Assets.h"
#include "../ConsoleRig/Console.h"

using namespace OSServices::Literals;

namespace PlatformRig
{
///////////////////////////////////////////////////////////////////////////////////////////////////

    struct OverlaySystemSwitch::Pimpl
    {
        signed _activeChildIndex, _defaultChildIndex;
        std::vector<std::pair<uint32_t,std::shared_ptr<IOverlay>>> _childSystems;

        std::vector<RenderCore::Techniques::PreregisteredAttachment> _preregisteredAttachments;
        RenderCore::FrameBufferProperties _fbProps;
        std::vector<RenderCore::Format> _systemAttachmentFormats;
    };

    ProcessInputResult    OverlaySystemSwitch::ProcessInput(const InputContext& context, const OSServices::InputSnapshot& evnt)
    {
        using namespace RenderOverlays::DebuggingDisplay;
        constexpr auto shiftKey = "shift"_key;
        if (evnt.IsHeld(shiftKey)) {
            for (auto i=_pimpl->_childSystems.cbegin(); i!=_pimpl->_childSystems.cend(); ++i) {
                if (evnt.IsPress(i->first)) {
                    auto newIndex = std::distance(_pimpl->_childSystems.cbegin(), i);

                    if (_pimpl->_activeChildIndex >= 0 && _pimpl->_activeChildIndex < signed(_pimpl->_childSystems.size()))
                        if (auto ext=dynamic_cast<IOverlayExtended*>(_pimpl->_childSystems[_pimpl->_activeChildIndex].second.get()))
                            ext->SetActivationState(false);

                    _pimpl->_activeChildIndex = (signed(newIndex) != _pimpl->_activeChildIndex) ? signed(newIndex) : _pimpl->_defaultChildIndex;
                    if (_pimpl->_activeChildIndex >= 0 && _pimpl->_activeChildIndex < signed(_pimpl->_childSystems.size()))
                        if (auto ext=dynamic_cast<IOverlayExtended*>(_pimpl->_childSystems[_pimpl->_activeChildIndex].second.get()))
                            ext->SetActivationState(true);

                    return ProcessInputResult::Consumed;
                }
            }
        }

        if (_pimpl->_activeChildIndex >= 0 && _pimpl->_activeChildIndex < signed(_pimpl->_childSystems.size())) {

                //  if we have an active overlay system, we always consume all input!
                //  Nothing gets through to the next level
            return _pimpl->_childSystems[_pimpl->_activeChildIndex].second->ProcessInput(context, evnt);
        }

        return ProcessInputResult::Passthrough;
    }

    void OverlaySystemSwitch::Render(
        RenderCore::Techniques::ParsingContext& parserContext) 
    {
        if (_pimpl->_activeChildIndex >= 0 && _pimpl->_activeChildIndex < signed(_pimpl->_childSystems.size()))
            _pimpl->_childSystems[_pimpl->_activeChildIndex].second->Render(parserContext);
    }

    void OverlaySystemSwitch::Update(float deltaTime)
    {
        if (_pimpl->_activeChildIndex >= 0 && _pimpl->_activeChildIndex < signed(_pimpl->_childSystems.size()))
            if (auto ut = dynamic_cast<IUpdateTick*>(_pimpl->_childSystems[_pimpl->_activeChildIndex].second.get()))
                ut->Update(deltaTime);
    }

    void OverlaySystemSwitch::SetActivationState(bool newState) 
    {
        if (!newState) {
            if (_pimpl->_activeChildIndex != _pimpl->_defaultChildIndex) {
                if (_pimpl->_activeChildIndex >= 0 && _pimpl->_activeChildIndex < signed(_pimpl->_childSystems.size()))
                    if (auto ext=dynamic_cast<IOverlayExtended*>(_pimpl->_childSystems[_pimpl->_activeChildIndex].second.get()))
                        ext->SetActivationState(false);
                _pimpl->_activeChildIndex = _pimpl->_defaultChildIndex;
            }
        } else {
            if (_pimpl->_activeChildIndex >= 0 && _pimpl->_activeChildIndex < signed(_pimpl->_childSystems.size()))
                if (auto ext=dynamic_cast<IOverlayExtended*>(_pimpl->_childSystems[_pimpl->_activeChildIndex].second.get()))
                    ext->SetActivationState(true);
        }
    }

	auto OverlaySystemSwitch::GetOverlayState() const -> OverlayState
	{
		if (_pimpl->_activeChildIndex >= 0 && _pimpl->_activeChildIndex < signed(_pimpl->_childSystems.size()))
            if (auto ext=dynamic_cast<IOverlayExtended*>(_pimpl->_childSystems[_pimpl->_activeChildIndex].second.get()))
                return ext->GetOverlayState();
        return {};
	}

    void OverlaySystemSwitch::AddSystem(uint32_t activator, std::shared_ptr<IOverlay> system)
    {
        auto* sys = system.get();
        _pimpl->_childSystems.push_back(std::make_pair(activator, std::move(system)));

        if (!_pimpl->_preregisteredAttachments.empty())
            sys->OnRenderTargetUpdate(_pimpl->_preregisteredAttachments, _pimpl->_fbProps, _pimpl->_systemAttachmentFormats);
    }

    void OverlaySystemSwitch::SetDefaultSystem(std::shared_ptr<IOverlay> system)
    {
        auto* sys = system.get();
        _pimpl->_childSystems.push_back(std::make_pair(0, std::move(system)));
        _pimpl->_defaultChildIndex = int(_pimpl->_childSystems.size()-1);

        if (!_pimpl->_preregisteredAttachments.empty())
            sys->OnRenderTargetUpdate(_pimpl->_preregisteredAttachments, _pimpl->_fbProps, _pimpl->_systemAttachmentFormats);
    }

    void OverlaySystemSwitch::OnRenderTargetUpdate(
        IteratorRange<const RenderCore::Techniques::PreregisteredAttachment*> preregAttachments,
        const RenderCore::FrameBufferProperties& fbProps,
        IteratorRange<const RenderCore::Format*> systemAttachmentFormats)
    {
        // We could potentially avoid calling this on inactive children; but we would then have to 
        // call it when they become active
        for (const auto&c:_pimpl->_childSystems)
            c.second->OnRenderTargetUpdate(preregAttachments, fbProps, systemAttachmentFormats);

        _pimpl->_preregisteredAttachments = {preregAttachments.begin(), preregAttachments.end()};
        _pimpl->_fbProps = fbProps;
        _pimpl->_systemAttachmentFormats = {systemAttachmentFormats.begin(), systemAttachmentFormats.end()};
    }

    OverlaySystemSwitch::OverlaySystemSwitch() 
    {
        _pimpl = std::make_unique<Pimpl>();
        _pimpl->_activeChildIndex = _pimpl->_defaultChildIndex = -1;
    }

    OverlaySystemSwitch::~OverlaySystemSwitch() {}

///////////////////////////////////////////////////////////////////////////////////////////////////

    struct OverlaySystemSet::Pimpl
    {
        signed _activeChildIndex;
        std::vector<std::shared_ptr<IOverlay>> _childSystems;

        std::vector<RenderCore::Techniques::PreregisteredAttachment> _preregisteredAttachments;
        RenderCore::FrameBufferProperties _fbProps;
        std::vector<RenderCore::Format> _systemAttachmentFormats;
    };

    ProcessInputResult OverlaySystemSet::ProcessInput(
        const InputContext& context,
        const OSServices::InputSnapshot& evnt)
    {
        for (auto i=_pimpl->_childSystems.begin(); i!=_pimpl->_childSystems.end(); ++i) {
            auto c = (*i)->ProcessInput(context, evnt);
            if (c != ProcessInputResult::Passthrough)
                return c;
        }

        return ProcessInputResult::Passthrough;
    }

    void OverlaySystemSet::Render(
        RenderCore::Techniques::ParsingContext& parsingContext) 
    {
        for (auto i=_pimpl->_childSystems.begin(); i!=_pimpl->_childSystems.end(); ++i) {
            (*i)->Render(parsingContext);
        }
    }

    void OverlaySystemSet::Update(float deltaTime)
    {
        for (auto i=_pimpl->_childSystems.begin(); i!=_pimpl->_childSystems.end(); ++i)
            if (auto ut = dynamic_cast<IUpdateTick*>(i->get()))
                ut->Update(deltaTime);
    }

    void OverlaySystemSet::SetActivationState(bool newState) 
    {
        for (auto i=_pimpl->_childSystems.begin(); i!=_pimpl->_childSystems.end(); ++i)
            if (auto ext=dynamic_cast<IOverlayExtended*>(i->get()))
                ext->SetActivationState(newState);
    }

	auto OverlaySystemSet::GetOverlayState() const -> OverlayState
	{
		OverlayState result;
		for (auto i=_pimpl->_childSystems.begin(); i!=_pimpl->_childSystems.end(); ++i)
            if (auto ext=dynamic_cast<IOverlayExtended*>(i->get()))
                if (auto childState = ext->GetOverlayState(); childState._refreshMode == RefreshMode::RegularAnimation)
                    result._refreshMode = RefreshMode::RegularAnimation;
		return result;
	}

    void OverlaySystemSet::AddSystem(std::shared_ptr<IOverlay> system)
    {
        auto* sys = system.get();
        _pimpl->_childSystems.push_back(std::move(system));
            // todo -- do we need to call SetActivationState() here?

        if (!_pimpl->_preregisteredAttachments.empty())
            sys->OnRenderTargetUpdate(_pimpl->_preregisteredAttachments, _pimpl->_fbProps, _pimpl->_systemAttachmentFormats);
    }

	void OverlaySystemSet::RemoveSystem(IOverlay& system)
    {
		for (auto i=_pimpl->_childSystems.begin(); i!=_pimpl->_childSystems.end(); ++i)
			if (i->get() == &system) {
				_pimpl->_childSystems.erase(i);
				return;
			}
	}

    void OverlaySystemSet::OnRenderTargetUpdate(
        IteratorRange<const RenderCore::Techniques::PreregisteredAttachment*> preregAttachments,
        const RenderCore::FrameBufferProperties& fbProps,
        IteratorRange<const RenderCore::Format*> systemAttachmentFormats)
    {
        for (const auto&c:_pimpl->_childSystems)
            c->OnRenderTargetUpdate(preregAttachments, fbProps, systemAttachmentFormats);

        _pimpl->_preregisteredAttachments = {preregAttachments.begin(), preregAttachments.end()};
        _pimpl->_fbProps = fbProps;
        _pimpl->_systemAttachmentFormats = {systemAttachmentFormats.begin(), systemAttachmentFormats.end()};
    }

    OverlaySystemSet::OverlaySystemSet() 
    {
        _pimpl = std::make_unique<Pimpl>();
        _pimpl->_activeChildIndex = -1;
    }

    OverlaySystemSet::~OverlaySystemSet() {}

///////////////////////////////////////////////////////////////////////////////////////////////////

    void IOverlay::Render(
        RenderCore::Techniques::ParsingContext& parserContext) {}
    void IOverlay::OnRenderTargetUpdate(
        IteratorRange<const RenderCore::Techniques::PreregisteredAttachment*> preregAttachments,
        const RenderCore::FrameBufferProperties& fbProps,
        IteratorRange<const RenderCore::Format*> systemAttachmentFormats) {}
    ProcessInputResult IOverlay::ProcessInput(
        const InputContext& context,
        const OSServices::InputSnapshot& evnt) { return ProcessInputResult::Passthrough; }
    IOverlay::~IOverlay() {}
    IUpdateTick::~IUpdateTick() {}
	void IOverlayExtended::SetActivationState(bool newState) {}
	auto IOverlayExtended::GetOverlayState() const -> OverlayState { return {}; }
    IOverlayExtended::~IOverlayExtended() {}

///////////////////////////////////////////////////////////////////////////////////////////////////

    class ConsoleOverlaySystem : public IOverlay
    {
    public:
        virtual ProcessInputResult ProcessInput(
			const InputContext& context,
			const OSServices::InputSnapshot& evnt) override;
        void Render(
            RenderCore::Techniques::ParsingContext& parserContext) override;

        ConsoleOverlaySystem(
            std::shared_ptr<RenderCore::Techniques::IDrawableSubmitter> immediateDrawables,
            std::shared_ptr<RenderOverlays::ShapesRenderingDelegate> sequencerConfigSet,
            std::shared_ptr<RenderOverlays::FontRenderingManager> fontRenderer);
        ~ConsoleOverlaySystem();

    private:
        std::shared_ptr<RenderOverlays::DebuggingDisplay::IWidget> _openWidget;
        std::shared_ptr<RenderOverlays::DebuggingDisplay::IWidget> _closedWidget;

        std::shared_ptr<RenderCore::Techniques::IDrawableSubmitter> _immediateDrawables;
        std::shared_ptr<RenderOverlays::ShapesRenderingDelegate> _sequencerConfigSet;
        std::shared_ptr<RenderOverlays::FontRenderingManager> _fontRenderer;

        RenderOverlays::DebuggingDisplay::InterfaceStateHelper _interfaceStateHelper;
        bool _open = false;
    };

    ProcessInputResult ConsoleOverlaySystem::ProcessInput(
        const InputContext& context,
        const OSServices::InputSnapshot& evnt)
    {
        _interfaceStateHelper.OnInputEvent(context, evnt);

        if (evnt.IsPress("~"_key)) {
            _open = !_open;
            return ProcessInputResult::Consumed;
        }

        if (_open) {
            return _openWidget->ProcessInput(_interfaceStateHelper._currentInterfaceState, evnt);
        } else {
            return _closedWidget->ProcessInput(_interfaceStateHelper._currentInterfaceState, evnt);
        }
    }

    void ConsoleOverlaySystem::Render(
        RenderCore::Techniques::ParsingContext& parserContext)
    {
        _interfaceStateHelper.PreRender();

		auto overlayContext = RenderOverlays::MakeImmediateOverlayContext(parserContext.GetThreadContext(), *_immediateDrawables, _fontRenderer.get());

        // RenderOverlays::BlurryBackgroundEffect blurryBackground { parserContext };
        // overlayContext->AttachService2(blurryBackground);

        Int2 viewportDims{ parserContext.GetViewport()._width, parserContext.GetViewport()._height };
        assert(viewportDims[0] * viewportDims[1]);
        RenderOverlays::ImmediateLayout layout(RenderOverlays::Rect{ {0,0}, viewportDims });
        if (_open) {
            _openWidget->Render(*overlayContext, layout, _interfaceStateHelper._currentInteractables, _interfaceStateHelper._currentInterfaceState);
        } else {
            _closedWidget->Render(*overlayContext, layout, _interfaceStateHelper._currentInteractables, _interfaceStateHelper._currentInterfaceState);
        }

        if (!_immediateDrawables->IsEmpty()) {
            auto rpi = RenderCore::Techniques::RenderPassToPresentationTarget(parserContext);
            _immediateDrawables->ExecuteDraws(parserContext, _sequencerConfigSet->GetTechniqueDelegate(), rpi);
        }

        _interfaceStateHelper.PostRender();
    }

    ConsoleOverlaySystem::ConsoleOverlaySystem(
        std::shared_ptr<RenderCore::Techniques::IDrawableSubmitter> immediateDrawables,
        std::shared_ptr<RenderOverlays::ShapesRenderingDelegate> sequencerConfigSet,
        std::shared_ptr<RenderOverlays::FontRenderingManager> fontRenderer)
    : _immediateDrawables(std::move(immediateDrawables))
    , _sequencerConfigSet(std::move(sequencerConfigSet))
    , _fontRenderer(std::move(fontRenderer))
    {
        _openWidget = std::make_shared<PlatformRig::Overlays::ConsoleDisplay>(std::ref(ConsoleRig::Console::GetInstance()));
        _closedWidget = std::make_shared<PlatformRig::Overlays::ConsoleRecentMsgsDisplay>(std::ref(ConsoleRig::Console::GetInstance()));
        _open = false;
    }

    ConsoleOverlaySystem::~ConsoleOverlaySystem()
    {
    }

    std::shared_ptr<IOverlay> CreateConsoleOverlaySystem(
        std::shared_ptr<RenderCore::Techniques::IDrawableSubmitter> immediateDrawables,
        std::shared_ptr<RenderOverlays::ShapesRenderingDelegate> sequencerConfigSet,
        std::shared_ptr<RenderOverlays::FontRenderingManager> fontRenderer)
    {
        return std::make_shared<ConsoleOverlaySystem>(std::move(immediateDrawables), std::move(sequencerConfigSet), std::move(fontRenderer));
    }

    std::shared_ptr<IOverlay> CreateConsoleOverlaySystem(
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
        BridgingInputListener(std::shared_ptr<IOverlay> overlays) : _overlays(std::move(overlays)) {}
    private:
        std::shared_ptr<IOverlay> _overlays;
    };

    std::shared_ptr<IInputListener> CreateInputListenerBridge(std::shared_ptr<IOverlay> overlays)
    {
        return std::make_shared<BridgingInputListener>(std::move(overlays));
    }

}

