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
namespace std { template<typename T> class promise; }

namespace RenderCore { namespace LightingEngine
{
	using SequenceParseId = unsigned;
	class CompiledLightingTechnique;
	class LightingTechniqueStepper;
	class Sequence;
	struct FrameToFrameProperties;

	class SequenceIterator
	{
	public:
		IThreadContext* _threadContext = nullptr;
		Techniques::ParsingContext* _parsingContext = nullptr;
		Techniques::RenderPassInstance _rpi;

		void ExecuteDrawables(
			SequenceParseId parseId,
			Techniques::SequencerConfig& sequencerCfg,
			const std::shared_ptr<Techniques::IShaderResourceDelegate>& uniformDelegate = nullptr);
		void GetPkts(IteratorRange<Techniques::DrawablesPacket**> result, SequenceParseId parse);
		const FrameToFrameProperties& GetFrameToFrameProperties() const { return *_frameToFrameProps; }

		SequenceIterator(
			Techniques::ParsingContext& parsingContext,
			FrameToFrameProperties& frameToFrameProps);
	private:
		std::vector<Techniques::DrawablesPacket> _drawablePkt;
		std::vector<bool> _drawablePktsReserved;
		std::vector<Techniques::IShaderResourceDelegate*> _delegatesPendingUnbind;
		unsigned _drawablePktIdxOffset = 0;
		FrameToFrameProperties* _frameToFrameProps = nullptr;

		void GetOrAllocatePkts(IteratorRange<Techniques::DrawablesPacket**> result, SequenceParseId parse, Techniques::BatchFlags::BitField batches);

		friend class SequencePlayback;
		friend class LightingTechniqueStepper;
	};

}}
