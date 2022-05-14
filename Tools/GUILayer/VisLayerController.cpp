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
#include "../../PlatformRig/InputTranslator.h"
#include "../../PlatformRig/FrameRig.h"
#include "../../PlatformRig/OverlaySystem.h"
#include "../../RenderOverlays/DebuggingDisplay.h"
#include "../../RenderCore/Techniques/Techniques.h"
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
		std::shared_ptr<ToolsRig::ISimpleSceneLayer> _modelLayer;
		std::shared_ptr<PlatformRig::IOverlaySystem> _manipulatorLayer;
		std::shared_ptr<ToolsRig::MouseOverTrackingOverlay> _trackingLayer;
		std::shared_ptr<ToolsRig::VisMouseOver> _mouseOver;
		std::shared_ptr<ToolsRig::VisAnimationState> _animState;

		std::shared_ptr<ToolsRig::DeferredCompiledShaderPatchCollection> _patchCollection;
		std::shared_ptr<SceneEngine::IScene> _scene;

		void ApplyPatchCollection()
		{
			auto* patchCollectionScene = dynamic_cast<ToolsRig::IPatchCollectionVisualizationScene*>(_scene.get());
			if (patchCollectionScene)
				patchCollectionScene->SetPatchCollection(_patchCollection->GetFuture());
		}
    };

	VisMouseOver^ VisLayerController::MouseOver::get()
	{
		return gcnew VisMouseOver(_pimpl->_mouseOver, _pimpl->_scene);
	}

	VisAnimationState^ VisLayerController::AnimationState::get()
	{
		return gcnew VisAnimationState(_pimpl->_animState);
	}

	void VisLayerController::SetScene(ModelVisSettings^ settings)
	{
		auto pipelineAcceleratorPool = EngineDevice::GetInstance()->GetNative().GetMainPipelineAcceleratorPool();
		auto nativeSettings = settings->ConvertToNative();
		_pimpl->_scene = ToolsRig::MakeScene(pipelineAcceleratorPool, *nativeSettings);
		_pimpl->ApplyPatchCollection();
		_pimpl->_modelLayer->Set(_pimpl->_scene);
		_pimpl->_visOverlay->Set(_pimpl->_scene);
		_pimpl->_trackingLayer->Set(_pimpl->_scene);
	}

	void VisLayerController::SetScene(MaterialVisSettings^ settings)
	{
		auto pipelineAcceleratorPool = EngineDevice::GetInstance()->GetNative().GetMainPipelineAcceleratorPool();
		auto nativeSettings = settings->ConvertToNative();
		_pimpl->_scene = ToolsRig::MakeScene(pipelineAcceleratorPool, *nativeSettings);
		_pimpl->ApplyPatchCollection();
		_pimpl->_modelLayer->Set(_pimpl->_scene);
		_pimpl->_visOverlay->Set(_pimpl->_scene);
		_pimpl->_trackingLayer->Set(_pimpl->_scene);
	}

	void VisLayerController::SetPreviewRegistryScene(System::String^ name)
	{
		auto pipelineAcceleratorPool = EngineDevice::GetInstance()->GetNative().GetMainPipelineAcceleratorPool();
		auto nativeName = clix::marshalString<clix::E_UTF8>(name);
		_pimpl->_scene = ToolsRig::Services::GetPreviewSceneRegistry().CreateScene(MakeStringSection(nativeName), pipelineAcceleratorPool);
		_pimpl->ApplyPatchCollection();
		_pimpl->_modelLayer->Set(_pimpl->_scene);
		_pimpl->_visOverlay->Set(_pimpl->_scene);
		_pimpl->_trackingLayer->Set(_pimpl->_scene);
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
		overlaySet.AddSystem(_pimpl->_trackingLayer);
	}

	void VisLayerController::DetachFromView(LayerControl^ view)
	{
		auto& overlaySet = view->GetWindowRig().GetMainOverlaySystemSet();
		overlaySet.RemoveSystem(*_pimpl->_trackingLayer);
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
		_pimpl->_mouseOver = std::make_shared<ToolsRig::VisMouseOver>();
		_pimpl->_animState = std::make_shared<ToolsRig::VisAnimationState>();

		_pimpl->_modelLayer = ToolsRig::CreateSimpleSceneLayer(immediateDrawableApparatus, lightingEngineApparatus, drawingApparatus->_deformAccelerators);
		_pimpl->_modelLayer->Set(ToolsRig::VisEnvSettings{});

		_pimpl->_visOverlay = std::make_shared<ToolsRig::VisualisationOverlay>(
			immediateDrawableApparatus,
			ToolsRig::VisOverlaySettings{},
            _pimpl->_mouseOver);
		_pimpl->_visOverlay->Set(_pimpl->_modelLayer->GetCamera());
		_pimpl->_visOverlay->Set(_pimpl->_animState);

		{
			auto manipulators = std::make_shared<ToolsRig::ManipulatorStack>(_pimpl->_modelLayer->GetCamera(), drawingApparatus);
			manipulators->Register(
				ToolsRig::ManipulatorStack::CameraManipulator,
				ToolsRig::CreateCameraManipulator(
					_pimpl->_modelLayer->GetCamera(), 
					ToolsRig::CameraManipulatorMode::Blender_RightButton));
			_pimpl->_manipulatorLayer = ToolsRig::MakeLayerForInput(manipulators);
		}

		_pimpl->_trackingLayer = std::make_shared<ToolsRig::MouseOverTrackingOverlay>(
			_pimpl->_mouseOver,
			drawingApparatus,
			_pimpl->_modelLayer->GetCamera());

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

