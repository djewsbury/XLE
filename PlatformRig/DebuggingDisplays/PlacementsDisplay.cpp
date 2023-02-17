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
#include "../../Assets/Marker.h"
#include "../../Utility/StringFormat.h"

using namespace Utility::Literals;

namespace PlatformRig { namespace Overlays
{
	using namespace RenderOverlays;
	using namespace RenderOverlays::DebuggingDisplay;

	template<typename T>
		T TryAnyCast(std::any&& any, T defaultValue)
	{
		if (any.has_value() && any.type() == typeid(T))
			return std::any_cast<T>(std::move(any));
		return defaultValue;
	}

	class PlacementsDisplay : public IWidget ///////////////////////////////////////////////////////////
	{
	public:
		void Render(IOverlayContext& context, Layout& layout, Interactables&interactables, InterfaceState& interfaceState)
		{
			const unsigned lineHeight = 20;
			const auto titleBkground = RenderOverlays::ColorB { 51, 51, 51 };

			auto allocation = layout.AllocateFullWidth(30);
			FillRectangle(context, allocation, titleBkground);
			allocation._topLeft[0] += 8;
			if (auto* font = _headingFont->TryActualize())
				DrawText()
					.Font(**font)
					.Color({ 191, 123, 0 })
					.Alignment(RenderOverlays::TextAlignment::Left)
					.Flags(RenderOverlays::DrawTextFlags::Shadow)
					.Draw(context, allocation, "Placements Selector");
			
			if (_hasSelectedPlacements) {
				char meldBuffer[256];
				DrawText()
					.Color(0xffcfcfcf)
					.Draw(context, layout.AllocateFullWidth(lineHeight), StringMeldInPlace(meldBuffer) << "Model: " << _selectedModelName);
				DrawText()
					.Color(0xffcfcfcf)
					.Draw(context, layout.AllocateFullWidth(lineHeight), StringMeldInPlace(meldBuffer) << "Material: " << _selectedMaterialName);
			}

			if (_hasLastRayTest)
				context.DrawLines(ProjectionMode::P3D, &_lastRayTest.first, 2, ColorB{255, 128, 128});
		}

		virtual ProcessInputResult ProcessInput(InterfaceState& interfaceState, const PlatformRig::InputSnapshot& input)
		{
			// Given the camera & viewport find a ray & perform intersection detection withte placements scene
			if (input.IsRelease_LButton()) {
				UInt2 viewportDims { 1920, 1080 };	// todo -- get real values
				auto cameraDesc = ToolsRig::AsCameraDesc(*_camera);
				auto worldSpaceRay = SceneEngine::CalculateWorldSpaceRay(
					cameraDesc, input._mousePosition, {0,0}, viewportDims);

				_lastRayTest = worldSpaceRay;
				_hasLastRayTest = true;

				auto threadContext = RenderCore::Techniques::GetThreadContext();
				auto techniqueContext = SceneEngine::MakeIntersectionsTechniqueContext(*_drawingApparatus);
				RenderCore::Techniques::ParsingContext parsingContext{techniqueContext, *threadContext};
				parsingContext.SetPipelineAcceleratorsVisibility(techniqueContext._pipelineAccelerators->VisibilityBarrier());
				parsingContext.GetProjectionDesc() = RenderCore::Techniques::BuildProjectionDesc(cameraDesc, viewportDims);

				auto firstHit = SceneEngine::FirstRayIntersection(parsingContext, *_placementsEditor, worldSpaceRay, &cameraDesc);
				if (firstHit) {
					_selectedMaterialName = _selectedModelName = {};
					if (firstHit->_metadataQuery) {
						_selectedMaterialName = TryAnyCast(firstHit->_metadataQuery("MaterialName"_h), _selectedMaterialName);
						_selectedModelName = TryAnyCast(firstHit->_metadataQuery("ModelScaffold"_h), _selectedModelName);
					}
					_hasSelectedPlacements = true;
				} else {
					_selectedMaterialName = _selectedModelName = {};
					_hasSelectedPlacements = false;
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
			_headingFont = RenderOverlays::MakeFont("DosisExtraBold", 20);
		}

	private:
		std::shared_ptr<RenderCore::Techniques::DrawingApparatus> _drawingApparatus;
		std::shared_ptr<SceneEngine::PlacementsEditor> _placementsEditor;
		std::shared_ptr<ToolsRig::VisCameraSettings> _camera;

		std::string _selectedModelName, _selectedMaterialName;
		bool _hasSelectedPlacements = false;

		std::pair<Float3, Float3> _lastRayTest;
		bool _hasLastRayTest = false;

		::Assets::PtrToMarkerPtr<RenderOverlays::Font> _headingFont;
	};

	std::shared_ptr<RenderOverlays::DebuggingDisplay::IWidget> CreatePlacementsDisplay(
		std::shared_ptr<RenderCore::Techniques::DrawingApparatus> drawingApparatus,
		std::shared_ptr<SceneEngine::PlacementsEditor> placements,
		std::shared_ptr<ToolsRig::VisCameraSettings> camera)
	{
		return std::make_shared<PlacementsDisplay>(std::move(drawingApparatus), std::move(placements), std::move(camera));
	}

}}

