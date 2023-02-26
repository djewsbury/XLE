// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "../../Math/Vector.h"
#include "../../Utility/IteratorUtils.h"
#include <memory>
#include <utility>

namespace PlatformRig { class IOverlaySystem; class InputContext; class InputSnapshot; class WindowApparatus; }
namespace RenderCore { namespace Techniques { class DrawingApparatus; class PrimaryResourcesApparatus; class ParsingContext; class CameraDesc; class TechniqueContext; struct PreregisteredAttachment; }}
namespace RenderCore { namespace LightingEngine { class LightingEngineApparatus; }}
namespace RenderCore { class IDevice; class IThreadContext; class FrameBufferProperties; enum class Format; }
namespace RenderOverlays { class OverlayApparatus; }

namespace UnitTests
{
	class IInteractiveTestHelper;

	class IInteractiveTestOverlay
	{
	public:
		virtual void Render(
			RenderCore::Techniques::ParsingContext& parserContext,
			IInteractiveTestHelper& testHelper);

		virtual void OnUpdate(float deltaTime);
		virtual bool OnInputEvent(
			const PlatformRig::InputContext& context,
			const PlatformRig::InputSnapshot& evnt,
			IInteractiveTestHelper& testHelper);

		virtual void OnRenderTargetUpdate(
			IteratorRange<const RenderCore::Techniques::PreregisteredAttachment*> preregAttachments,
			const RenderCore::FrameBufferProperties& fbProps,
			IteratorRange<const RenderCore::Format*> systemAttachmentFormats);

		virtual ~IInteractiveTestOverlay();
	};

	class IInteractiveTestHelper
	{
	public:
		virtual std::shared_ptr<RenderCore::Techniques::DrawingApparatus> GetDrawingApparatus() const = 0;
		virtual std::shared_ptr<RenderOverlays::OverlayApparatus> GetOverlayApparatus() const = 0;
		virtual std::shared_ptr<RenderCore::Techniques::PrimaryResourcesApparatus> GetPrimaryResourcesApparatus() const = 0;
		virtual std::shared_ptr<RenderCore::LightingEngine::LightingEngineApparatus> GetLightingEngineApparatus() const = 0;
		virtual std::shared_ptr<PlatformRig::WindowApparatus> GetWindowApparatus() const = 0;
		virtual std::shared_ptr<RenderCore::IDevice> GetDevice() const = 0;
		virtual RenderCore::Techniques::TechniqueContext CreateTechniqueContext() = 0;

		virtual void Run(
			const RenderCore::Techniques::CameraDesc& camera,
			std::shared_ptr<IInteractiveTestOverlay> overlaySystem) = 0;

		virtual std::pair<Float3, Float3> ScreenToWorldSpaceRay(Int2 screenPt) const = 0;
		virtual void ResizeWindow(unsigned width, unsigned height) = 0;

		struct EnabledComponents
		{
			enum Enum {
				RenderCoreTechniques = 1<<0,
				LightingEngine = 1<<1
			};
			using BitField = unsigned;
		};

		virtual ~IInteractiveTestHelper();
	};

	std::shared_ptr<IInteractiveTestHelper> CreateInteractiveTestHelper(IInteractiveTestHelper::EnabledComponents::BitField enabledComponents);
}
