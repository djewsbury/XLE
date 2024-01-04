// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "NativeModelViewer.h"
#include "../../Tools/ToolsRig/ModelVisualisation.h"
#include "../../Tools/ToolsRig/VisualisationUtils.h"
#include "../../Tools/ToolsRig/BasicManipulators.h"
#include "../../Tools/ToolsRig/ToolsRigServices.h"
#include "../../RenderCore/Techniques/Apparatuses.h"
#include "../../RenderCore/LightingEngine/LightingEngineApparatus.h"
#include "../../PlatformRig/PlatformApparatuses.h"
#include "../../PlatformRig/SingleWindowAppRig.h"
#include "../../Utility/StringFormat.h"

namespace Sample
{
	void NativeModelViewerOverlay::OnStartup(const PlatformRig::AppRigGlobals& globals)
	{
		ToolsRig::MountTextEntityDocument("cfg/lighting", "rawos/defaultenv.dat");

		auto modelLayer = ToolsRig::CreateSimpleSceneOverlay(
			globals._overlayApparatus,
			std::make_shared<RenderCore::LightingEngine::LightingEngineApparatus>(globals._drawingApparatus),
			globals._drawingApparatus->_deformAccelerators);
		AddSystem(modelLayer);

		ToolsRig::VisOverlaySettings overlaySettings;
		overlaySettings._colourByMaterial = 0;
		overlaySettings._drawNormals = false;
		overlaySettings._drawWireframe = false;

		auto visOverlay = std::make_shared<ToolsRig::VisualisationOverlay>(
			globals._overlayApparatus,
			overlaySettings);
		AddSystem(visOverlay);

		_overlayBinder = std::make_shared<ToolsRig::VisOverlayController>(
			globals._drawingApparatus->_drawablesPool, globals._drawingApparatus->_pipelineAccelerators, globals._drawingApparatus->_deformAccelerators,
			globals._windowApparatus->_mainLoadingContext);
		_overlayBinder->AttachSceneOverlay(modelLayer);
		_overlayBinder->AttachVisualisationOverlay(visOverlay);

		auto camera = std::make_shared<ToolsRig::VisCameraSettings>();
		modelLayer->Set(camera);
		_overlayBinder->SetCamera(camera);

		{
			auto manipulators = std::make_shared<ToolsRig::ManipulatorStack>(camera, globals._drawingApparatus);
			manipulators->Register(
				ToolsRig::ManipulatorStack::CameraManipulator,
				ToolsRig::CreateCameraManipulator(camera, ToolsRig::CameraManipulatorMode::Blender_RightButton));
			AddSystem(ToolsRig::MakeLayerForInput(manipulators));
		}

		ToolsRig::ModelVisSettings visSettings {};
		visSettings._modelName = "rawos/game/model/galleon/galleon.dae";
        visSettings._materialName = "rawos/game/model/galleon/galleon.material";
		_overlayBinder->SetScene(visSettings);
		_overlayBinder->SetEnvSettings("cfg/lighting");
	}

	void NativeModelViewerOverlay::Configure(SampleConfiguration& cfg)
	{
		cfg._presentationChainBindFlags = RenderCore::BindFlag::UnorderedAccess;
		cfg._windowTitle = "Native Model Viewer (XLE sample)";
	}

	NativeModelViewerOverlay::NativeModelViewerOverlay() = default;
	NativeModelViewerOverlay::~NativeModelViewerOverlay() = default;

}

