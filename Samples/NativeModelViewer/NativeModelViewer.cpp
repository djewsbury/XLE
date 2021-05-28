// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "NativeModelViewer.h"
#include "../Shared/SampleRig.h"
#include "../../Tools/ToolsRig/ModelVisualisation.h"
#include "../../Tools/ToolsRig/VisualisationUtils.h"
#include "../../Tools/ToolsRig/BasicManipulators.h"
#include "../../RenderCore/Techniques/PipelineAccelerator.h"
#include "../../RenderCore/Techniques/ParsingContext.h"
#include "../../RenderCore/Techniques/Apparatuses.h"
#include "../../RenderCore/LightingEngine/LightingEngineApparatus.h"
#include "../../RenderOverlays/DebuggingDisplay.h"
#include "../../RenderOverlays/Font.h"
#include "../../ConsoleRig/ResourceBox.h"
#include "../../Utility/StringFormat.h"
#include <iomanip>

namespace Sample
{
	void NativeModelViewerOverlay::OnUpdate(float deltaTime)
	{
	}

	static void RenderTrackingOverlay(
        RenderOverlays::IOverlayContext& context,
		const RenderOverlays::DebuggingDisplay::Rect& viewport,
		const ToolsRig::VisMouseOver& mouseOver, 
		const SceneEngine::IScene& scene)
    {
        using namespace RenderOverlays::DebuggingDisplay;

        auto textHeight = (int)RenderOverlays::GetDefaultFont()->GetFontProperties()._lineHeight;
        std::string matName = "matName";
        DrawText(
            &context,
            Rect(Coord2(viewport._topLeft[0]+3, viewport._bottomRight[1]-textHeight-3), Coord2(viewport._bottomRight[0]-3, viewport._bottomRight[1]-3)),
            nullptr, RenderOverlays::ColorB(0xffafafaf),
            StringMeld<512>() 
                << "Material: {Color:7f3faf}" << matName
                << "{Color:afafaf}, Draw call: " << mouseOver._drawCallIndex
                << std::setprecision(4)
                << ", (" << mouseOver._intersectionPt[0]
                << ", "  << mouseOver._intersectionPt[1]
                << ", "  << mouseOver._intersectionPt[2]
                << ")");
    }

	void NativeModelViewerOverlay::OnStartup(const SampleGlobals& globals)
	{
		auto pipelineAccelerators = globals._drawingApparatus->_pipelineAccelerators;
		auto lightingEngineApparatus = std::make_shared<RenderCore::LightingEngine::LightingEngineApparatus>(globals._drawingApparatus);
		auto techniqueContext = globals._drawingApparatus->_techniqueContext;

		auto modelLayer = ToolsRig::CreateSimpleSceneLayer(
			globals._immediateDrawingApparatus,
			lightingEngineApparatus);

		ToolsRig::ModelVisSettings visSettings {};

		auto scene = ToolsRig::MakeScene(pipelineAccelerators, visSettings);
		modelLayer->Set(scene);
		modelLayer->Set(ToolsRig::VisEnvSettings{"rawos/defaultenv.txt:environment"});
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
			mouseOver, techniqueContext, pipelineAccelerators,
			modelLayer->GetCamera());
		trackingOverlay->Set(scene);
		AddSystem(trackingOverlay);

		{
			auto manipulators = std::make_shared<ToolsRig::ManipulatorStack>(modelLayer->GetCamera(), techniqueContext, pipelineAccelerators);
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
        RenderCore::IThreadContext& threadContext,
        RenderCore::Techniques::ParsingContext& parserContext)
	{
		OverlaySystemSet::Render(threadContext, parserContext);
	}

	void NativeModelViewerOverlay::OnRenderTargetUpdate(
		IteratorRange<const RenderCore::Techniques::PreregisteredAttachment*> preregAttachments,
		const RenderCore::FrameBufferProperties& fbProps)
	{
		OverlaySystemSet::OnRenderTargetUpdate(preregAttachments, fbProps);
	}

	NativeModelViewerOverlay::NativeModelViewerOverlay()
	{
	}

	NativeModelViewerOverlay::~NativeModelViewerOverlay()
	{
	}
    
}

