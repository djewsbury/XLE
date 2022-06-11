// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../Techniques/RenderPass.h"
#include <vector>
#include <memory>

namespace RenderCore { class IThreadContext; }
namespace RenderCore { namespace Techniques { class ParsingContext; class IPipelineAcceleratorPool; class IDeformAcceleratorPool; class DrawablesPacket; class IShaderResourceDelegate; class SequencerConfig; } }
namespace RenderCore { namespace Techniques { namespace BatchFlags { using BitField = unsigned; }}}

namespace RenderCore { namespace LightingEngine
{
	using TechniqueSequenceParseId = unsigned;
	class CompiledLightingTechnique;
	class LightingTechniqueStepper;

	class LightingTechniqueIterator
	{
	public:
		IThreadContext* _threadContext = nullptr;
		Techniques::ParsingContext* _parsingContext = nullptr;
		Techniques::IPipelineAcceleratorPool* _pipelineAcceleratorPool = nullptr;
		Techniques::IDeformAcceleratorPool* _deformAcceleratorPool = nullptr;
		const CompiledLightingTechnique* _compiledTechnique = nullptr;
		Techniques::RenderPassInstance _rpi;

		void ExecuteDrawables(
			TechniqueSequenceParseId parseId,
			Techniques::SequencerConfig& sequencerCfg,
			const std::shared_ptr<Techniques::IShaderResourceDelegate>& uniformDelegate = nullptr);
		void GetPkts(IteratorRange<Techniques::DrawablesPacket**> result, TechniqueSequenceParseId parse);

		LightingTechniqueIterator(
			Techniques::ParsingContext& parsingContext,
			const CompiledLightingTechnique& compiledTechnique);

	private:
		std::vector<Techniques::DrawablesPacket> _drawablePkt;
		std::vector<bool> _drawablePktsReserved;
		std::unique_ptr<LightingTechniqueStepper> _stepper;
		enum class Phase { SequenceSetup, SceneParse, Execute };
		Phase _currentPhase = Phase::SequenceSetup;
		void ResetIteration(Phase newPhase);
		std::vector<Techniques::IShaderResourceDelegate*> _delegatesPendingUnbind;

		void GetOrAllocatePkts(IteratorRange<Techniques::DrawablesPacket**> result, TechniqueSequenceParseId parse, Techniques::BatchFlags::BitField batches);

		friend class LightingTechniqueInstance;
	};
}}
