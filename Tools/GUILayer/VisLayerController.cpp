// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "VisLayerController.h"
#include "LayerControl.h"
#include "IWindowRig.h"
#include "GUILayerUtil.h"
#include "NativeEngineDevice.h"
#include "../ToolsRig/VisualisationUtils.h"
#include "../ToolsRig/ModelVisualisation.h"
#include "../ToolsRig/MaterialVisualisation.h"
#include "../ToolsRig/IManipulator.h"
#include "../ToolsRig/BasicManipulators.h"
#include "../ToolsRig/PreviewSceneRegistry.h"
#include "../ToolsRig/ToolsRigServices.h"
#include "../../PlatformRig/WinAPI/InputTranslator.h"
#include "../../PlatformRig/FrameRig.h"
#include "../../PlatformRig/OverlaySystem.h"
#include "../../RenderOverlays/DebuggingDisplay.h"
#include "../../RenderCore/Techniques/Techniques.h"
#include "../../RenderCore/Techniques/Apparatuses.h"
#include "../../RenderCore/Assets/MaterialScaffold.h"
#include "../../SceneEngine/IScene.h"
#include "../../Utility/StringFormat.h"
#include <iomanip>

using namespace System;

namespace GUILayer 
{
	class VisLayerControllerPimpl 
    {
    public:
        std::shared_ptr<ToolsRig::VisualisationOverlay> _visOverlay;
		std::shared_ptr<ToolsRig::ISimpleSceneOverlay> _modelLayer;
		std::shared_ptr<PlatformRig::IOverlaySystem> _manipulatorLayer;
		std::shared_ptr<ToolsRig::VisCameraSettings> _camera;
		std::shared_ptr<ToolsRig::VisAnimationState> _animState;
		std::shared_ptr<ToolsRig::VisOverlayController> _overlayBinder;

		std::shared_ptr<ToolsRig::DeferredCompiledShaderPatchCollection> _patchCollection;

		void ApplyPatchCollection()
		{
			auto* scene = _overlayBinder->TryGetScene();
			if (!scene) return;		// note that this will fail if the scene hasn't completed loading yet

			auto* patchCollectionScene = dynamic_cast<ToolsRig::IPatchCollectionVisualizationScene*>(scene);
			if (patchCollectionScene)
				patchCollectionScene->SetPatchCollection(_patchCollection->GetFuture());
		}
    };

	VisMouseOver^ VisLayerController::MouseOver::get()
	{
		return gcnew VisMouseOver(_pimpl->_visOverlay->GetMouseOver(), nullptr);	// todo -- scene
	}

	VisAnimationState^ VisLayerController::AnimationState::get()
	{
		return gcnew VisAnimationState(_pimpl->_animState);
	}

	void VisLayerController::SetScene(ModelVisSettings^ settings)
	{
		_pimpl->_overlayBinder->SetScene(*settings->ConvertToNative());
		// _pimpl->ApplyPatchCollection();
	}

	void VisLayerController::SetScene(MaterialVisSettings^ settings)
	{
		_pimpl->_overlayBinder->SetScene(*settings->ConvertToNative());
		// _pimpl->ApplyPatchCollection();
	}

	void VisLayerController::SetPreviewRegistryScene(System::String^ name)
	{
		auto pipelineAcceleratorPool = EngineDevice::GetInstance()->GetNative().GetMainPipelineAcceleratorPool();
		auto drawablesPool = EngineDevice::GetInstance()->GetNative().GetDrawingApparatus()->_drawablesPool;
		auto deformAcceleratorPool = EngineDevice::GetInstance()->GetNative().GetDrawingApparatus()->_deformAccelerators;
		auto nativeName = clix::marshalString<clix::E_UTF8>(name);
		auto scene = ToolsRig::Services::GetPreviewSceneRegistry().CreateScene(MakeStringSection(nativeName), drawablesPool, pipelineAcceleratorPool, deformAcceleratorPool);
		_pimpl->_overlayBinder->SetScene(std::move(scene));
		// _pimpl->ApplyPatchCollection();
	}

	void VisLayerController::SetOverlaySettings(VisOverlaySettings^ settings)
	{
		_pimpl->_visOverlay->Set(*settings->ConvertToNative());
	}

	VisOverlaySettings^ VisLayerController::GetOverlaySettings()
	{
		return VisOverlaySettings::ConvertFromNative(_pimpl->_visOverlay->GetOverlaySettings());
	}

