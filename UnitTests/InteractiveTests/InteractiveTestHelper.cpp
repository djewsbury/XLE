// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include <memory>

namespace PlatformRig { class IOverlaySystem; }
namespace RenderCore { namespace Techniques { class DrawingApparatus; class ImmediateDrawingApparatus; }}
namespace RenderCore { namespace LightingEngine { class LightingEngineApparatus; }}

#include "../../PlatformRig/FrameRig.h"
#include "../../PlatformRig/PlatformApparatuses.h"
#include "../../PlatformRig/OverlappedWindow.h"
#include "../../PlatformRig/MainInputHandler.h"
#include "../../PlatformRig/PlatformRigUtil.h"

#include "../../RenderCore/LightingEngine/LightingEngineApparatus.h"
#include "../../RenderCore/Techniques/Apparatuses.h"
#include "../../RenderCore/Techniques/Techniques.h"
#include "../../RenderCore/Techniques/RenderPass.h"
#include "../../RenderCore/Techniques/ParsingContext.h"
#include "../../RenderCore/Techniques/Services.h"
#include "../../RenderCore/Init.h"
#include "../../RenderCore/IDevice.h"
#include "../../RenderCore/IThreadContext.h"

#include "../../Assets/IFileSystem.h"
#include "../../Assets/MountingTree.h"
#include "../../Assets/OSFileSystem.h"
#include "../../Assets/AssetServices.h"
#include "../../Assets/AssetSetManager.h"

#include "../../ConsoleRig/GlobalServices.h"
#include "../../ConsoleRig/AttachablePtr.h"
#include "../../Utility/StringFormat.h"
#include "../../Utility/Profiling/CPUProfiler.h"

#include "catch2/catch_test_macros.hpp"
#include "catch2/catch_approx.hpp"

using namespace Catch::literals;
using namespace std::chrono_literals;
namespace UnitTests
{
	class IInteractiveTestHelper
	{
	public:
		virtual std::shared_ptr<RenderCore::Techniques::DrawingApparatus> GetDrawingApparatus() const;
		virtual std::shared_ptr<RenderCore::Techniques::ImmediateDrawingApparatus> GetImmediateDrawingApparatus() const;
		virtual std::shared_ptr<RenderCore::LightingEngine::LightingEngineApparatus> GetLightingEngineApparatus() const;

		virtual void Run(std::shared_ptr<PlatformRig::IOverlaySystem> overlaySystem);

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

	class InteractiveTestHelper : public IInteractiveTestHelper
	{
	public:
		virtual std::shared_ptr<RenderCore::Techniques::DrawingApparatus> GetDrawingApparatus() const override { return _drawingApparatus; }
		virtual std::shared_ptr<RenderCore::Techniques::ImmediateDrawingApparatus> GetImmediateDrawingApparatus() const override { return _immediateDrawingApparatus; }
		virtual std::shared_ptr<RenderCore::LightingEngine::LightingEngineApparatus> GetLightingEngineApparatus() const override { return _lightingEngineApparatus; }

		void Run(std::shared_ptr<PlatformRig::IOverlaySystem> overlaySystem) override
		{
			while (PlatformRig::OverlappedWindow::DoMsgPump() != PlatformRig::OverlappedWindow::PumpResult::Terminate) {
				_frameRig->ExecuteFrame(*_windowApparatus, *_frameRenderingApparatus, _drawingApparatus.get());
				_frameRenderingApparatus->_frameCPUProfiler->EndFrame();
			}
		}

		InteractiveTestHelper(EnabledComponents::BitField enabledComponents)
		{
			if (!_globalServices) _globalServices = std::make_shared<ConsoleRig::GlobalServices>();
			_device = RenderCore::CreateDevice(RenderCore::Techniques::GetTargetAPI());
			if (!_assetServices) _assetServices = std::make_shared<::Assets::Services>();

			_windowApparatus = std::make_shared<PlatformRig::WindowApparatus>(_device);
			_primaryResourcesApparatus = std::make_shared<RenderCore::Techniques::PrimaryResourcesApparatus>(_device);
			_frameRenderingApparatus = std::make_shared<RenderCore::Techniques::FrameRenderingApparatus>(_device);
			auto v = _device->GetDesc();
			_windowApparatus->_osWindow->SetTitle(StringMeld<128>() << "XLE interactive unit test [RenderCore: " << v._buildVersion << ", " << v._buildDate << "]");

			_frameRig = std::make_shared<PlatformRig::FrameRig>(_primaryResourcesApparatus->_subFrameEvents);

			_frameRig->UpdatePresentationChain(*_windowApparatus->_presentationChain);
			_windowApparatus->_windowHandler->_onResize.Bind(
				[fra = std::weak_ptr<RenderCore::Techniques::FrameRenderingApparatus>{_frameRenderingApparatus},
				ps = std::weak_ptr<RenderCore::IPresentationChain>(_windowApparatus->_presentationChain), 
				fr = std::weak_ptr<PlatformRig::FrameRig>{_frameRig}](unsigned, unsigned) {
					auto apparatus = fra.lock();
					if (apparatus)
						RenderCore::Techniques::ResetFrameBufferPool(*apparatus->_frameBufferPool);
					auto presChain = ps.lock();
					auto frameRig = fr.lock();
					if (presChain && frameRig)
						frameRig->UpdatePresentationChain(*presChain);
				});

			if (enabledComponents & EnabledComponents::RenderCoreTechniques) {
				_drawingApparatus = std::make_shared<RenderCore::Techniques::DrawingApparatus>(_device);
				_immediateDrawingApparatus = std::make_shared<RenderCore::Techniques::ImmediateDrawingApparatus>(_drawingApparatus);
			}

			if (enabledComponents & EnabledComponents::LightingEngine) {
				REQUIRE(enabledComponents & EnabledComponents::RenderCoreTechniques);
				_lightingEngineApparatus = std::make_shared<RenderCore::LightingEngine::LightingEngineApparatus>(_drawingApparatus);
			}
		}

		~InteractiveTestHelper()
		{
		}
	private:
		ConsoleRig::AttachablePtr<ConsoleRig::GlobalServices> _globalServices;
		ConsoleRig::AttachablePtr<::Assets::Services> _assetServices;
		std::shared_ptr<RenderCore::IDevice> _device;

		std::shared_ptr<PlatformRig::WindowApparatus> _windowApparatus;
		std::shared_ptr<RenderCore::Techniques::DrawingApparatus> _drawingApparatus;
		std::shared_ptr<RenderCore::Techniques::ImmediateDrawingApparatus> _immediateDrawingApparatus;
		std::shared_ptr<RenderCore::Techniques::PrimaryResourcesApparatus> _primaryResourcesApparatus;
		std::shared_ptr<RenderCore::Techniques::FrameRenderingApparatus> _frameRenderingApparatus;

		std::shared_ptr<RenderCore::LightingEngine::LightingEngineApparatus> _lightingEngineApparatus;

		std::shared_ptr<PlatformRig::FrameRig> _frameRig;
	};

}