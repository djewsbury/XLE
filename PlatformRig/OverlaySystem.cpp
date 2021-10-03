// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "OverlaySystem.h"
#include "../../PlatformRig/DebuggingDisplays/ConsoleDisplay.h"
#include "../../RenderOverlays/OverlayContext.h"
#include "../../RenderCore/IThreadContext.h"
#include "../../RenderCore/IDevice.h"
#include "../../RenderCore/Techniques/ParsingContext.h"
#include "../../RenderCore/Techniques/RenderPassUtils.h"
#include "../../RenderCore/Techniques/RenderPass.h"
#include "../../RenderCore/Techniques/ImmediateDrawables.h"
#include "../../RenderCore/Techniques/Apparatuses.h"
#include "../../RenderOverlays/DebuggingDisplay.h"
#include "../../Assets/Assets.h"
#include "../../ConsoleRig/Console.h"

namespace PlatformRig
{
///////////////////////////////////////////////////////////////////////////////////////////////////

    class OverlaySystemSwitch::InputListener : public IInputListener
    {
    public:
        virtual bool    OnInputEvent(const InputContext& context, const InputSnapshot& evnt)
        {
            using namespace RenderOverlays::DebuggingDisplay;
            static const KeyId shiftKey = KeyId_Make("shift");
            if (evnt.IsHeld(shiftKey)) {
                for (auto i=_parent->_childSystems.cbegin(); i!=_parent->_childSystems.cend(); ++i) {
                    if (evnt.IsPress(i->first)) {
                        auto newIndex = std::distance(_parent->_childSystems.cbegin(), i);

                        if (_parent->_activeChildIndex >= 0 && _parent->_activeChildIndex < signed(_parent->_childSystems.size())) {
                            _parent->_childSystems[_parent->_activeChildIndex].second->SetActivationState(false);
                        }
                            
                        if (signed(newIndex) != _parent->_activeChildIndex) {
                            _parent->_activeChildIndex = signed(newIndex);
                            if (_parent->_activeChildIndex >= 0 && _parent->_activeChildIndex < signed(_parent->_childSystems.size())) {
                                _parent->_childSystems[_parent->_activeChildIndex].second->SetActivationState(true);
                            }
                        } else {
                            _parent->_activeChildIndex = -1;
                        }

                        return true;
                    }
                }
            }

            if (_parent->_activeChildIndex >= 0 && _parent->_activeChildIndex < signed(_parent->_childSystems.size())) {

                    //  if we have an active overlay system, we always consume all input!
                    //  Nothing gets through to the next level
                _parent->_childSystems[_parent->_activeChildIndex].second->GetInputListener()->OnInputEvent(context, evnt);
                return true;
            }

            return false;
        }

        InputListener(OverlaySystemSwitch* parent) : _parent(parent) {}
    protected:
        OverlaySystemSwitch* _parent;
    };

    std::shared_ptr<IInputListener> OverlaySystemSwitch::GetInputListener()
    {
        return _inputListener;
    }

