// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "InteractiveTestHelper.h"
#include "../EmbeddedRes.h"
#include "../../PlatformRig/FrameRig.h"
#include "../../PlatformRig/PlatformApparatuses.h"
#include "../../PlatformRig/OverlappedWindow.h"
#include "../../PlatformRig/MainInputHandler.h"
#include "../../PlatformRig/PlatformRigUtil.h"
#include "../../PlatformRig/OverlaySystem.h"

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
		virtual std::shared_ptr<RenderCore::Techniques::ImmediateDrawingApparatus> GetImmediateDrawingApparatus() const override { return _immediateDrawingApparatus; }
		virtual std::shared_ptr<RenderCore::LightingEngine::LightingEngineApparatus> GetLightingEngineApparatus() const override { return _lightingEngineApparatus; }

		void Run(
			const RenderCore::Techniques::CameraDesc& camera,
			std::shared_ptr<IInteractiveTestOverlay> overlaySystem) override
		{
			REQUIRE(!_activeCamera);
			_activeCamera = &camera;

			auto adapter = CreateAdapter(overlaySystem, camera, weak_from_this());
			_frameRig->SetMainOverlaySystem(adapter);
			_frameRig->UpdatePresentationChain(*_windowApparatus->_presentationChain);
			_windowApparatus->_mainInputHandler->AddListener(adapter->GetInputListener());
			while (PlatformRig::OverlappedWindow::DoMsgPump() != PlatformRig::OverlappedWindow::PumpResult::Terminate) {
				_frameRig->ExecuteFrame(*_windowApparatus, *_frameRenderingApparatus, _drawingApparatus.get());
				_frameRenderingApparatus->_frameCPUProfiler->EndFrame();
			}
			_frameRig->SetMainOverlaySystem(nullptr);
			_activeCamera = nullptr;
		}

		std::pair<Float3, Float3> ScreenToWorldSpaceRay(Int2 screenPt) const override
		{
			REQUIRE(_activeCamera);

			auto presChainDesc = _windowApparatus->_presentationChain->GetDesc();
			UInt2 viewportDims { presChainDesc->_width, presChainDesc->_height };
			REQUIRE(viewportDims[0] > 0); REQUIRE(viewportDims[1] > 0);	// expecting a non-empty viewport here, otherwise we'll get a divide by zero below

			return RenderCore::Techniques::BuildRayUnderCursor(
				screenPt, *_activeCamera, 
				std::make_pair(Float2{0,0}, viewportDims));
		}

		InteractiveTestHelper(EnabledComponents::BitField enabledComponents)
		{
			if (!_globalServices) _globalServices = std::make_shared<ConsoleRig::GlobalServices>();
			_xleresmnt = ::Assets::MainFileSystem::GetMountingTree()->Mount("xleres", UnitTests::CreateEmbeddedResFileSystem());

			_device = RenderCore::CreateDevice(RenderCore::Techniques::GetTargetAPI());
			if (!_assetServices) _assetServices = std::make_shared<::Assets::Services>();

			_windowApparatus = std::make_shared<PlatformRig::WindowApparatus>(_device);
			_primaryResourcesApparatus = std::make_shared<RenderCore::Techniques::PrimaryResourcesApparatus>(_device);
			_frameRenderingApparatus = std::make_shared<RenderCore::Techniques::FrameRenderingApparatus>(_device);
			auto v = _device->GetDesc();
			_windowApparatus->_osWindow->SetTitle(StringMeld<128>() << "XLE interactive unit test [RenderCore: " << v._buildVersion << ", " << v._buildDate << "]");

			_frameRig = std::make_shared<PlatformRig::FrameRig>(_primaryResourcesApparatus->_subFrameEvents);
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
			if (_xleresmnt != ~0u)
				::Assets::MainFileSystem::GetMountingTree()->Unmount(_xleresmnt);
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
		uint32_t _xleresmnt = ~0u;
		const RenderCore::Techniques::CameraDesc* _activeCamera = nullptr;
	};

	class OverlayAdapter : public PlatformRig::IOverlaySystem
	{
	public:
		virtual void Render(
            RenderCore::IThreadContext& threadContext,
			RenderCore::Techniques::ParsingContext& parserContext) override
		{
			UInt2 viewportDims { parserContext.GetViewport()._width, parserContext.GetViewport()._height };
			auto oldProjDesc = parserContext.GetProjectionDesc();
			parserContext.GetProjectionDesc() = RenderCore::Techniques::BuildProjectionDesc(_camera, viewportDims);
			auto testHelper = _testHelper.lock();
			REQUIRE(testHelper);
			_overlaySystem->Render(threadContext, parserContext, *testHelper);
			parserContext.GetProjectionDesc() = oldProjDesc;
		}

        virtual std::shared_ptr<PlatformRig::IInputListener> GetInputListener() override
		{
			return _childInputListener;
		}

		class ChildInputListener : public PlatformRig::IInputListener
		{
		public:
			virtual bool OnInputEvent(
				const PlatformRig::InputContext& context,
				const PlatformRig::InputSnapshot& evnt) override
			{
				auto testHelper = _parent->_testHelper.lock();
				REQUIRE(testHelper);
				return _parent->_overlaySystem->OnInputEvent(context, evnt, *testHelper);
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
		RenderCore::IThreadContext& threadContext,
		RenderCore::Techniques::ParsingContext& parserContext,
		IInteractiveTestHelper& testHelper)
	{}
	void IInteractiveTestOverlay::OnUpdate(float deltaTime) {}
	bool IInteractiveTestOverlay::OnInputEvent(
		const PlatformRig::InputContext& context,
		const PlatformRig::InputSnapshot& evnt,
		IInteractiveTestHelper& testHelper)
	{
		return false;
	}

	IInteractiveTestOverlay::~IInteractiveTestOverlay() {}
	IInteractiveTestHelper::~IInteractiveTestHelper() {}

}