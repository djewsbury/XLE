// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../Assets/DepVal.h"
#include "../ConsoleRig/AttachablePtr.h"
#include "../Utility/FunctionUtils.h"		// SignalDelegateId
#include <memory>

namespace RenderCore { namespace Techniques { class DrawingApparatus; class IImmediateDrawables; class Services; class ParsingContext; class RenderPassInstance; class IPipelineAcceleratorPool; }}

namespace RenderOverlays
{
	class ShapesRenderingDelegate;
	class FontRenderingManager;
	class FTFontResources;

	class OverlayApparatus
	{
	public:
		std::shared_ptr<RenderCore::Techniques::DrawingApparatus> _mainDrawingApparatus;
		std::shared_ptr<RenderCore::Techniques::IImmediateDrawables> _immediateDrawables;
		std::shared_ptr<RenderCore::Techniques::IPipelineAcceleratorPool> _overlayPipelineAccelerators;

		ConsoleRig::AttachablePtr<FTFontResources> _fontResources;
		std::shared_ptr<FontRenderingManager> _fontRenderingManager;
		std::shared_ptr<ShapesRenderingDelegate> _shapeRenderingDelegate;

		SignalDelegateId _frameBarrierBinding;

		const ::Assets::DependencyValidation& GetDependencyValidation() const { return _depValPtr; }
		::Assets::DependencyValidation _depValPtr;

		ConsoleRig::AttachablePtr<RenderCore::Techniques::Services> _techniqueServices;

		OverlayApparatus(std::shared_ptr<RenderCore::Techniques::DrawingApparatus>);
		~OverlayApparatus();
		OverlayApparatus(OverlayApparatus&) = delete;
		OverlayApparatus& operator=(OverlayApparatus&) = delete;
	};

	void ExecuteDraws(
		RenderCore::Techniques::ParsingContext&,
		RenderCore::Techniques::RenderPassInstance&,
		OverlayApparatus&);
	void ExecuteDraws(
		RenderCore::Techniques::ParsingContext&,
		RenderCore::Techniques::RenderPassInstance&,
		RenderCore::Techniques::IImmediateDrawables&,
		ShapesRenderingDelegate&);
}

