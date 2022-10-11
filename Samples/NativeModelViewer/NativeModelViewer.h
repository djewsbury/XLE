// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../Shared/SampleRig.h"
#include <memory>

namespace SceneEngine { class IScene; class ILightingStateDelegate; class BasicLightingStateDelegate; }
namespace RenderCore { namespace Techniques { class CameraDesc; class IPipelineAcceleratorPool; }}
namespace ToolsRig { class VisOverlayController; }

namespace Sample
{
	class SampleLightingDelegate;

	class NativeModelViewerOverlay : virtual public PlatformRig::OverlaySystemSet, virtual public ISampleOverlay
	{
	public:
		virtual void OnUpdate(float deltaTime) override;
		virtual void OnStartup(const SampleGlobals& globals) override;

		void Render(
            RenderCore::Techniques::ParsingContext& parserContext) override;
		std::shared_ptr<PlatformRig::IInputListener> GetInputListener() override;
        void SetActivationState(bool newState) override;

		virtual void OnRenderTargetUpdate(
            IteratorRange<const RenderCore::Techniques::PreregisteredAttachment*> preregAttachments,
            const RenderCore::FrameBufferProperties& fbProps,
			IteratorRange<const RenderCore::Format*> systemAttachmentFormats) override;

		NativeModelViewerOverlay();
		~NativeModelViewerOverlay();
	private:
		std::shared_ptr<ToolsRig::VisOverlayController> _overlayBinder;
	};
}
