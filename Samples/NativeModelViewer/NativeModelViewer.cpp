// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "NativeModelViewer.h"
#include "../Shared/SampleRig.h"
#include "../../Tools/ToolsRig/ModelVisualisation.h"
#include "../../Tools/ToolsRig/VisualisationUtils.h"
#include "../../Tools/ToolsRig/BasicManipulators.h"
#include "../../Tools/ToolsRig/ToolsRigServices.h"
#include "../../Tools/EntityInterface/EntityInterface.h"
#include "../../Tools/EntityInterface/FormatterAdapters.h"
#include "../../RenderCore/Techniques/PipelineAccelerator.h"
#include "../../RenderCore/Techniques/ParsingContext.h"
#include "../../RenderCore/Techniques/Apparatuses.h"
#include "../../RenderCore/LightingEngine/LightingEngineApparatus.h"
#include "../../RenderOverlays/DebuggingDisplay.h"
#include "../../ConsoleRig/ResourceBox.h"
#include "../../Utility/StringFormat.h"
#include <iomanip>

namespace Sample
{
	void NativeModelViewerOverlay::OnUpdate(float deltaTime)
	{
	}

	void NativeModelViewerOverlay::OnStartup(const SampleGlobals& globals)
	{
		auto pipelineAccelerators = globals._drawingApparatus->_pipelineAccelerators;
		auto lightingEngineApparatus = std::make_shared<RenderCore::LightingEngine::LightingEngineApparatus>(globals._drawingApparatus);

		auto modelLayer = ToolsRig::CreateSimpleSceneLayer(
			globals._immediateDrawingApparatus,
			lightingEngineApparatus,
			globals._drawingApparatus->_deformAccelerators);

		ToolsRig::ModelVisSettings visSettings {};
		ToolsRig::Services::GetEntityMountingTree().MountDocument("cfg/lighting", EntityInterface::CreateTextEntityDocument("rawos/defaultenv.dat"));

		auto scene = ToolsRig::MakeScene(globals._drawingApparatus->_drawablesPool, pipelineAccelerators, globals._drawingApparatus->_deformAccelerators, visSettings);
		modelLayer->Set(scene);
		modelLayer->Set([]() { return ToolsRig::MakeLightingStateDelegate("cfg/lighting"); });
		AddSystem(modelLayer);

		auto mouseOver = std::make_shared<ToolsRig::VisMouseOver>();
		ToolsRig::VisOverlaySettings overlaySettings;
		overlaySettings._colourByMaterial = 2;
		overlaySettings._drawNormals = true;
		overlaySettings._drawWireframe = false;

		auto visOverlay = std::make_shared<ToolsRig::VisualisationOverlay>(
			globals._immediateDrawingApparatus,
			overlaySettings, mouseOver);
		visOverlay->Set(scene);
		visOverlay->Set(modelLayer->GetCamera());
		AddSystem(visOverlay);

		auto trackingOverlay = std::make_shared<ToolsRig::MouseOverTrackingOverlay>(
			mouseOver, globals._drawingApparatus,
			modelLayer->GetCamera());
		trackingOverlay->Set(scene);
		AddSystem(trackingOverlay);

		{
			auto manipulators = std::make_shared<ToolsRig::ManipulatorStack>(modelLayer->GetCamera(), globals._drawingApparatus);
			manipulators->Register(
				ToolsRig::ManipulatorStack::CameraManipulator,
				ToolsRig::CreateCameraManipulator(modelLayer->GetCamera(), ToolsRig::CameraManipulatorMode::Blender_RightButton));
			AddSystem(ToolsRig::MakeLayerForInput(manipulators));
		}
	}

	auto NativeModelViewerOverlay::GetInputListener() -> std::shared_ptr<PlatformRig::IInputListener>
	{ 
		return OverlaySystemSet::GetInputListener(); 
	}
	
	void NativeModelViewerOverlay::SetActivationState(bool newState) 
	{
		OverlaySystemSet::SetActivationState(newState);
	}

	void NativeModelViewerOverlay::Render(
        RenderCore::Techniques::ParsingContext& parserContext)
	{
		OverlaySystemSet::Render(parserContext);
	}

	void NativeModelViewerOverlay::OnRenderTargetUpdate(
		IteratorRange<const RenderCore::Techniques::PreregisteredAttachment*> preregAttachments,
		const RenderCore::FrameBufferProperties& fbProps,
		IteratorRange<const RenderCore::Format*> systemAttachmentFormats)
	{
		OverlaySystemSet::OnRenderTargetUpdate(preregAttachments, fbProps, systemAttachmentFormats);
	}

	NativeModelViewerOverlay::NativeModelViewerOverlay()
	{
	}

	NativeModelViewerOverlay::~NativeModelViewerOverlay()
	{
	}
    
}

