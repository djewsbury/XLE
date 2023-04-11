// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "InteractiveTestHelper.h"
#include "../EmbeddedRes.h"
#include "../../PlatformRig/FrameRig.h"
#include "../../PlatformRig/PlatformApparatuses.h"
#include "../../OSServices/OverlappedWindow.h"
#include "../../PlatformRig/MainInputHandler.h"
#include "../../PlatformRig/OverlaySystem.h"

#include "../../RenderOverlays/OverlayApparatus.h"

#include "../../RenderCore/LightingEngine/LightingEngineApparatus.h"
#include "../../RenderCore/Techniques/Apparatuses.h"
#include "../../RenderCore/Techniques/Techniques.h"
#include "../../RenderCore/Techniques/RenderPass.h"
#include "../../RenderCore/Techniques/ParsingContext.h"
#include "../../RenderCore/Techniques/Services.h"
#include "../../RenderCore/Techniques/SubFrameEvents.h"
#include "../../RenderCore/DeviceInitialization.h"
#include "../../RenderCore/IDevice.h"
#include "../../RenderCore/Vulkan/IDeviceVulkan.h"

#include "../../Assets/IFileSystem.h"
#include "../../Assets/MountingTree.h"
#include "../../Assets/OSFileSystem.h"
#include "../../Assets/AssetServices.h"
#include "../../Assets/AssetSetManager.h"
#include "../../Math/Transformations.h"

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
	class InteractiveTestHelper;
	static std::shared_ptr<PlatformRig::IOverlaySystem> CreateAdapter(std::shared_ptr<IInteractiveTestOverlay> overlaySystem, const RenderCore::Techniques::CameraDesc& camera, std::weak_ptr<InteractiveTestHelper> testHelper);

	class InteractiveTestHelper : public IInteractiveTestHelper, public std::enable_shared_from_this<InteractiveTestHelper>
	{
	public:
		virtual std::shared_ptr<RenderCore::Techniques::DrawingApparatus> GetDrawingApparatus() const override { return _drawingApparatus; }
		virtual std::shared_ptr<RenderOverlays::OverlayApparatus> GetOverlayApparatus() const override { return _immediateDrawingApparatus; }
		virtual std::shared_ptr<RenderCore::Techniques::PrimaryResourcesApparatus> GetPrimaryResourcesApparatus() const override { return _primaryResourcesApparatus; }
		virtual std::shared_ptr<RenderCore::LightingEngine::LightingEngineApparatus> GetLightingEngineApparatus() const override { return _lightingEngineApparatus; }
		virtual std::shared_ptr<PlatformRig::WindowApparatus> GetWindowApparatus() const override { return _windowApparatus; }
		virtual std::shared_ptr<RenderCore::IDevice> GetDevice() const override { return _device; }

		void Run(
			const RenderCore::Techniques::CameraDesc& camera,
			std::shared_ptr<IInteractiveTestOverlay> overlaySystem) override
		{
			REQUIRE(!_activeCamera);
			_activeCamera = &camera;

			auto adapter = CreateAdapter(overlaySystem, camera, weak_from_this());
			_frameRig->UpdatePresentationChain(*_windowApparatus->_presentationChain);
			auto overlayConfig = _windowApparatus->_frameRig->GetOverlayConfiguration(*_windowApparatus->_presentationChain);
			overlaySystem->OnRenderTargetUpdate(overlayConfig._preregAttachments, overlayConfig._fbProps, overlayConfig._systemAttachmentFormats);
			if (_drawingApparatus)
				_drawingApparatus->_techniqueServices->GetSubFrameEvents()._onCheckCompleteInitialization.Invoke(*_windowApparatus->_immediateContext);
			auto inputListener = PlatformRig::CreateInputListener(adapter);
			_windowApparatus->_mainInputHandler->AddListener(inputListener);
			_windowApparatus->_osWindow->Show();

			auto cleanup = MakeAutoCleanup(
				[&]{
					_windowApparatus->_osWindow->Show(false);
					_windowApparatus->_mainInputHandler->RemoveListener(*inputListener);
					_activeCamera = nullptr;
				});

			for (;;) {
				auto msg = OSServices::Window::SingleWindowMessagePump(*_windowApparatus->_osWindow);
				PlatformRig::CommonEventHandling(*_windowApparatus, msg);

				if (std::holds_alternative<OSServices::ShutdownRequest>(msg)) {
					break;
				} else if (std::holds_alternative<OSServices::Idle>(msg)) {

					auto& idle = std::get<OSServices::Idle>(msg);
					if (idle._state == OSServices::IdleState::Background) {
						// Bail if we're minimized (don't have to check this in the foreground case)
						auto presChainDesc = _windowApparatus->_presentationChain->GetDesc();
						if (!(presChainDesc._width * presChainDesc._height)) {
							Threading::Sleep(64);       // minimized and inactive
							continue;
						}
					}

					overlaySystem->OnUpdate(_windowApparatus->_frameRig->GetSmoothedDeltaTime());
					
					auto parsingContext = _windowApparatus->_frameRig->StartupFrame(*_windowApparatus);
					parsingContext.GetProjectionDesc() = RenderCore::Techniques::BuildProjectionDesc(*_activeCamera, {unsigned(parsingContext.GetViewport()._width), unsigned(parsingContext.GetViewport()._height)});

					TRY {
						overlaySystem->Render(parsingContext, *this);
					} CATCH(const std::exception& e) {
						PlatformRig::ReportErrorToColorLDR(parsingContext, *_immediateDrawingApparatus, e.what());
					} CATCH_END

					auto frameResult = _windowApparatus->_frameRig->ShutdownFrame(parsingContext);
					_windowApparatus->_frameRig->IntermedialSleep(*_windowApparatus, idle._state == OSServices::IdleState::Background, frameResult);

				} else if (std::holds_alternative<OSServices::WindowResize>(msg)) {

					auto newOverlayConfig = _windowApparatus->_frameRig->GetOverlayConfiguration(*_windowApparatus->_presentationChain);
					if (newOverlayConfig._hash != overlayConfig._hash) {
						overlaySystem->OnRenderTargetUpdate(newOverlayConfig._preregAttachments, newOverlayConfig._fbProps, newOverlayConfig._systemAttachmentFormats);
						overlayConfig = newOverlayConfig;
					}

				}
			}
		}

		std::pair<Float3, Float3> ScreenToWorldSpaceRay(Int2 screenPt) const override
		{
			REQUIRE(_activeCamera);

			auto presChainDesc = _windowApparatus->_presentationChain->GetDesc();
			UInt2 viewportDims { presChainDesc._width, presChainDesc._height };
			REQUIRE(viewportDims[0] > 0); REQUIRE(viewportDims[1] > 0);	// expecting a non-empty viewport here, otherwise we'll get a divide by zero below

			return RenderCore::Techniques::BuildRayUnderCursor(
				screenPt, *_activeCamera, 
				std::make_pair(Float2{0,0}, viewportDims));
		}

		virtual RenderCore::Techniques::TechniqueContext CreateTechniqueContext() override
		{
			return _frameRig->GetTechniqueContext();
		}

		virtual void ResizeWindow(unsigned width, unsigned height) override
		{
			return _windowApparatus->_osWindow->Resize(width, height);
		}

		InteractiveTestHelper(EnabledComponents::BitField enabledComponents)
		{
			if (!_globalServices) _globalServices = std::make_shared<ConsoleRig::GlobalServices>();
			#if !defined(NO_EMBEDDED_RES)
				_xleresmnt = ::Assets::MainFileSystem::GetMountingTree()->Mount("xleres", UnitTests::CreateEmbeddedResFileSystem());
			#else
				_xleresmnt = ::Assets::MainFileSystem::GetMountingTree()->Mount("xleres", ::Assets::CreateFileSystem_OS("Game/xleres", ConsoleRig::GlobalServices::GetInstance().GetPollingThread()));
			#endif
			if (enabledComponents)
				_rawosmnt = ::Assets::MainFileSystem::GetMountingTree()->Mount("rawos", ::Assets::CreateFileSystem_OS({}, ConsoleRig::GlobalServices::GetInstance().GetPollingThread()));

			auto osWindow = std::make_unique<OSServices::Window>();
			auto renderAPI = RenderCore::CreateAPIInstance(RenderCore::Techniques::GetTargetAPI());

			_device = renderAPI->CreateDevice(0, renderAPI->QueryFeatureCapability(0));
			if (!_assetServices) _assetServices = std::make_shared<::Assets::Services>();

			_primaryResourcesApparatus = std::make_shared<RenderCore::Techniques::PrimaryResourcesApparatus>(_device);
			_frameRenderingApparatus = std::make_shared<RenderCore::Techniques::FrameRenderingApparatus>(_device);

			if (enabledComponents & EnabledComponents::RenderCoreTechniques) {
				_drawingApparatus = std::make_shared<RenderCore::Techniques::DrawingApparatus>(_device);
				_immediateDrawingApparatus = std::make_shared<RenderOverlays::OverlayApparatus>(_drawingApparatus);
			}

			if (enabledComponents & EnabledComponents::LightingEngine) {
				REQUIRE(enabledComponents & EnabledComponents::RenderCoreTechniques);
				_lightingEngineApparatus = std::make_shared<RenderCore::LightingEngine::LightingEngineApparatus>(_drawingApparatus);
			}

			auto presentationChainBindFlags = RenderCore::BindFlag::UnorderedAccess;
			_windowApparatus = std::make_shared<PlatformRig::WindowApparatus>(std::move(osWindow), _drawingApparatus.get(), *_frameRenderingApparatus, presentationChainBindFlags);
			auto v = _device->GetDesc();
			_windowApparatus->_osWindow->SetTitle(StringMeld<128>() << "XLE interactive unit test [RenderCore: " << v._buildVersion << ", " << v._buildDate << "]");

			_frameRig = _windowApparatus->_frameRig;
		}

		~InteractiveTestHelper()
		{
			if (_rawosmnt != ~0u)
				::Assets::MainFileSystem::GetMountingTree()->Unmount(_rawosmnt);
			if (_xleresmnt != ~0u)
				::Assets::MainFileSystem::GetMountingTree()->Unmount(_xleresmnt);
			_globalServices->PrepareForDestruction();
		}
	private:
		ConsoleRig::AttachablePtr<ConsoleRig::GlobalServices> _globalServices;
		ConsoleRig::AttachablePtr<::Assets::Services> _assetServices;
		std::shared_ptr<RenderCore::IDevice> _device;

		std::shared_ptr<PlatformRig::WindowApparatus> _windowApparatus;
		std::shared_ptr<RenderCore::Techniques::DrawingApparatus> _drawingApparatus;
		std::shared_ptr<RenderOverlays::OverlayApparatus> _immediateDrawingApparatus;
		std::shared_ptr<RenderCore::Techniques::PrimaryResourcesApparatus> _primaryResourcesApparatus;
		std::shared_ptr<RenderCore::Techniques::FrameRenderingApparatus> _frameRenderingApparatus;

		std::shared_ptr<RenderCore::LightingEngine::LightingEngineApparatus> _lightingEngineApparatus;

		std::shared_ptr<PlatformRig::FrameRig> _frameRig;
		uint32_t _xleresmnt = ~0u;
		uint32_t _rawosmnt = ~0u;
		const RenderCore::Techniques::CameraDesc* _activeCamera = nullptr;
	};

	class OverlayAdapter : public PlatformRig::IOverlaySystem
	{
	public:
		virtual void Render(
			RenderCore::Techniques::ParsingContext& parserContext) override
		{
			UInt2 viewportDims { parserContext.GetViewport()._width, parserContext.GetViewport()._height };
			auto oldProjDesc = parserContext.GetProjectionDesc();
			parserContext.GetProjectionDesc() = RenderCore::Techniques::BuildProjectionDesc(_camera, viewportDims);
			auto testHelper = _testHelper.lock();
			REQUIRE(testHelper);
			_overlaySystem->Render(parserContext, *testHelper);
			parserContext.GetProjectionDesc() = oldProjDesc;
		}

		virtual PlatformRig::ProcessInputResult ProcessInput(
			const PlatformRig::InputContext& context,
			const OSServices::InputSnapshot& evnt) override
		{
			return _childInputListener->OnInputEvent(context, evnt);
		}

		virtual void OnRenderTargetUpdate(
            IteratorRange<const RenderCore::Techniques::PreregisteredAttachment*> preregAttachments,
            const RenderCore::FrameBufferProperties& fbProps,
            IteratorRange<const RenderCore::Format*> systemAttachmentFormats) override
		{
			_overlaySystem->OnRenderTargetUpdate(preregAttachments, fbProps, systemAttachmentFormats);
		}

		class ChildInputListener : public PlatformRig::IInputListener
		{
		public:
			virtual PlatformRig::ProcessInputResult OnInputEvent(
				const PlatformRig::InputContext& context,
				const OSServices::InputSnapshot& evnt) override
			{
				auto testHelper = _parent->_testHelper.lock();
				REQUIRE(testHelper);
				return _parent->_overlaySystem->OnInputEvent(context, evnt, *testHelper) ? ProcessInputResult::Consumed : ProcessInputResult::Passthrough;
			}
			ChildInputListener(OverlayAdapter* parent) : _parent(parent) {}
			OverlayAdapter* _parent;
		};

		OverlayAdapter(
			std::shared_ptr<IInteractiveTestOverlay> overlaySystem,
			const RenderCore::Techniques::CameraDesc& camera,
			std::weak_ptr<InteractiveTestHelper> testHelper)
		: _overlaySystem(std::move(overlaySystem)), _testHelper(std::move(testHelper)), _camera(camera)
		{
			_childInputListener = std::make_shared<ChildInputListener>(this);
		}

		std::shared_ptr<IInteractiveTestOverlay> _overlaySystem;
		std::weak_ptr<InteractiveTestHelper> _testHelper;
		std::shared_ptr<PlatformRig::IInputListener> _childInputListener;
		RenderCore::Techniques::CameraDesc _camera;
	};

	std::shared_ptr<PlatformRig::IOverlaySystem> CreateAdapter(std::shared_ptr<IInteractiveTestOverlay> overlaySystem, const RenderCore::Techniques::CameraDesc& camera, std::weak_ptr<InteractiveTestHelper> testHelper)
	{
		return std::make_shared<OverlayAdapter>(overlaySystem, camera, testHelper);
	}

	std::shared_ptr<IInteractiveTestHelper> CreateInteractiveTestHelper(IInteractiveTestHelper::EnabledComponents::BitField enabledComponents)
	{
		return std::make_shared<InteractiveTestHelper>(enabledComponents);
	}

	void IInteractiveTestOverlay::Render(
		RenderCore::Techniques::ParsingContext& parserContext,
		IInteractiveTestHelper& testHelper)
	{}
	void IInteractiveTestOverlay::OnUpdate(float deltaTime) {}
	bool IInteractiveTestOverlay::OnInputEvent(
		const PlatformRig::InputContext& context,
		const OSServices::InputSnapshot& evnt,
		IInteractiveTestHelper& testHelper)
	{
		return false;
	}
	void IInteractiveTestOverlay::OnRenderTargetUpdate(
		IteratorRange<const RenderCore::Techniques::PreregisteredAttachment*> preregAttachments,
		const RenderCore::FrameBufferProperties& fbProps,
		IteratorRange<const RenderCore::Format*> systemAttachmentFormats)
	{}

	IInteractiveTestOverlay::~IInteractiveTestOverlay() {}
	IInteractiveTestHelper::~IInteractiveTestHelper() {}

}