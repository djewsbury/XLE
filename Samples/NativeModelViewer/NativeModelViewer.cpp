// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "NativeModelViewer.h"
#include "../Shared/SampleRig.h"
#include "../../Tools/ToolsRig/ModelVisualisation.h"
#include "../../Tools/ToolsRig/VisualisationUtils.h"
#include "../../Tools/ToolsRig/BasicManipulators.h"
#include "../../Tools/ToolsRig/ToolsRigServices.h"
#include "../../RenderCore/Techniques/Apparatuses.h"
#include "../../RenderCore/LightingEngine/LightingEngineApparatus.h"
#include "../../PlatformRig/DebuggingDisplays/PipelineAcceleratorDisplay.h"
#include "../../PlatformRig/DebuggingDisplays/DeformAcceleratorDisplay.h"
#include "../../PlatformRig/DebuggingDisplays/VulkanMemoryDisplay.h"
#include "../../PlatformRig/DebuggingDisplays/BufferUploadDisplay.h"
#include "../../PlatformRig/DebuggingDisplays/InvalidAssetDisplay.h"
#include "../../PlatformRig/DebugScreenRegistry.h"
#include "../../Assets/OperationContext.h"
#include "../../ConsoleRig/ResourceBox.h"
#include "../../Utility/StringFormat.h"

namespace Sample
{
	void NativeModelViewerOverlay::OnUpdate(float deltaTime)
	{
	}

	void NativeModelViewerOverlay::OnStartup(const SampleGlobals& globals)
	{
		ToolsRig::MountTextEntityDocument("cfg/lighting", "rawos/defaultenv.dat");

		auto camera = std::make_shared<ToolsRig::VisCameraSettings>();
		auto modelLayer = ToolsRig::CreateSimpleSceneOverlay(
			globals._immediateDrawingApparatus,
			std::make_shared<RenderCore::LightingEngine::LightingEngineApparatus>(globals._drawingApparatus),
			globals._drawingApparatus->_deformAccelerators);
		modelLayer->Set(camera);
		AddSystem(modelLayer);

		ToolsRig::VisOverlaySettings overlaySettings;
		overlaySettings._colourByMaterial = 2;
		overlaySettings._drawNormals = true;
		overlaySettings._drawWireframe = false;

		auto visOverlay = std::make_shared<ToolsRig::VisualisationOverlay>(
			globals._immediateDrawingApparatus,
			overlaySettings);
		visOverlay->Set(camera);
		AddSystem(visOverlay);

		{
			auto manipulators = std::make_shared<ToolsRig::ManipulatorStack>(camera, globals._drawingApparatus);
			manipulators->Register(
				ToolsRig::ManipulatorStack::CameraManipulator,
				ToolsRig::CreateCameraManipulator(camera, ToolsRig::CameraManipulatorMode::Blender_RightButton));
			AddSystem(ToolsRig::MakeLayerForInput(manipulators));
		}

		auto loadingContext = std::make_shared<::Assets::OperationContext>();
		_overlayBinder = std::make_shared<ToolsRig::VisOverlayController>(
			globals._drawingApparatus->_drawablesPool, globals._drawingApparatus->_pipelineAccelerators, globals._drawingApparatus->_deformAccelerators,
			loadingContext);
		_overlayBinder->AttachSceneOverlay(modelLayer);
		_overlayBinder->AttachVisualisationOverlay(visOverlay);

		ToolsRig::ModelVisSettings visSettings {};
		_overlayBinder->SetScene(visSettings);
		_overlayBinder->SetEnvSettings("cfg/lighting");

		/*scene->StallWhilePending();
		TRY {
			auto actualizedScene = scene->Actualize();
			auto* visContent = dynamic_cast<ToolsRig::IVisContent*>(actualizedScene.get());
			if (visContent) {
				auto animationState = std::make_shared<ToolsRig::VisAnimationState>();
				animationState->_activeAnimation = "idle";
				animationState->_state = ToolsRig::VisAnimationState::State::Playing;
				animationState->_anchorTime = std::chrono::steady_clock::now();
				visContent->BindAnimationState(animationState);
			}
		} CATCH(...) {
		} CATCH_END*/

		_displayRegistrations.emplace_back(
			"PipelineAccelerators", 
			PlatformRig::Overlays::CreatePipelineAcceleratorPoolDisplay(globals._drawingApparatus->_pipelineAccelerators));

		_displayRegistrations.emplace_back(
			"DeformAccelerators",
			PlatformRig::Overlays::CreateDeformAcceleratorPoolDisplay(globals._drawingApparatus->_deformAccelerators));

		_displayRegistrations.emplace_back(
			"Vulkan Memory Allocator",
			PlatformRig::Overlays::CreateVulkanMemoryAllocatorDisplay(globals._drawingApparatus->_device));

		_displayRegistrations.emplace_back(
			"Buffer Uploads Display",
			std::make_shared<PlatformRig::Overlays::BufferUploadDisplay>(globals._primaryResourcesApparatus->_bufferUploads.get()));

		_displayRegistrations.emplace_back(
			"LoadingContext",
			std::make_shared<PlatformRig::Overlays::OperationContextDisplay>(loadingContext));
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

