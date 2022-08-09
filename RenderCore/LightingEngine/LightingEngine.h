// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../../Utility/IteratorUtils.h"

namespace RenderCore { class IThreadContext; }
namespace RenderCore { namespace Techniques { class ProjectionDesc; class IDeformAcceleratorPool; class DrawablesPacket; class ParsingContext; struct PreparedResourcesVisibility; }}
namespace Assets { class IAsyncMarker; class DependencyValidation; }
namespace XLEMath { class ArbitraryConvexVolumeTester; }
namespace std { template<typename Type> class future; }

namespace RenderCore { namespace LightingEngine
{
	class LightingEngineApparatus;
	class SharedTechniqueDelegateBox;
	class CompiledLightingTechnique;

	enum class StepType { ParseScene, MultiViewParseScene, DrawSky, ReadyInstances, None, Abort };

	class LightingTechniqueIterator;
	class LightingTechniqueInstance
	{
	public:
		struct Step
		{
			StepType _type = StepType::Abort;
			std::vector<Techniques::DrawablesPacket*> _pkts;			// todo -- candidate for subframe heap
			XLEMath::ArbitraryConvexVolumeTester* _complexCullingVolume = nullptr;
			std::vector<Techniques::ProjectionDesc> _multiViewDesc;		// todo -- candidate for subframe heap
			Techniques::ParsingContext* _parsingContext = nullptr;

			operator bool() const { return _type != StepType::None && _type != StepType::Abort; }
		};
		Step GetNextStep();

		void SetDeformAcceleratorPool(Techniques::IDeformAcceleratorPool&);

		LightingTechniqueInstance(
			Techniques::ParsingContext&,
			CompiledLightingTechnique&);
		~LightingTechniqueInstance();

		// For ensuring that required resources are prepared/loaded
		std::future<Techniques::PreparedResourcesVisibility> GetResourcePreparationMarker();
		LightingTechniqueInstance(
			CompiledLightingTechnique&);
	private:
		std::unique_ptr<LightingTechniqueIterator> _iterator;

		class PrepareResourcesIterator;
		std::unique_ptr<PrepareResourcesIterator> _prepareResourcesIterator;
		Step GetNextPrepareResourcesStep();
		void CleanupPostIteration();
	};

	class ILightScene;
	ILightScene& GetLightScene(CompiledLightingTechnique&);
	const ::Assets::DependencyValidation& GetDependencyValidation(CompiledLightingTechnique&);

}}
