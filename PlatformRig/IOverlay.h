// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../../Utility/IteratorUtils.h"
#include <memory>

namespace RenderCore { class IThreadContext; class FrameBufferProperties; enum class Format; }
namespace RenderCore { namespace Techniques 
{ 
    class ProjectionDesc; class ParsingContext; class IDrawableSubmitter;
    struct PreregisteredAttachment;
}}
namespace RenderOverlays { class FontRenderingManager; class OverlayApparatus; class ShapesRenderingDelegate; }
namespace OSServices { class InputSnapshot; }

namespace PlatformRig
{
    class InputContext;
    enum class ProcessInputResult;

///////////////////////////////////////////////////////////////////////////////////////////////////

    class IOverlay
    {
    public:
		virtual void Render(
			RenderCore::Techniques::ParsingContext& parserContext);

        virtual ProcessInputResult ProcessInput(
			const InputContext& context,
			const OSServices::InputSnapshot& evnt);

        virtual void OnRenderTargetUpdate(
            IteratorRange<const RenderCore::Techniques::PreregisteredAttachment*> preregAttachments,
            const RenderCore::FrameBufferProperties& fbProps,
            IteratorRange<const RenderCore::Format*> systemAttachmentFormats);

        virtual ~IOverlay();
    };

    class IUpdateTick
    {
    public:
        virtual void Update(float deltaTime) = 0;
        virtual ~IUpdateTick();
    };

///////////////////////////////////////////////////////////////////////////////////////////////////

    class IInputListener;
    std::shared_ptr<IInputListener> CreateInputListenerBridge(std::shared_ptr<IOverlay>);

    class IOverlayExtended
    {
    public:
        virtual void SetActivationState(bool newState);

		enum class RefreshMode { EventBased, RegularAnimation };
		struct OverlayState
		{
			RefreshMode _refreshMode = RefreshMode::EventBased;
		};
		virtual OverlayState GetOverlayState() const;
        virtual ~IOverlayExtended();
    };

    class OverlaySystemSwitch : public IOverlay, public IOverlayExtended, public IUpdateTick
    {
    public:
        virtual ProcessInputResult ProcessInput(
			const InputContext& context,
			const OSServices::InputSnapshot& evnt) override;

        void Render(RenderCore::Techniques::ParsingContext& parserContext) override;
        void Update(float deltaTime) override;
        void SetActivationState(bool newState) override;
		OverlayState GetOverlayState() const override;

        virtual void OnRenderTargetUpdate(
            IteratorRange<const RenderCore::Techniques::PreregisteredAttachment*> preregAttachments,
            const RenderCore::FrameBufferProperties& fbProps,
            IteratorRange<const RenderCore::Format*> systemAttachmentFormats) override;

        void AddSystem(uint32_t activator, std::shared_ptr<IOverlay> system);
        void SetDefaultSystem(std::shared_ptr<IOverlay> system);

        OverlaySystemSwitch();
        ~OverlaySystemSwitch();

    private:
        struct Pimpl;
        std::unique_ptr<Pimpl> _pimpl;
    };

///////////////////////////////////////////////////////////////////////////////////////////////////

    class OverlaySystemSet : public IOverlay, public IOverlayExtended, public IUpdateTick
    {
    public:
        virtual ProcessInputResult ProcessInput(
			const InputContext& context,
			const OSServices::InputSnapshot& evnt) override;

        void Render(RenderCore::Techniques::ParsingContext& parserContext) override;
        void Update(float deltaTime) override;
        void SetActivationState(bool newState) override;
		virtual OverlayState GetOverlayState() const override;

        virtual void OnRenderTargetUpdate(
            IteratorRange<const RenderCore::Techniques::PreregisteredAttachment*> preregAttachments,
            const RenderCore::FrameBufferProperties& fbProps,
            IteratorRange<const RenderCore::Format*> systemAttachmentFormats) override;

        void AddSystem(std::shared_ptr<IOverlay> system);
		void RemoveSystem(IOverlay& system);

        OverlaySystemSet();
        ~OverlaySystemSet();

    private:
        struct Pimpl;
        std::unique_ptr<Pimpl> _pimpl;
    };

    std::shared_ptr<IOverlay> CreateConsoleOverlaySystem(
        std::shared_ptr<RenderCore::Techniques::IDrawableSubmitter>,
        std::shared_ptr<RenderOverlays::ShapesRenderingDelegate>,
        std::shared_ptr<RenderOverlays::FontRenderingManager>);

}