	void VisLayerController::SetPatchCollectionOverrides(CompiledShaderPatchCollectionWrapper^ patchCollection)
	{
		if (patchCollection) {
			_pimpl->_patchCollection = patchCollection->_patchCollection.GetNativePtr();
		} else {
			_pimpl->_patchCollection = nullptr;
		}

		_pimpl->ApplyPatchCollection();
	}

	void VisLayerController::ResetCamera()
	{
		_pimpl->_modelLayer->ResetCamera();
	}

	void VisLayerController::AttachToView(LayerControl^ view)
	{
		auto& overlaySet = view->GetWindowRig().GetMainOverlaySystemSet();
        overlaySet.AddSystem(_pimpl->_modelLayer);
		overlaySet.AddSystem(_pimpl->_visOverlay);
		overlaySet.AddSystem(_pimpl->_manipulatorLayer);
	}

	void VisLayerController::DetachFromView(LayerControl^ view)
	{
		auto& overlaySet = view->GetWindowRig().GetMainOverlaySystemSet();
		overlaySet.RemoveSystem(*_pimpl->_manipulatorLayer);
		overlaySet.RemoveSystem(*_pimpl->_visOverlay);
		overlaySet.RemoveSystem(*_pimpl->_modelLayer);
	}

	void VisLayerController::OnEngineShutdown()
	{
		_pimpl.reset();
	}

	VisLayerController::VisLayerController()
	{
		auto drawingApparatus = EngineDevice::GetInstance()->GetNative().GetDrawingApparatus();
		auto immediateDrawables = EngineDevice::GetInstance()->GetNative().GetImmediateDrawables();
		auto immediateDrawableApparatus = EngineDevice::GetInstance()->GetNative().GetImmediateDrawingApparatus();
		auto lightingEngineApparatus = EngineDevice::GetInstance()->GetNative().GetLightingEngineApparatus();
		auto primaryResourcesApparatus = EngineDevice::GetInstance()->GetNative().GetPrimaryResourcesApparatus();

		_pimpl.reset(new VisLayerControllerPimpl());
		_pimpl->_animState = std::make_shared<ToolsRig::VisAnimationState>();
		_pimpl->_camera = std::make_shared<ToolsRig::VisCameraSettings>();

		_pimpl->_modelLayer = ToolsRig::CreateSimpleSceneOverlay(immediateDrawableApparatus, lightingEngineApparatus, drawingApparatus->_deformAccelerators);
		// _pimpl->_modelLayer->Set(ToolsRig::VisEnvSettings{});
		_pimpl->_modelLayer->Set(_pimpl->_camera);

		_pimpl->_visOverlay = std::make_shared<ToolsRig::VisualisationOverlay>(
			immediateDrawableApparatus,
			ToolsRig::VisOverlaySettings{});
		_pimpl->_visOverlay->Set(_pimpl->_camera);
		_pimpl->_visOverlay->Set(_pimpl->_animState);

		{
			auto manipulators = std::make_shared<ToolsRig::ManipulatorStack>(_pimpl->_camera, drawingApparatus);
			manipulators->Register(
				ToolsRig::ManipulatorStack::CameraManipulator,
				ToolsRig::CreateCameraManipulator(
					_pimpl->_camera,
					ToolsRig::CameraManipulatorMode::Blender_RightButton));
			_pimpl->_manipulatorLayer = ToolsRig::MakeLayerForInput(manipulators);
		}

		_pimpl->_overlayBinder = std::make_shared<ToolsRig::VisOverlayController>(drawingApparatus->_drawablesPool, drawingApparatus->_pipelineAccelerators, drawingApparatus->_deformAccelerators);
		_pimpl->_overlayBinder->AttachSceneOverlay(_pimpl->_modelLayer);
		_pimpl->_overlayBinder->AttachVisualisationOverlay(_pimpl->_visOverlay);

		_pimpl->_overlayBinder->SetEnvSettings("cfg/lighting");		// default env settings

		auto engineDevice = EngineDevice::GetInstance();
		engineDevice->AddOnShutdown(this);
	}

	VisLayerController::~VisLayerController()
	{
		_pimpl.reset();
	}

	VisLayerController::!VisLayerController()
	{
		if (_pimpl.get())
			System::Diagnostics::Debug::Assert(false, "Non deterministic delete of LayerControl");
	}
}