    void OverlaySystemSwitch::Render(
        RenderCore::IThreadContext& device, RenderCore::Techniques::ParsingContext& parserContext) 
    {
        if (_activeChildIndex >= 0 && _activeChildIndex < signed(_childSystems.size())) {
            _childSystems[_activeChildIndex].second->Render(device, parserContext);
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
            sys->OnRenderTargetUpdate(_preregisteredAttachments, _fbProps);
    }

    void OverlaySystemSwitch::OnRenderTargetUpdate(
        IteratorRange<const RenderCore::Techniques::PreregisteredAttachment*> preregAttachments,
        const RenderCore::FrameBufferProperties& fbProps)
    {
        // We could potentially avoid calling this on inactive children; but we would then have to 
        // call it when they become active
        for (const auto&c:_childSystems)
            c.second->OnRenderTargetUpdate(preregAttachments, fbProps);

        _preregisteredAttachments = {preregAttachments.begin(), preregAttachments.end()};
        _fbProps = fbProps;
    }

    OverlaySystemSwitch::OverlaySystemSwitch() 
    : _activeChildIndex(-1)
    {
        _inputListener = std::make_shared<InputListener>(this);
    }

    OverlaySystemSwitch::~OverlaySystemSwitch() {}

///////////////////////////////////////////////////////////////////////////////////////////////////

    class OverlaySystemSet::InputListener : public IInputListener
    {
    public:
        virtual bool    OnInputEvent(const InputContext& context, const InputSnapshot& evnt)
        {
            if (!_parent) return false;
            
            for (auto i=_parent->_childSystems.begin(); i!=_parent->_childSystems.end(); ++i) {
                auto listener = (*i)->GetInputListener();
                if (listener && listener->OnInputEvent(context, evnt)) {
                    return true;
                }
            }

            return false;
        }

        void ReleaseOverlaySystemSet() { _parent = nullptr; }

        InputListener(OverlaySystemSet* parent) : _parent(parent) {}
    protected:
        OverlaySystemSet* _parent;
    };

    std::shared_ptr<IInputListener> OverlaySystemSet::GetInputListener()         { return _inputListener; }

    void OverlaySystemSet::Render(
        RenderCore::IThreadContext& device,
        RenderCore::Techniques::ParsingContext& parsingContext) 
    {
        for (auto i=_childSystems.begin(); i!=_childSystems.end(); ++i) {
            (*i)->Render(device, parsingContext);
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
            sys->OnRenderTargetUpdate(_preregisteredAttachments, _fbProps);
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
        const RenderCore::FrameBufferProperties& fbProps)
    {
        for (const auto&c:_childSystems)
            c->OnRenderTargetUpdate(preregAttachments, fbProps);

        _preregisteredAttachments = {preregAttachments.begin(), preregAttachments.end()};
        _fbProps = fbProps;
    }

    OverlaySystemSet::OverlaySystemSet() 
    : _activeChildIndex(-1)
    {
        _inputListener = std::make_shared<InputListener>(this);
    }

    OverlaySystemSet::~OverlaySystemSet() 
    {
        if (_inputListener)
            _inputListener->ReleaseOverlaySystemSet();
    }

///////////////////////////////////////////////////////////////////////////////////////////////////

	std::shared_ptr<IInputListener> IOverlaySystem::GetInputListener() { return nullptr; }
	void IOverlaySystem::SetActivationState(bool newState) {}
	auto IOverlaySystem::GetOverlayState() const -> OverlayState { return {}; }
    void IOverlaySystem::OnRenderTargetUpdate(
        IteratorRange<const RenderCore::Techniques::PreregisteredAttachment*> preregAttachments,
        const RenderCore::FrameBufferProperties& fbProps) {}
    IOverlaySystem::~IOverlaySystem() {}

///////////////////////////////////////////////////////////////////////////////////////////////////

    class ConsoleOverlaySystem : public IOverlaySystem
    {
    public:
        std::shared_ptr<IInputListener> GetInputListener() override;
        void Render(
            RenderCore::IThreadContext& device,
            RenderCore::Techniques::ParsingContext& parserContext) override;
        void SetActivationState(bool) override;

        ConsoleOverlaySystem(
            std::shared_ptr<RenderCore::Techniques::IImmediateDrawables> immediateDrawables,
            std::shared_ptr<RenderOverlays::FontRenderingManager> fontRenderer);
        ~ConsoleOverlaySystem();

    private:
        typedef RenderOverlays::DebuggingDisplay::DebugScreensSystem DebugScreensSystem;
        std::shared_ptr<DebugScreensSystem> _screens;
        std::shared_ptr<RenderCore::Techniques::IImmediateDrawables> _immediateDrawables;
        std::shared_ptr<RenderOverlays::FontRenderingManager> _fontRenderer;
    };

    std::shared_ptr<IInputListener> ConsoleOverlaySystem::GetInputListener()
    {
        return _screens;
    }

    void ConsoleOverlaySystem::Render(
        RenderCore::IThreadContext& threadContext,
        RenderCore::Techniques::ParsingContext& parserContext)
    {
		auto overlayContext = RenderOverlays::MakeImmediateOverlayContext(threadContext, *_immediateDrawables, _fontRenderer.get());

        Int2 viewportDims{ parserContext.GetViewport()._width, parserContext.GetViewport()._height };
        assert(viewportDims[0] * viewportDims[1]);
        _screens->Render(*overlayContext, RenderOverlays::Rect{ {0,0}, viewportDims });

		auto rpi = RenderCore::Techniques::RenderPassToPresentationTarget(threadContext, parserContext);
        _immediateDrawables->ExecuteDraws(threadContext, parserContext, rpi);
    }

    void ConsoleOverlaySystem::SetActivationState(bool) {}

    ConsoleOverlaySystem::ConsoleOverlaySystem(
        std::shared_ptr<RenderCore::Techniques::IImmediateDrawables> immediateDrawables,
        std::shared_ptr<RenderOverlays::FontRenderingManager> fontRenderer)
    : _immediateDrawables(immediateDrawables)
    , _fontRenderer(fontRenderer)
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
        std::shared_ptr<RenderOverlays::FontRenderingManager> fontRenderer)
    {
        return std::make_shared<ConsoleOverlaySystem>(std::move(immediateDrawables), std::move(fontRenderer));
    }

    std::shared_ptr<IOverlaySystem> CreateConsoleOverlaySystem(
        RenderCore::Techniques::ImmediateDrawingApparatus& immediateDrawing)
    {
        return std::make_shared<ConsoleOverlaySystem>(immediateDrawing._immediateDrawables, immediateDrawing._fontRenderingManager);
    }

}

