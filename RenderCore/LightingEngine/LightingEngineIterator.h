// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../Techniques/RenderPass.h"
#include "../Techniques/TechniqueUtils.h"
#include <vector>
#include <memory>
#include <variant>

namespace RenderCore { class IThreadContext; }
namespace RenderCore { namespace Techniques { class ParsingContext; class IPipelineAcceleratorPool; class IDeformAcceleratorPool; class DrawablesPacket; class IShaderResourceDelegate; class SequencerConfig; struct PreparedResourcesVisibility; } }
namespace RenderCore { namespace Techniques { namespace BatchFlags { using BitField = unsigned; }}}
namespace RenderCore { namespace BufferUploads { using CommandListID = uint32_t; }}
namespace std { template<typename T> class promise; }

namespace RenderCore { namespace LightingEngine
{
	using TechniqueSequenceParseId = unsigned;
	class CompiledLightingTechnique;
	class LightingTechniqueStepper;
	class LightingTechniqueSequence;

	class LightingTechniqueIterator
	{
	public:
		IThreadContext* _threadContext = nullptr;
		Techniques::ParsingContext* _parsingContext = nullptr;
		Techniques::RenderPassInstance _rpi;

		void ExecuteDrawables(
			TechniqueSequenceParseId parseId,
			Techniques::SequencerConfig& sequencerCfg,
			const std::shared_ptr<Techniques::IShaderResourceDelegate>& uniformDelegate = nullptr);
		void GetPkts(IteratorRange<Techniques::DrawablesPacket**> result, TechniqueSequenceParseId parse);

	private:
		std::vector<Techniques::DrawablesPacket> _drawablePkt;
		std::vector<bool> _drawablePktsReserved;
		std::unique_ptr<LightingTechniqueStepper> _stepper;
		enum class Phase { SequenceSetup, SceneParse, Execute };
		Phase _currentPhase = Phase::SequenceSetup;
		void ResetIteration(Phase newPhase);
		std::vector<Techniques::IShaderResourceDelegate*> _delegatesPendingUnbind;
		std::vector<LightingTechniqueSequence*> _sequences;

		void GetOrAllocatePkts(IteratorRange<Techniques::DrawablesPacket**> result, TechniqueSequenceParseId parse, Techniques::BatchFlags::BitField batches);

		LightingTechniqueIterator(
			Techniques::ParsingContext& parsingContext,
			IteratorRange<LightingTechniqueSequence** const> sequences);
		friend class LightingTechniqueInstance;
	};

	struct FrameToFrameProperties
	{
		unsigned _frameIdx = 0;
		Techniques::ProjectionDesc _prevProjDesc;
		bool _hasPrevProjDesc = false;
	};

	enum class StepType { ParseScene, MultiViewParseScene, DrawSky, ReadyInstances, None, Abort };

	class LightingTechniqueInstance
	{
	public:
		struct Step
		{
			StepType _type = StepType::Abort;
			Techniques::ParsingContext* _parsingContext = nullptr;
			std::vector<Techniques::DrawablesPacket*> _pkts;			// todo -- candidate for subframe heap
			XLEMath::ArbitraryConvexVolumeTester* _complexCullingVolume = nullptr;
			std::vector<Techniques::ProjectionDesc> _multiViewDesc;		// todo -- candidate for subframe heap

			operator bool() const { return _type != StepType::None && _type != StepType::Abort; }
		};
		Step GetNextStep();

		LightingTechniqueInstance(
			Techniques::ParsingContext&,
			IteratorRange<LightingTechniqueSequence** const> sequences,
			FrameToFrameProperties& frameToFrameProps);
		~LightingTechniqueInstance();

		// For ensuring that required resources are prepared/loaded
		void FulfillWhenNotPending(std::promise<Techniques::PreparedResourcesVisibility>&& promise);
		void AddRequiredCommandList(BufferUploads::CommandListID);
		LightingTechniqueInstance(
			Techniques::IPipelineAcceleratorPool& pipelineAccelerators,
			IteratorRange<LightingTechniqueSequence** const> sequences);

		LightingTechniqueInstance(LightingTechniqueInstance&&) = default;
		LightingTechniqueInstance& operator=(LightingTechniqueInstance&&) = default;
	private:
		std::unique_ptr<LightingTechniqueIterator> _iterator;
		FrameToFrameProperties* _frameToFrameProps = nullptr;

		class PrepareResourcesIterator;
		std::unique_ptr<PrepareResourcesIterator> _prepareResourcesIterator;
		Step GetNextPrepareResourcesStep();
		void CleanupPostIteration();
	};

}}
