// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "RenderStep.h"
#include "../RenderCore/LightingEngine/LightDesc.h"

namespace RenderCore { namespace Techniques { class IPipelineAcceleratorPool; }}

namespace SceneEngine
{
	class RenderStep_PrepareRTShadows : public IRenderStep
	{
	public:
		const RenderStepFragmentInterface& GetInterface() const override { return _fragment; }
		void Execute(
			RenderCore::IThreadContext& threadContext,
			RenderCore::Techniques::ParsingContext& parsingContext,
			const RenderCore::Techniques::IPipelineAcceleratorPool& pipelineAccelerators,
			LightingParserContext& lightingParserContext,
			RenderStepFragmentInstance& rpi,
			IViewDelegate* viewDelegate) override;

		RenderStep_PrepareRTShadows();
		~RenderStep_PrepareRTShadows();
	private:
		RenderStepFragmentInterface _fragment;
	};

	class ViewDelegate_Shadow : public IViewDelegate
	{
	public:
		RenderCore::Techniques::DrawablesPacket _general;
		RenderCore::LightingEngine::StandardShadowProjection _shadowProj;

		RenderCore::Techniques::DrawablesPacket* GetDrawablesPacket(RenderCore::Techniques::BatchFilter batch) override;
		void Reset() override;
		ViewDelegate_Shadow(RenderCore::LightingEngine::StandardShadowProjection shadowProjection);
		~ViewDelegate_Shadow();
	};
}
