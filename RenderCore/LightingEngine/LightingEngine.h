// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../Techniques/Drawables.h"		// for Techniques::BatchFilter

namespace RenderCore { class IThreadContext; }

namespace RenderCore { namespace LightingEngine
{
	class LightingEngineApparatus;
	class SharedTechniqueDelegateBox;
	class CompiledLightingTechnique;

	enum class StepType { ParseScene, DrawSky, None, Abort };

	class LightingTechniqueIterator;
	class LightingTechniqueInstance
	{
	public:
		struct Step
		{
			StepType _type = StepType::Abort;
			Techniques::BatchFilter _batch = Techniques::BatchFilter::Max;
			Techniques::DrawablesPacket* _pkt = nullptr;
		};
		Step GetNextStep();

		LightingTechniqueInstance(
			IThreadContext&,
			Techniques::ParsingContext&,
			Techniques::IPipelineAcceleratorPool&,
			CompiledLightingTechnique&);
		~LightingTechniqueInstance();

		// For ensuring that required resources are prepared/loaded
		std::shared_ptr<::Assets::IAsyncMarker> GetResourcePreparationMarker();
		LightingTechniqueInstance(
			Techniques::IPipelineAcceleratorPool&,
			CompiledLightingTechnique&);
	private:
		std::unique_ptr<LightingTechniqueIterator> _iterator;

		class PrepareResourcesIterator;
		std::unique_ptr<PrepareResourcesIterator> _prepareResourcesIterator;
		Step GetNextPrepareResourcesStep();
	};

	class ILightScene;
	ILightScene& GetLightScene(CompiledLightingTechnique&);

}}
