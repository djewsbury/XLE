// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "OverlayApparatus.h"
#include "ShapesRendering.h"
#include "FontRendering.h"
#include "../RenderCore/Techniques/Apparatuses.h"
#include "../RenderCore/Techniques/SubFrameEvents.h"
#include "../RenderCore/Techniques/ImmediateDrawables.h"
#include "../RenderCore/Techniques/Services.h"

namespace RenderOverlays
{
	OverlayApparatus::OverlayApparatus(std::shared_ptr<RenderCore::Techniques::DrawingApparatus> mainDrawingApparatus)
	{
		_depValPtr = ::Assets::GetDepValSys().Make();

		_mainDrawingApparatus = std::move(mainDrawingApparatus);
		_depValPtr.RegisterDependency(_mainDrawingApparatus->GetDependencyValidation());

		_shapeRenderingDelegate = std::make_shared<ShapesRenderingDelegate>();
		
		_immediateDrawables =  RenderCore::Techniques::CreateImmediateDrawables(_mainDrawingApparatus->_device, _shapeRenderingDelegate->GetPipelineLayoutDelegate());
		_fontRenderingManager = std::make_shared<RenderOverlays::FontRenderingManager>(*_mainDrawingApparatus->_device);

		auto& subFrameEvents = _techniqueServices->GetSubFrameEvents();
		_frameBarrierBinding = subFrameEvents._onFrameBarrier.Bind(
			[im=std::weak_ptr<RenderCore::Techniques::IImmediateDrawables>{_immediateDrawables}]() {
				auto l = im.lock();
				if (l) l->OnFrameBarrier();
			});
	}
	
	OverlayApparatus::~OverlayApparatus()
	{
		auto& subFrameEvents = _techniqueServices->GetSubFrameEvents();
		subFrameEvents._onFrameBarrier.Unbind(_frameBarrierBinding);
	}

	void ExecuteDraws(
		RenderCore::Techniques::ParsingContext& parsingContext,
		RenderCore::Techniques::RenderPassInstance& rpi,
		OverlayApparatus& apparatus)
	{
		apparatus._immediateDrawables->ExecuteDraws(parsingContext, apparatus._shapeRenderingDelegate->GetTechniqueDelegate(), rpi);
	}
	void ExecuteDraws(
		RenderCore::Techniques::ParsingContext& parsingContext,
		RenderCore::Techniques::RenderPassInstance& rpi,
		RenderCore::Techniques::IImmediateDrawables& immediateDrawables,
		ShapesRenderingDelegate& shapesRenderingDelegate)
	{
		immediateDrawables.ExecuteDraws(parsingContext, shapesRenderingDelegate.GetTechniqueDelegate(), rpi);
	}
}
