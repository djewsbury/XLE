// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../../RenderCore/IDevice_Forward.h"
#include "../../RenderCore/FrameBufferDesc.h"
#include "../../Math/Matrix.h"
#include "../../Utility/IteratorUtils.h"
#include <memory>
#include <vector>

namespace RenderCore { class IThreadContext; class FrameBufferProperties; }
namespace RenderCore { namespace Techniques 
{ 
    class ProjectionDesc; class ParsingContext; class IImmediateDrawables; class ImmediateDrawingApparatus;
    struct PreregisteredAttachment;
}}
namespace RenderOverlays { class FontRenderingManager; }

namespace PlatformRig
{
	class IInputListener;

///////////////////////////////////////////////////////////////////////////////////////////////////
    class IOverlaySystem
    {
    public:
		virtual void Render(
			RenderCore::Techniques::ParsingContext& parserContext) = 0; 

        virtual std::shared_ptr<IInputListener> GetInputListener();
        virtual void SetActivationState(bool newState);

		enum class RefreshMode { EventBased, RegularAnimation };
		struct OverlayState
		{
			RefreshMode _refreshMode = RefreshMode::EventBased;
		};
		virtual OverlayState GetOverlayState() const;

        virtual void OnRenderTargetUpdate(
            IteratorRange<const RenderCore::Techniques::PreregisteredAttachment*> preregAttachments,
            const RenderCore::FrameBufferProperties& fbProps);

        virtual ~IOverlaySystem();
    };

///////////////////////////////////////////////////////////////////////////////////////////////////
    class OverlaySystemSwitch : public IOverlaySystem
    {
    public:
        std::shared_ptr<IInputListener> GetInputListener() override;

        void Render(
            RenderCore::Techniques::ParsingContext& parserContext) override;
        void SetActivationState(bool newState) override;
		OverlayState GetOverlayState() const override;

        virtual void OnRenderTargetUpdate(
            IteratorRange<const RenderCore::Techniques::PreregisteredAttachment*> preregAttachments,
            const RenderCore::FrameBufferProperties& fbProps) override;

        void AddSystem(uint32_t activator, std::shared_ptr<IOverlaySystem> system);

        OverlaySystemSwitch();
        ~OverlaySystemSwitch();

    private:
        class InputListener;

        signed _activeChildIndex;
        std::vector<std::pair<uint32_t,std::shared_ptr<IOverlaySystem>>> _childSystems;
        std::shared_ptr<InputListener> _inputListener;

        std::vector<RenderCore::Techniques::PreregisteredAttachment> _preregisteredAttachments;
        RenderCore::FrameBufferProperties _fbProps;
    };

///////////////////////////////////////////////////////////////////////////////////////////////////
    class OverlaySystemSet : public IOverlaySystem
    {
    public:
        std::shared_ptr<IInputListener> GetInputListener() override;

        void Render(
            RenderCore::Techniques::ParsingContext& parserContext) override;
        void SetActivationState(bool newState) override;
		virtual OverlayState GetOverlayState() const override;

        virtual void OnRenderTargetUpdate(
            IteratorRange<const RenderCore::Techniques::PreregisteredAttachment*> preregAttachments,
            const RenderCore::FrameBufferProperties& fbProps) override;

        void AddSystem(std::shared_ptr<IOverlaySystem> system);
		void RemoveSystem(IOverlaySystem& system);

        OverlaySystemSet();
        ~OverlaySystemSet();

    private:
        class InputListener;

        signed _activeChildIndex;
        std::vector<std::shared_ptr<IOverlaySystem>> _childSystems;
        std::shared_ptr<InputListener> _inputListener;

        std::vector<RenderCore::Techniques::PreregisteredAttachment> _preregisteredAttachments;
        RenderCore::FrameBufferProperties _fbProps;
    };

    std::shared_ptr<IOverlaySystem> CreateConsoleOverlaySystem(
        RenderCore::Techniques::ImmediateDrawingApparatus&);

    std::shared_ptr<IOverlaySystem> CreateConsoleOverlaySystem(
        std::shared_ptr<RenderCore::Techniques::IImmediateDrawables>,
        std::shared_ptr<RenderOverlays::FontRenderingManager>);
}
