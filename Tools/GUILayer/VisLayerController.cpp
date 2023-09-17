// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "VisLayerController.h"
#include "LayerControl.h"
#include "EngineDevice.h"
#include "NativeEngineDevice.h"
#include "../../SceneEngine/BasicLightingStateDelegate.h"
#include "../ToolsRig/VisualisationUtils.h"
#include "../ToolsRig/BasicManipulators.h"
#include "../ToolsRig/PreviewSceneRegistry.h"
#include "../ToolsRig/ToolsRigServices.h"
#include "../ToolsRig/MiscUtils.h"
#include "../../RenderCore/Techniques/Apparatuses.h"
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
		std::shared_ptr<::Assets::OperationContext> _loadingContext;
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
	}

	void VisLayerController::SetScene(MaterialVisSettings^ settings)
	{
		_pimpl->_overlayBinder->SetScene(*settings->ConvertToNative());
	}

	void VisLayerController::SetPreviewRegistryScene(System::String^ name)
	{
		auto pipelineAcceleratorPool = EngineDevice::GetInstance()->GetNative().GetMainPipelineAcceleratorPool();
		auto drawablesPool = EngineDevice::GetInstance()->GetNative().GetDrawingApparatus()->_drawablesPool;
		auto deformAcceleratorPool = EngineDevice::GetInstance()->GetNative().GetDrawingApparatus()->_deformAccelerators;
		auto nativeName = clix::marshalString<clix::E_UTF8>(name);
		auto scene = ToolsRig::Services::GetPreviewSceneRegistry().CreateScene(MakeStringSection(nativeName), drawablesPool, pipelineAcceleratorPool, deformAcceleratorPool, _pimpl->_loadingContext);
		_pimpl->_overlayBinder->SetScene(std::move(scene));
	}

	void VisLayerController::SetEnvSettings(System::String^ mountedEnvSettings)
	{
		_pimpl->_overlayBinder->SetEnvSettings(clix::marshalString<clix::E_UTF8>(mountedEnvSettings));
	}

	static RenderCore::Techniques::UtilityDelegateType AsUtilityDelegateType(UtilityRenderingType renderingType)
	{
		switch (renderingType) {
		default:
		case UtilityRenderingType::FlatColor: return RenderCore::Techniques::UtilityDelegateType::FlatColor;
		case UtilityRenderingType::CopyDiffuseAlbedo: return RenderCore::Techniques::UtilityDelegateType::CopyDiffuseAlbedo;
		case UtilityRenderingType::CopyWorldSpacePosition: return RenderCore::Techniques::UtilityDelegateType::CopyWorldSpacePosition;
		case UtilityRenderingType::CopyWorldSpaceNormal: return RenderCore::Techniques::UtilityDelegateType::CopyWorldSpaceNormal;
		case UtilityRenderingType::CopyRoughness: return RenderCore::Techniques::UtilityDelegateType::CopyRoughness;
		case UtilityRenderingType::CopyMetal: return RenderCore::Techniques::UtilityDelegateType::CopyMetal;
		case UtilityRenderingType::CopySpecular: return RenderCore::Techniques::UtilityDelegateType::CopySpecular;
		case UtilityRenderingType::CopyCookedAO: return RenderCore::Techniques::UtilityDelegateType::CopyCookedAO;
		case UtilityRenderingType::SolidWireframe: return RenderCore::Techniques::UtilityDelegateType::SolidWireframe;
		}
	}

	void VisLayerController::SetUtilityRenderingType(UtilityRenderingType renderingType)
	{
		_pimpl->_overlayBinder->SetEnvSettings(SceneEngine::CreateUtilityLightingStateDelegate(AsUtilityDelegateType(renderingType)));
	}

	void VisLayerController::SetOverlaySettings(VisOverlaySettings^ settings)
	{
		_pimpl->_visOverlay->Set(*settings->ConvertToNative());
	}

	VisOverlaySettings^ VisLayerController::GetOverlaySettings()
	{
		return VisOverlaySettings::ConvertFromNative(_pimpl->_visOverlay->GetOverlaySettings());
	}

	void VisLayerController::ResetCamera()
	{
		_pimpl->_modelLayer->ResetCamera();
	}

	void VisLayerController::AttachToView(LayerControl^ view)
	{
		auto& overlaySet = view->GetMainOverlaySystemSet();
        overlaySet.AddSystem(_pimpl->_modelLayer);
		overlaySet.AddSystem(_pimpl->_visOverlay);
		overlaySet.AddSystem(_pimpl->_manipulatorLayer);
		view->UpdateRenderTargets();
	}

	void VisLayerController::DetachFromView(LayerControl^ view)
	{
		auto& overlaySet = view->GetMainOverlaySystemSet();
		overlaySet.RemoveSystem(*_pimpl->_manipulatorLayer);
		overlaySet.RemoveSystem(*_pimpl->_visOverlay);
		overlaySet.RemoveSystem(*_pimpl->_modelLayer);
		view->UpdateRenderTargets();
	}

	void VisLayerController::OnEngineShutdown()
	{
		_pimpl.reset();
	}

	VisLayerController::VisLayerController()
	{
		auto drawingApparatus = EngineDevice::GetInstance()->GetNative().GetDrawingApparatus();
		auto immediateDrawables = EngineDevice::GetInstance()->GetNative().GetImmediateDrawables();
		auto immediateDrawableApparatus = EngineDevice::GetInstance()->GetNative().GetOverlayApparatus();
		auto lightingEngineApparatus = EngineDevice::GetInstance()->GetNative().GetLightingEngineApparatus();
		auto primaryResourcesApparatus = EngineDevice::GetInstance()->GetNative().GetPrimaryResourcesApparatus();

		_pimpl.reset(new VisLayerControllerPimpl());
		_pimpl->_animState = std::make_shared<ToolsRig::VisAnimationState>();
		_pimpl->_camera = std::make_shared<ToolsRig::VisCameraSettings>();
		_pimpl->_loadingContext = ToolsRig::CreateLoadingContext();

		_pimpl->_modelLayer = ToolsRig::CreateSimpleSceneOverlay(immediateDrawableApparatus, lightingEngineApparatus, drawingApparatus->_deformAccelerators);
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

		_pimpl->_overlayBinder = std::make_shared<ToolsRig::VisOverlayController>(drawingApparatus->_drawablesPool, drawingApparatus->_pipelineAccelerators, drawingApparatus->_deformAccelerators, _pimpl->_loadingContext);
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
		System::Diagnostics::Debug::Assert(!_pimpl.get(), "Non deterministic delete of LayerControl");
	}
}

