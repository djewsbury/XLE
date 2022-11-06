// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "PlacementsDisplay.h"
#include "../../SceneEngine/PlacementsManager.h"
#include "../../SceneEngine/RayVsModel.h"
#include "../../SceneEngine/IntersectionTest.h"
#include "../../RenderCore/Techniques/Apparatuses.h"
#include "../../RenderCore/Techniques/ParsingContext.h"
#include "../../RenderCore/Techniques/TechniqueUtils.h"
#include "../../RenderCore/Techniques/Techniques.h"
#include "../../RenderCore/Techniques/PipelineAccelerator.h"
#include "../../RenderOverlays/DebuggingDisplay.h"
#include "../../Tools/ToolsRig/VisualisationUtils.h"

namespace PlatformRig { namespace Overlays
{
	using namespace RenderOverlays;
	using namespace RenderOverlays::DebuggingDisplay;

	class PlacementsDisplay : public IWidget ///////////////////////////////////////////////////////////
	{
	public:
		void    Render(IOverlayContext& context, Layout& layout, Interactables&interactables, InterfaceState& interfaceState)
		{}

		virtual ProcessInputResult ProcessInput(InterfaceState& interfaceState, const PlatformRig::InputSnapshot& input)
		{
			// Given the camera & viewport find a ray & perform intersection detection withte placements scene
			if (input.IsRelease_LButton()) {
				UInt2 viewportDims { 1920, 1080 };	// todo -- get real values
				auto cameraDesc = ToolsRig::AsCameraDesc(*_camera);
				auto worldSpaceRay = SceneEngine::CalculateWorldSpaceRay(
					cameraDesc, input._mousePosition, {0,0}, viewportDims);

				auto& threadContext = *RenderCore::Techniques::GetThreadContext();
				auto techniqueContext = SceneEngine::MakeIntersectionsTechniqueContext(*_drawingApparatus);
				RenderCore::Techniques::ParsingContext parsingContext{techniqueContext, threadContext};
				parsingContext.SetPipelineAcceleratorsVisibility(techniqueContext._pipelineAccelerators->VisibilityBarrier());
				parsingContext.GetProjectionDesc() = RenderCore::Techniques::BuildProjectionDesc(cameraDesc, viewportDims);

				auto firstHit = SceneEngine::FirstRayIntersection(parsingContext, *_placementsEditor, worldSpaceRay, nullptr);
				if (firstHit) {
					_selectedMaterialName = firstHit->_materialName;
					_selectedModelName = firstHit->_modelName;
				} else {
					_selectedMaterialName = _selectedModelName = {};
				}

				return ProcessInputResult::Consumed;
			}

			if (input.IsPress_LButton())
				return ProcessInputResult::Consumed;

			return ProcessInputResult::Passthrough;
		}

		PlacementsDisplay(
			std::shared_ptr<RenderCore::Techniques::DrawingApparatus> drawingApparatus,
			std::shared_ptr<SceneEngine::PlacementsEditor> placements, 
			std::shared_ptr<ToolsRig::VisCameraSettings> camera)
		: _drawingApparatus(std::move(drawingApparatus))
		, _placementsEditor(std::move(placements))
		, _camera(std::move(camera))
		{
		}

	private:
		std::shared_ptr<RenderCore::Techniques::DrawingApparatus> _drawingApparatus;
		std::shared_ptr<SceneEngine::PlacementsEditor> _placementsEditor;
		std::shared_ptr<ToolsRig::VisCameraSettings> _camera;

		std::string _selectedModelName, _selectedMaterialName;
	};

	std::shared_ptr<RenderOverlays::DebuggingDisplay::IWidget> CreatePlacementsDisplay(
		std::shared_ptr<RenderCore::Techniques::DrawingApparatus> drawingApparatus,
		std::shared_ptr<SceneEngine::PlacementsEditor> placements,
		std::shared_ptr<ToolsRig::VisCameraSettings> camera)
	{
		return std::make_shared<PlacementsDisplay>(std::move(drawingApparatus), std::move(placements), std::move(camera));
	}

}}

