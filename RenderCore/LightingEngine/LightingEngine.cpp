// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "LightingEngine.h"
#include "Sequence.h"
#include "SequenceIterator.h"
#include "RenderStepFragments.h"
#include "LightingEngineApparatus.h"
#include "ForwardLightingDelegate.h"		// for construction
#include "DeferredLightingDelegate.h"		// for construction
#include "UtilityLightingDelegate.h"		// for construction
#include "../Techniques/RenderPass.h"
#include "../Techniques/PipelineAccelerator.h"
#include "../Techniques/Techniques.h"
#include "../Techniques/ParsingContext.h"
#include "../Techniques/DrawableDelegates.h"
#include "../FrameBufferDesc.h"
#include "../../Assets/Continuation.h"
#include "../../Assets/ContinuationUtil.h"
#include "../../Assets/AsyncMarkerGroup.h"
#include "../../OSServices/Log.h"

namespace RenderCore { namespace LightingEngine
{
	void Sequence::CreateStep_CallFunction(std::function<StepFnSig>&& fn)
	{
		assert(!_frozen);
		ExecuteStep newStep;
		newStep._type = ExecuteStep::Type::CallFunction;
		newStep._function = std::move(fn);

		if (_pendingCreateFragmentSteps.empty()) {
			_steps.emplace_back(std::move(newStep));
		} else {
			_pendingCreateFragmentSteps.emplace_back(std::move(newStep));
		}
	}

	auto Sequence::CreateParseScene(Techniques::BatchFlags::BitField batches) -> SequenceParseId
	{
		assert(!_frozen);
		for (auto& s:_parseSteps)
			if (!s._complexCullingVolume && s._multiViewProjections.empty()) {
				s._prepareOnly = false;
				s._batches |= batches;
				return s._parseId | (batches << 16u);
			}
		ParseStep newStep;
		newStep._batches = batches;
		newStep._parseId = _nextParseId++;
		_parseSteps.emplace_back(std::move(newStep));
		assert((newStep._parseId & 0xffff) == newStep._parseId);
		return newStep._parseId | (batches << 16u);
	}

	auto Sequence::CreateParseScene(Techniques::BatchFlags::BitField batches, std::shared_ptr<XLEMath::ArbitraryConvexVolumeTester> complexCullingVolume) -> SequenceParseId
	{
		assert(!_frozen);
		for (auto& s:_parseSteps)
			if (s._complexCullingVolume == complexCullingVolume && s._multiViewProjections.empty()) {
				s._prepareOnly = false;
				s._batches |= batches;
				return s._parseId | (batches << 16u);
			}
		ParseStep newStep;
		newStep._batches = batches;
		newStep._parseId = _nextParseId++;
		newStep._complexCullingVolume = std::move(complexCullingVolume);
		_parseSteps.emplace_back(std::move(newStep));
		assert((newStep._parseId & 0xffff) == newStep._parseId);
		return newStep._parseId | (batches << 16u);
	}

	auto Sequence::CreateMultiViewParseScene(
		Techniques::BatchFlags::BitField batches,
		std::vector<Techniques::ProjectionDesc>&& projDescs,
		std::shared_ptr<XLEMath::ArbitraryConvexVolumeTester> complexCullingVolume) -> SequenceParseId
	{
		assert(!_frozen);
		// Don't bother trying to combine this with another parse step in this case -- since it's unlikely
		// we'll find one with exactly the same views
		ParseStep newStep;
		newStep._batches = batches;
		newStep._parseId = _nextParseId++;
		newStep._complexCullingVolume = std::move(complexCullingVolume);
		newStep._multiViewProjections = std::move(projDescs);
		_parseSteps.emplace_back(std::move(newStep));
		assert((newStep._parseId & 0xffff) == newStep._parseId);
		return newStep._parseId | (batches << 16u);
	}

	auto Sequence::CreatePrepareOnlyParseScene(Techniques::BatchFlags::BitField batches) -> SequenceParseId
	{
		assert(!_frozen);
		for (auto& s:_parseSteps)
			if (!s._complexCullingVolume) {
				s._batches |= batches;
				return s._parseId | (batches << 16u);
			}
		ParseStep newStep;
		newStep._batches = batches;
		newStep._parseId = _nextParseId++;
		newStep._prepareOnly = true;
		_parseSteps.emplace_back(std::move(newStep));
		assert((newStep._parseId & 0xffff) == newStep._parseId);
		return newStep._parseId | (batches << 16u);
	}

	void Sequence::CreateStep_ExecuteDrawables(
		std::shared_ptr<Techniques::SequencerConfig> sequencerConfig,
		std::shared_ptr<Techniques::IShaderResourceDelegate> uniformDelegate,
		SequenceParseId parseId)
	{
		assert(!_frozen);
		ExecuteStep newStep;
		newStep._type = ExecuteStep::Type::PrepareOnly_ExecuteDrawables;
		newStep._sequencerConfig = std::move(sequencerConfig);
		newStep._shaderResourceDelegate = std::move(uniformDelegate);
		newStep._fbDescIdx = parseId;

		if (_pendingCreateFragmentSteps.empty()) {
			_steps.emplace_back(std::move(newStep));
		} else {
			_pendingCreateFragmentSteps.emplace_back(std::move(newStep));
		}
	}

	void Sequence::CreatePrepareOnlyStep_ExecuteDrawables(std::shared_ptr<Techniques::SequencerConfig> sequencerConfig, SequenceParseId parseId)
	{
		assert(!_frozen);
		ExecuteStep newStep;
		newStep._type = ExecuteStep::Type::PrepareOnly_ExecuteDrawables;
		newStep._sequencerConfig = std::move(sequencerConfig);
		newStep._fbDescIdx = parseId;

		if (_pendingCreateFragmentSteps.empty()) {
			_steps.emplace_back(std::move(newStep));
		} else {
			_pendingCreateFragmentSteps.emplace_back(std::move(newStep));
		}
	}

	auto Sequence::CreateStep_RunFragments(RenderStepFragmentInterface&& fragments) -> FragmentInterfaceRegistration
	{
		assert(!_frozen);
		if (!_pendingCreateFragmentSteps.empty() && std::get<PendingCreateFragmentPair>(_pendingCreateFragmentSteps[0]).first.GetPipelineType() != fragments.GetPipelineType())
			ResolvePendingCreateFragmentSteps();
		_pendingCreateFragmentSteps.emplace_back(std::make_pair(std::move(fragments), _nextFragmentInterfaceRegistration));
		return _nextFragmentInterfaceRegistration++;
	}

	void Sequence::CreateStep_BindDelegate(std::shared_ptr<Techniques::IShaderResourceDelegate> uniformDelegate)
	{
		assert(!_frozen);
		ExecuteStep newStep;
		newStep._type = ExecuteStep::Type::BindDelegate;
		newStep._shaderResourceDelegate = std::move(uniformDelegate);
		
		if (_pendingCreateFragmentSteps.empty()) {
			_steps.emplace_back(std::move(newStep));
		} else {
			_pendingCreateFragmentSteps.emplace_back(std::move(newStep));
		}
	}

	void Sequence::CreateStep_InvalidateUniforms()
	{
		assert(!_frozen);
		ExecuteStep newStep;
		newStep._type = ExecuteStep::Type::InvalidateUniforms;
		
		if (_pendingCreateFragmentSteps.empty()) {
			_steps.emplace_back(std::move(newStep));
		} else {
			_pendingCreateFragmentSteps.emplace_back(std::move(newStep));
		}
	}

	void Sequence::CreateStep_BringUpToDateUniforms()
	{
		assert(!_frozen);
		ExecuteStep newStep;
		newStep._type = ExecuteStep::Type::BringUpToDateUniforms;
		
		if (_pendingCreateFragmentSteps.empty()) {
			_steps.emplace_back(std::move(newStep));
		} else {
			_pendingCreateFragmentSteps.emplace_back(std::move(newStep));
		}
	}

	void Sequence::ForceRetainAttachment(uint64_t semantic, BindFlag::BitField layout)
	{
		assert(!_frozen);
		_forceRetainSemantics.emplace_back(semantic, layout);
	}

	static const std::string s_defaultSequencerCfgName = "lighting-technique";

	void Sequence::ResolvePendingCreateFragmentSteps()
	{
		assert(!_frozen);
		if (_pendingCreateFragmentSteps.empty()) return;

		{
			std::vector<Techniques::FrameBufferDescFragment> fragments;
			for (auto& pendingStep:_pendingCreateFragmentSteps)
				if (pendingStep.index() == 0)
					fragments.emplace_back(Techniques::FrameBufferDescFragment{std::get<PendingCreateFragmentPair>(pendingStep).first.GetFrameBufferDescFragment()});
			_fbDescsPendingStitch.emplace_back(std::move(fragments));
		}

		// Generate commands for walking through the render pass
		ExecuteStep beginStep;
		beginStep._type = ExecuteStep::Type::BeginRenderPassInstance;
		beginStep._fbDescIdx = (unsigned)_fbDescsPendingStitch.size()-1;
		_steps.emplace_back(std::move(beginStep));
		
		unsigned stepCounter = 0;
		for (auto& pendingStep:_pendingCreateFragmentSteps) {

			if (pendingStep.index() == 0) {
				auto& fragmentStep = std::get<PendingCreateFragmentPair>(pendingStep);

				assert(_fragmentInterfaceMappings.size() == fragmentStep.second);
				_fragmentInterfaceMappings.push_back({beginStep._fbDescIdx, stepCounter});

				assert(!fragmentStep.first.GetSubpassAddendums().empty());
				for (unsigned c=0; c<fragmentStep.first.GetSubpassAddendums().size(); ++c) {
					if (stepCounter != 0) _steps.push_back({ExecuteStep::Type::NextRenderPassStep});
					auto& sb = fragmentStep.first.GetSubpassAddendums()[c];

					using SubpassExtension = RenderStepFragmentInterface::SubpassExtension;
					if (sb._type == SubpassExtension::Type::ExecuteDrawables) {
						assert(sb._techniqueDelegate);

						ExecuteStep drawStep;
						drawStep._type = ExecuteStep::Type::ExecuteDrawables;
						#if defined(_DEBUG)
							auto name = fragmentStep.first.GetFrameBufferDescFragment().GetSubpasses()[c]._name;
							if (name.empty()) name = s_defaultSequencerCfgName;
						#else
							auto name = s_defaultSequencerCfgName;
						#endif
						drawStep._fbDescIdx = CreateParseScene(sb._batchFilter);
						_sequencerConfigsPendingConstruction.push_back(
							SequencerConfigPendingConstruction {
								(unsigned)_steps.size(),
								name, sb._techniqueDelegate, sb._sequencerSelectors,
								beginStep._fbDescIdx, stepCounter });
						drawStep._shaderResourceDelegate = sb._shaderResourceDelegate;
						_steps.emplace_back(std::move(drawStep));
					} else if (sb._type == SubpassExtension::Type::ExecuteSky) {
						_steps.push_back({ExecuteStep::Type::DrawSky});
					} else if (sb._type == SubpassExtension::Type::CallLightingIteratorFunction) {
						ExecuteStep newStep;
						newStep._type = ExecuteStep::Type::CallFunction;
						newStep._function = std::move(sb._lightingIteratorFunction);
						_steps.emplace_back(std::move(newStep));
					} else {
						assert(sb._type == SubpassExtension::Type::HandledByPrevious);
					}

					++stepCounter;
				}
			} else {
				_steps.emplace_back(std::get<ExecuteStep>(std::move(pendingStep)));
			}
		}

		ExecuteStep endStep;
		endStep._type = ExecuteStep::Type::EndRenderPassInstance;
		_steps.push_back(std::move(endStep));

		_pendingCreateFragmentSteps.clear();
	}

	void Sequence::CompleteAndSeal(
		Techniques::IPipelineAcceleratorPool& pipelineAccelerators,
		Techniques::FragmentStitchingContext& stitchingContext,
		const FrameBufferProperties& fbProps)
	{
		if (_dynamicFn) return;

		// complete all frame buffers in _fbDescsPendingStitch & fill in the sequencer configs pointed to by _sequencerConfigsPendingConstruction
		ResolvePendingCreateFragmentSteps();
		_frozen = true;
		PropagateReverseAttachmentDependencies(stitchingContext);

		assert(_fbDescs.empty());
		_fbDescs.reserve(_fbDescsPendingStitch.size());
		for (const auto& stitchOp:_fbDescsPendingStitch) {
			auto mergedFB = stitchingContext.TryStitchFrameBufferDesc(MakeIteratorRange(stitchOp), fbProps);

			#if defined(_DEBUG)
				Log(Warning) << "Merged fragment in lighting technique:" << std::endl << mergedFB._log << std::endl;
			#endif

			stitchingContext.UpdateAttachments(mergedFB);
			_fbDescs.emplace_back(std::move(mergedFB));
		}
		_fbDescsPendingStitch.clear();

		for (const auto& createSequencerConfig:_sequencerConfigsPendingConstruction) {
			auto seqCfg = pipelineAccelerators.CreateSequencerConfig(createSequencerConfig._name,createSequencerConfig._sequencerSelectors);
			pipelineAccelerators.SetTechniqueDelegate(*seqCfg, createSequencerConfig._delegate);
			pipelineAccelerators.SetFrameBufferDesc(*seqCfg, _fbDescs[createSequencerConfig._fbDescIndex]._fbDesc, createSequencerConfig._subpassIndex);
			assert(createSequencerConfig._stepIndex < _steps.size());
			assert(_steps[createSequencerConfig._stepIndex]._type == ExecuteStep::Type::ExecuteDrawables);
			assert(_steps[createSequencerConfig._stepIndex]._sequencerConfig == nullptr);
			_steps[createSequencerConfig._stepIndex]._sequencerConfig = std::move(seqCfg);
		}
		_sequencerConfigsPendingConstruction.clear();
	}

	void Sequence::PropagateReverseAttachmentDependencies(Techniques::FragmentStitchingContext& stitchingContext)
	{
		// For each input attachment in later fragments, search backwards 
		// another fragment that produces/writes to that attachment. Ensure that
		// the store state is correct to match the required load state
		// This will sometimes flip a "discard" state into a "store" state (for example)
		std::vector<Techniques::FrameBufferDescFragment*> frags;
		frags.reserve(_fbDescsPendingStitch.size()*2);
		for (auto& part:_fbDescsPendingStitch) for (auto& f:part) frags.push_back(&f);

		for (auto readingFrag=frags.rbegin(); readingFrag!=frags.rend(); ++readingFrag) {
			for (const auto& a:(*readingFrag)->GetAttachments()) {
				auto [mainLoad, stencilLoad] = SplitAspects(a._loadFromPreviousPhase);
				if (mainLoad != LoadStore::Retain && stencilLoad != LoadStore::Retain) continue;
				
				// Find the first fragment before this that used this attachemnt
				for (auto preparingFrag=readingFrag+1; preparingFrag!=frags.rend(); ++preparingFrag) {
					auto i = std::find_if((*preparingFrag)->GetAttachments().begin(), (*preparingFrag)->GetAttachments().end(),
						[semantic=a._semantic](const auto& q) { return q._semantic == semantic; });
					if (i != (*preparingFrag)->GetAttachments().end()) {
						auto [mainStore, stencilStore] = SplitAspects(i->_storeToNextPhase);
						if (	(mainLoad == LoadStore::Retain && mainStore != LoadStore::Retain)
							||	(stencilLoad == LoadStore::Retain && stencilStore != LoadStore::Retain)) {

							mainStore = (mainLoad == LoadStore::Retain)?LoadStore::Retain:mainStore;
							stencilStore = (stencilLoad == LoadStore::Retain)?LoadStore::Retain:stencilStore;

							Log(Warning) << "Changed store operation in PropagateReverseAttachmentDependencies" << std::endl;
							i->_storeToNextPhase = CombineAspects(mainStore, stencilStore);
						}
						break;
					}
				}
			}
		}

		for (auto forceRetain:_forceRetainSemantics) {
			for (auto preparingFrag=frags.rbegin(); preparingFrag!=frags.rend(); ++preparingFrag) {
				auto i = std::find_if((*preparingFrag)->GetAttachments().begin(), (*preparingFrag)->GetAttachments().end(),
					[forceRetain](const auto& q) { return q._semantic == forceRetain.first; });
				if (i != (*preparingFrag)->GetAttachments().end()) {
					if (i->_storeToNextPhase != LoadStore::Retain) {
						i->_storeToNextPhase = LoadStore::Retain;
						Log(Warning) << "Changed store operation due to force retain in PropagateReverseAttachmentDependencies" << std::endl;
					}
					i->_finalLayout = forceRetain.second;
					break;
				}
			}
		}

		// ensure that the double buffer attachments end up in the layout we're expecting for the next frame
		for (const auto& doubleBuffer:stitchingContext.GetDoubleBufferAttachments()) {
			for (auto preparingFrag=frags.rbegin(); preparingFrag!=frags.rend(); ++preparingFrag) {
				auto i = std::find_if((*preparingFrag)->GetAttachments().begin(), (*preparingFrag)->GetAttachments().end(),
					[sem=doubleBuffer._yesterdaySemantic](const auto& q) { return q._semantic == sem; });
				if (i != (*preparingFrag)->GetAttachments().end()) {
					if (i->_storeToNextPhase != LoadStore::Retain) {
						i->_storeToNextPhase = LoadStore::Retain;
						Log(Warning) << "Changed store operation due to force retain in PropagateReverseAttachmentDependencies" << std::endl;
					}
					i->_finalLayout = doubleBuffer._initialLayout;
					break;
				}
			}
		}

		#if defined(_DEBUG)
			for (const auto& doubleBuffer:stitchingContext.GetDoubleBufferAttachments()) {
				auto i = std::find_if(_forceRetainSemantics.begin(), _forceRetainSemantics.end(), [sem=doubleBuffer._yesterdaySemantic](const auto& q) { return q.first == sem; });
				if (i != _forceRetainSemantics.end() && i->second != doubleBuffer._yesterdaySemantic) {
					Log(Warning) << "Force retain for attachment (";
					if (auto* dehash = Techniques::AttachmentSemantics::TryDehash(doubleBuffer._yesterdaySemantic))
						Log(Warning) << dehash;
					else
						Log(Warning) << "0x" << std::hex << doubleBuffer._yesterdaySemantic << std::dec;
					Log(Warning) << ") conflicts with double buffer setting. Force retain setting ignored." << std::endl;
				}
			}
		#endif
	}

	void Sequence::Reset()
	{
		_pendingCreateFragmentSteps.clear();
		_steps.clear();
		_parseSteps.clear();
		_fbDescs.clear();
		_fragmentInterfaceMappings.clear();
		_nextFragmentInterfaceRegistration = 0;
		_frozen = false;
		_nextParseId = 0;
	}

	void Sequence::TryDynamicInitialization(SequenceIterator& iterator)
	{
		if (_dynamicFn) {
			Reset();
			_dynamicFn(iterator, *this);
		}
	}

	std::pair<const FrameBufferDesc*, unsigned> Sequence::GetResolvedFrameBufferDesc(FragmentInterfaceRegistration regId) const
	{
		assert(_frozen);
		assert(regId < _fragmentInterfaceMappings.size());
		return std::make_pair(
			&_fbDescs[_fragmentInterfaceMappings[regId]._fbDesc]._fbDesc,
			_fragmentInterfaceMappings[regId]._subpassBegin);
	}

	Sequence::Sequence()
	{}

	Sequence::Sequence(DynamicSequenceFn&& dynFn)
	: _dynamicFn(std::move(dynFn)) {}

	Sequence::~Sequence()
	{}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////

	static const unsigned s_drawablePktsPerParse = (unsigned)Techniques::Batch::Max;

	void CompiledLightingTechnique::CompleteConstruction(
		std::shared_ptr<Techniques::IPipelineAcceleratorPool> pipelineAccelerators,
		Techniques::FragmentStitchingContext& stitchingContext,
		const FrameBufferProperties& fbProps)
	{
		assert(!_isConstructionCompleted);
		_doubleBufferAttachments = { stitchingContext.GetDoubleBufferAttachments().begin(), stitchingContext.GetDoubleBufferAttachments().end() };
		for (auto&s:_sequences)
			s->CompleteAndSeal(*pipelineAccelerators, stitchingContext, fbProps);
		_isConstructionCompleted = true;
	}

	Sequence& CompiledLightingTechnique::CreateSequence()
	{
		auto newSequence = std::make_shared<Sequence>();
		_sequences.push_back(std::move(newSequence));
		return *_sequences.back();
	}

	void CompiledLightingTechnique::CreateDynamicSequence(Sequence::DynamicSequenceFn&& fn)
	{
		auto newSequence = std::make_shared<Sequence>(std::move(fn));
		_sequences.emplace_back(std::move(newSequence));
	}

	ILightScene& CompiledLightingTechnique::GetLightScene()
	{
		return *_lightScene;
	}

	ILightScene& GetLightScene(CompiledLightingTechnique& technique)
	{
		return technique.GetLightScene();
	}

	const ::Assets::DependencyValidation& GetDependencyValidation(CompiledLightingTechnique& technique)
	{
		return technique.GetDependencyValidation();
	}

	IteratorRange<const Techniques::DoubleBufferAttachment*> GetDoubleBufferAttachments(CompiledLightingTechnique& technique)
	{
		return technique.GetDoubleBufferAttachments();
	}

	namespace Internal
	{
		void* QueryInterface(CompiledLightingTechnique& technique, uint64_t typeCode)
		{
			if (!technique._queryInterfaceHelper) return nullptr;
			return technique._queryInterfaceHelper(typeCode);
		}
	}

	CompiledLightingTechnique::CompiledLightingTechnique(const std::shared_ptr<ILightScene>& lightScene)
	: _lightScene(lightScene)
	{}

	CompiledLightingTechnique::~CompiledLightingTechnique() {}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	class LightingTechniqueStepper
	{
	public:
		std::vector<Sequence::ExecuteStep>::const_iterator _stepIterator;
		std::vector<Sequence::ExecuteStep>::const_iterator _stepEnd;

		std::vector<Sequence::ParseStep>::const_iterator _parseStepIterator;
		std::vector<Sequence::ParseStep>::const_iterator _parseStepEnd;

		std::vector<Sequence*> _remainingSequences;

		template<typename T>
			const Sequence::ExecuteStep* AdvanceExecuteStep(T& iterator);
		template<typename T>
			const Sequence::ParseStep* AdvanceParseStep(T& iterator);

		LightingTechniqueStepper(IteratorRange<Sequence** const> sequences);
		LightingTechniqueStepper() = default;
	};

	template<typename T>
		const Sequence::ExecuteStep* LightingTechniqueStepper::AdvanceExecuteStep(T& iterator)
	{
		if (_remainingSequences.empty()) return nullptr;
		while (_stepIterator == _stepEnd) {
			iterator._drawablePktIdxOffset += s_drawablePktsPerParse*_remainingSequences.front()->DrawablePktsToReserve();
			_remainingSequences.erase(_remainingSequences.begin());
			_stepIterator = _stepEnd = {};
			if (_remainingSequences.empty()) return nullptr;
			_stepIterator = _remainingSequences.front()->_steps.begin();
			_stepEnd = _remainingSequences.front()->_steps.end();
		}
		auto* result = AsPointer(_stepIterator);
		++_stepIterator;
		return result;
	}

	template<typename T>
		const Sequence::ParseStep* LightingTechniqueStepper::AdvanceParseStep(T& iterator)
	{
		if (_remainingSequences.empty()) return nullptr;
		while (_parseStepIterator == _parseStepEnd) {
			iterator._drawablePktIdxOffset += s_drawablePktsPerParse*_remainingSequences.front()->DrawablePktsToReserve();
			_remainingSequences.erase(_remainingSequences.begin());
			_parseStepIterator = _parseStepEnd = {};
			if (_remainingSequences.empty()) return nullptr;
			_parseStepIterator = _remainingSequences.front()->_parseSteps.begin();
			_parseStepEnd = _remainingSequences.front()->_parseSteps.end();
		}
		auto* result = AsPointer(_parseStepIterator);
		++_parseStepIterator;
		return result;
	}

	LightingTechniqueStepper::LightingTechniqueStepper(IteratorRange<Sequence**const> sequences)
	: _remainingSequences { sequences.begin(), sequences.end() }
	{
		if (!_remainingSequences.empty()) {
			_stepIterator = _remainingSequences.front()->_steps.begin();
			_stepEnd = _remainingSequences.front()->_steps.end();
			_parseStepIterator = _remainingSequences.front()->_parseSteps.begin();
			_parseStepEnd = _remainingSequences.front()->_parseSteps.end();
		} else {
			_stepIterator = _stepEnd = {};
			_parseStepIterator = _parseStepEnd = {};
		}
	}

	void SequenceIterator::ExecuteDrawables(
		SequenceParseId parseId,
		Techniques::SequencerConfig& sequencerCfg,
		const std::shared_ptr<Techniques::IShaderResourceDelegate>& uniformDelegate)
	{
		Techniques::DrawablesPacket* pkts[(unsigned)Techniques::Batch::Max];
		GetPkts(MakeIteratorRange(pkts), parseId);
		if (uniformDelegate)
			_parsingContext->GetUniformDelegateManager()->BindShaderResourceDelegate(uniformDelegate);
		for (unsigned c=0; c<(unsigned)Techniques::Batch::Max; ++c) {
			if (!pkts[c] || pkts[c]->_drawables.empty()) continue;
			TRY {
				Techniques::Draw(*_parsingContext, _parsingContext->GetPipelineAccelerators(), sequencerCfg, *pkts[c]);
			} CATCH(...) {
				if (uniformDelegate)
					_parsingContext->GetUniformDelegateManager()->UnbindShaderResourceDelegate(*uniformDelegate);
				throw;
			} CATCH_END
		}
		if (uniformDelegate)
			_parsingContext->GetUniformDelegateManager()->UnbindShaderResourceDelegate(*uniformDelegate);
	}

	void SequenceIterator::GetPkts(IteratorRange<Techniques::DrawablesPacket**> result, SequenceParseId parseId)
	{
		auto realParseId = parseId & 0xffff;
		auto batchFlags = parseId >> 16;
		auto pktIdx = _drawablePktIdxOffset+realParseId*s_drawablePktsPerParse;
		assert(pktIdx < _drawablePkt.size());
		assert(result.size() == s_drawablePktsPerParse);
		for (unsigned c=0; c<s_drawablePktsPerParse; ++c) {
			if (batchFlags & (1u<<c)) {
				assert(_drawablePktsReserved[pktIdx+c]);
				result[c] = _drawablePkt.data()+pktIdx+c;
			} else {
				result[c] = nullptr;
			}
		}
	}

	void SequenceIterator::GetOrAllocatePkts(IteratorRange<Techniques::DrawablesPacket**> result, SequenceParseId parseId, Techniques::BatchFlags::BitField batches)
	{
		auto realParseId = parseId & 0xffff;
		auto pktIdx = _drawablePktIdxOffset+realParseId*s_drawablePktsPerParse;
		if ((pktIdx+s_drawablePktsPerParse) > _drawablePkt.size()) {
			_drawablePkt.resize(pktIdx+s_drawablePktsPerParse);
			_drawablePktsReserved.resize(pktIdx+s_drawablePktsPerParse, false);
		}

		assert(result.size() <= s_drawablePktsPerParse);
		for (unsigned c=0; c<result.size(); ++c) {
			if (batches & (1u<<c)) {
				if (!_drawablePktsReserved[pktIdx+c]) {
					_drawablePkt[pktIdx+c] = _parsingContext->GetTechniqueContext()._drawablesPool->CreatePacket();
					_drawablePktsReserved[pktIdx+c] = true;
				}
				result[c] = _drawablePkt.data()+pktIdx+c;
				assert(result[c]);
			}
		}
	}

	SequenceIterator::SequenceIterator(
		Techniques::ParsingContext& parsingContext,
		FrameToFrameProperties& frameToFrameProps)
	: _threadContext(&parsingContext.GetThreadContext())
	, _parsingContext(&parsingContext)
	, _frameToFrameProps(&frameToFrameProps)
	{}

	auto SequencePlayback::GetNextStep() -> Step
	{
		_begunIteration = true;
		if (!_iterator)
			return GetNextPrepareResourcesStep();

		if (_currentPhase == Phase::SequenceSetup) {
			if (_frameToFrameProps->_hasPrevProjDesc) {
				_iterator->_parsingContext->GetPrevProjectionDesc() = _frameToFrameProps->_prevProjDesc;
				_iterator->_parsingContext->GetEnablePrevProjectionDesc() = true;
			}
			_frameToFrameProps->_prevProjDesc = _iterator->_parsingContext->GetProjectionDesc();
			_frameToFrameProps->_hasPrevProjDesc = true;

			for (auto& sequence:_sequences)
				sequence->TryDynamicInitialization(*_iterator);
			ResetIteration(Phase::SceneParse);
		}

		if (_currentPhase == Phase::SceneParse) {
			const Sequence::ParseStep* next;
			while ((next=_stepper->AdvanceParseStep(*_iterator))) {
				if (!next->_prepareOnly) {
					std::vector<Techniques::DrawablesPacket*> pkts;
					pkts.resize((unsigned)Techniques::Batch::Max);
					_iterator->GetOrAllocatePkts(MakeIteratorRange(pkts), next->_parseId, next->_batches);
					if (next->_multiViewProjections.empty()) {
						return { StepType::ParseScene, _iterator->_parsingContext, std::move(pkts), next->_complexCullingVolume.get() };
					} else {
						return { StepType::MultiViewParseScene, _iterator->_parsingContext, std::move(pkts), next->_complexCullingVolume.get(), next->_multiViewProjections };
					}
				}
			}

			ResetIteration(Phase::Execute);
			return { StepType::ReadyInstances };
		}

		const Sequence::ExecuteStep* next;
		while ((next=_stepper->AdvanceExecuteStep(*_iterator))) {
			switch (next->_type) {
			case Sequence::ExecuteStep::Type::CallFunction:
				TRY {
					next->_function(*_iterator);
				} CATCH(const std::exception& e) {
					StringMeldAppend(_iterator->_parsingContext->_stringHelpers->_errorString) << e.what() << "\n";
				} CATCH_END
				break;

			case Sequence::ExecuteStep::Type::ExecuteDrawables:
				_iterator->ExecuteDrawables(next->_fbDescIdx, *next->_sequencerConfig, next->_shaderResourceDelegate);
				break;

			case Sequence::ExecuteStep::Type::DrawSky:
				{
					Step result;
					result._type = StepType::DrawSky;
					result._parsingContext = _iterator->_parsingContext;
					return result;
				}

			case Sequence::ExecuteStep::Type::BeginRenderPassInstance:
				{
					assert(next->_fbDescIdx < _stepper->_remainingSequences.front()->_fbDescs.size());
					Techniques::RenderPassBeginDesc beginDesc;
					beginDesc._frameIdx = _frameToFrameProps->_frameIdx;
					_iterator->_rpi = Techniques::RenderPassInstance{
						*_iterator->_parsingContext,
						_stepper->_remainingSequences.front()->_fbDescs[next->_fbDescIdx],
						beginDesc};
				}
				break;

			case Sequence::ExecuteStep::Type::EndRenderPassInstance:
				_iterator->_rpi.End();
				_iterator->_rpi = {};
				break;

			case Sequence::ExecuteStep::Type::NextRenderPassStep:
				_iterator->_rpi.NextSubpass();
				break;

			case Sequence::ExecuteStep::Type::PrepareOnly_ExecuteDrawables:
				break;

			case Sequence::ExecuteStep::Type::BindDelegate:
				_iterator->_parsingContext->GetUniformDelegateManager()->BindShaderResourceDelegate(next->_shaderResourceDelegate);
				_iterator->_delegatesPendingUnbind.push_back(next->_shaderResourceDelegate.get());
				break;

			case Sequence::ExecuteStep::Type::InvalidateUniforms:
				_iterator->_parsingContext->GetUniformDelegateManager()->InvalidateUniforms();
				break;

			case Sequence::ExecuteStep::Type::BringUpToDateUniforms:
				_iterator->_parsingContext->GetUniformDelegateManager()->BringUpToDateGraphics(*_iterator->_parsingContext);
				_iterator->_parsingContext->GetUniformDelegateManager()->BringUpToDateCompute(*_iterator->_parsingContext);
				break;

			case Sequence::ExecuteStep::Type::None:
				UNREACHABLE();
				break;
			}
		}

		CleanupPostIteration();
		return Step { StepType::None };
	}

	void SequencePlayback::CleanupPostIteration()
	{
		// release all drawables now we're complete
		for (auto& pkt:_iterator->_drawablePkt)
			pkt.Reset();

		auto& delegateMan = *_iterator->_parsingContext->GetUniformDelegateManager();
		for (auto delegate:_iterator->_delegatesPendingUnbind)
			delegateMan.UnbindShaderResourceDelegate(*delegate);
		_iterator->_delegatesPendingUnbind.clear();
	}

	void SequencePlayback::QueueSequence(Sequence& sequence)
	{
		assert(!_begunIteration);
		_sequences.push_back(&sequence);
	}

	SequencePlayback::SequencePlayback(
		Techniques::ParsingContext& parsingContext,
		FrameToFrameProperties& frameToFrameProps)
	{
		_stepper = std::make_unique<LightingTechniqueStepper>();
		_currentPhase = Phase::SequenceSetup;

		_iterator = std::make_unique<SequenceIterator>(parsingContext, frameToFrameProps);
		_frameToFrameProps = &frameToFrameProps;
	}

	SequencePlayback::~SequencePlayback() 
	{
		if (_iterator) {
			// in case of exception, ensure that we've cleaned up everything from the iteration
			CleanupPostIteration();
			++_frameToFrameProps->_frameIdx;
		}
	}

	SequencePlayback::SequencePlayback(SequencePlayback&&) = default;
	SequencePlayback& SequencePlayback::operator=(SequencePlayback&&) = default;

	class SequencePlayback::PrepareResourcesIterator
	{
	public:
		std::vector<Techniques::DrawablesPacket> _drawablePkt;
		std::vector<std::future<Techniques::PreparedResourcesVisibility>> _requiredResources;

		Techniques::IPipelineAcceleratorPool* _pipelineAcceleratorPool = nullptr;
		unsigned _drawablePktIdxOffset = 0;

		BufferUploads::CommandListID _baseCommandList = 0;

		void GetOrAllocatePkts(IteratorRange<Techniques::DrawablesPacket**> result, SequenceParseId parseId, Techniques::BatchFlags::BitField batches)
		{
			auto realParseId = parseId & 0xffff;
			auto pktIdx = _drawablePktIdxOffset+realParseId*s_drawablePktsPerParse;
			if ((pktIdx+s_drawablePktsPerParse) > _drawablePkt.size())
				_drawablePkt.resize(pktIdx+s_drawablePktsPerParse);

			assert(result.size() <= s_drawablePktsPerParse);
			for (unsigned c=0; c<result.size(); ++c) {
				if (batches & (1u<<c)) {
					result[c] = _drawablePkt.data()+pktIdx+c;
					assert(result[c]);
				}
			}
		}
	};

	auto SequencePlayback::GetNextPrepareResourcesStep() -> Step
	{
		assert(_prepareResourcesIterator);
		if (_currentPhase == Phase::SequenceSetup) {
			// We can't initialize the dynamic sequences because we don't have a SequenceIterator
			ResetIteration(Phase::SceneParse);
		}

		if (_currentPhase == Phase::SceneParse) {
			const Sequence::ParseStep* next;
			while ((next=_stepper->AdvanceParseStep(*_prepareResourcesIterator))) {
				assert(next->_parseId != ~0u);
				std::vector<Techniques::DrawablesPacket*> pkts;
				pkts.resize((unsigned)Techniques::Batch::Max);
				_prepareResourcesIterator->GetOrAllocatePkts(MakeIteratorRange(pkts), next->_parseId, next->_batches);
				if (next->_multiViewProjections.empty()) {
					return { StepType::ParseScene, nullptr, std::move(pkts), next->_complexCullingVolume.get() };
				} else {
					return { StepType::MultiViewParseScene, nullptr, std::move(pkts), next->_complexCullingVolume.get(), next->_multiViewProjections };
				}
			}

			ResetIteration(Phase::Execute);
		}

		const Sequence::ExecuteStep* next;
		while ((next=_stepper->AdvanceExecuteStep(*_prepareResourcesIterator))) {
			switch (next->_type) {
			case Sequence::ExecuteStep::Type::DrawSky:
				return { StepType::DrawSky };

			case Sequence::ExecuteStep::Type::PrepareOnly_ExecuteDrawables:
			case Sequence::ExecuteStep::Type::ExecuteDrawables:
				{
					auto pktIdx = _prepareResourcesIterator->_drawablePktIdxOffset+(next->_fbDescIdx&0xffff);
					assert(pktIdx < _prepareResourcesIterator->_drawablePkt.size());
					std::promise<Techniques::PreparedResourcesVisibility> promise;
					auto future = promise.get_future();
					Techniques::PrepareResources(std::move(promise), *_prepareResourcesIterator->_pipelineAcceleratorPool, *next->_sequencerConfig, _prepareResourcesIterator->_drawablePkt[pktIdx]);
					_prepareResourcesIterator->_requiredResources.emplace_back(std::move(future));
				}
				break;

			case Sequence::ExecuteStep::Type::CallFunction:
			case Sequence::ExecuteStep::Type::BeginRenderPassInstance:
			case Sequence::ExecuteStep::Type::EndRenderPassInstance:
			case Sequence::ExecuteStep::Type::NextRenderPassStep:
			case Sequence::ExecuteStep::Type::BindDelegate:
			case Sequence::ExecuteStep::Type::InvalidateUniforms:
			case Sequence::ExecuteStep::Type::BringUpToDateUniforms:
				break;

			case Sequence::ExecuteStep::Type::None:
				UNREACHABLE();
				break;
			}
		}

		for (auto& pkt:_prepareResourcesIterator->_drawablePkt)
			pkt.Reset();

		return Step { StepType::None };
	}

	void SequencePlayback::FulfillWhenNotPending(std::promise<Techniques::PreparedResourcesVisibility>&& promise)
	{
		if (!_prepareResourcesIterator || _prepareResourcesIterator->_requiredResources.empty()) return;
		
		TRY {
			struct Futures
			{
				std::vector<std::future<Techniques::PreparedResourcesVisibility>> _pendingFutures;
				std::vector<std::future<Techniques::PreparedResourcesVisibility>> _readyFutures;
				Techniques::PreparedResourcesVisibility _starterVisibility;
			};
			auto futures = std::make_shared<Futures>();
			futures->_starterVisibility._bufferUploadsVisibility = _prepareResourcesIterator->_baseCommandList;

			for (auto& c:_prepareResourcesIterator->_requiredResources) {
				auto status = c.wait_for(std::chrono::milliseconds(0));
				if (status == std::future_status::timeout) {
					futures->_pendingFutures.emplace_back(std::move(c));
				} else {
					auto result = c.get();
					futures->_starterVisibility._pipelineAcceleratorsVisibility = std::max(futures->_starterVisibility._pipelineAcceleratorsVisibility, result._pipelineAcceleratorsVisibility);
					futures->_starterVisibility._bufferUploadsVisibility = std::max(futures->_starterVisibility._bufferUploadsVisibility, result._bufferUploadsVisibility);
				}
			}
			_prepareResourcesIterator->_requiredResources.clear();	// have to clear, can only query the futures once
			
			if (futures->_pendingFutures.empty()) {
				promise.set_value(futures->_starterVisibility);
			} else {
				::Assets::PollToPromise(
					std::move(promise),
					[futures](auto timeout) {
						auto timeoutTime = std::chrono::steady_clock::now() + timeout;
						while (!futures->_pendingFutures.empty()) {
							if ((futures->_pendingFutures.end()-1)->wait_until(timeoutTime) == std::future_status::timeout)
								return ::Assets::PollStatus::Continue;
							futures->_readyFutures.emplace_back(std::move(*(futures->_pendingFutures.end()-1)));
							futures->_pendingFutures.erase(futures->_pendingFutures.end()-1);
						}
						return ::Assets::PollStatus::Finish;
					},
					[futures]() {
						assert(futures->_pendingFutures.empty());
						auto result = futures->_starterVisibility;
						for (auto& f:futures->_readyFutures) {
							auto p = f.get();
							result._pipelineAcceleratorsVisibility = std::max(result._pipelineAcceleratorsVisibility, p._pipelineAcceleratorsVisibility);
							result._bufferUploadsVisibility = std::max(result._bufferUploadsVisibility, p._bufferUploadsVisibility);
						}
						return result;
					});
			}
		} CATCH(...) {
			promise.set_exception(std::current_exception());
		} CATCH_END
	}

	void SequencePlayback::ResetIteration(Phase newPhase)
	{
		*_stepper = LightingTechniqueStepper{MakeIteratorRange(_sequences)};
		if (_iterator) _iterator->_drawablePktIdxOffset = 0;
		if (_prepareResourcesIterator) _prepareResourcesIterator->_drawablePktIdxOffset = 0;
		_currentPhase = newPhase;
	}

	void SequencePlayback::AddRequiredCommandList(BufferUploads::CommandListID cmdListId)
	{
		_prepareResourcesIterator->_baseCommandList = std::max(_prepareResourcesIterator->_baseCommandList, cmdListId);
	}

	SequencePlayback::SequencePlayback(
		Techniques::IPipelineAcceleratorPool& pipelineAccelerators)
	{
		_stepper = std::make_unique<LightingTechniqueStepper>();
		_currentPhase = Phase::SequenceSetup;

		_prepareResourcesIterator = std::make_unique<PrepareResourcesIterator>();
		_prepareResourcesIterator->_pipelineAcceleratorPool = &pipelineAccelerators;
	}

	SequencePlayback BeginLightingTechniquePlayback(
		Techniques::ParsingContext& parsingContext,
		CompiledLightingTechnique& technique)
	{
		// If you hit this, it probably means that there's a missing call to CompiledLightingTechnique::CompleteConstruction()
		// (which should have happened at the end of the technique construction process)
		assert(technique._isConstructionCompleted);

		SequencePlayback result { parsingContext, technique._frameToFrameProperties };
		for (auto& c:technique._sequences)
			result.QueueSequence(*c);
		return result;

	}

	[[nodiscard]] SequencePlayback BeginPrepareResourcesInstance(Techniques::IPipelineAcceleratorPool& pipelineAccelerators, CompiledLightingTechnique& technique)
	{
		// If you hit this, it probably means that there's a missing call to CompiledLightingTechnique::CompleteConstruction()
		// (which should have happened at the end of the technique construction process)
		assert(technique._isConstructionCompleted);

		SequencePlayback result { pipelineAccelerators };
		result.AddRequiredCommandList(technique.GetCompletionCommandList());
		for (auto& c:technique._sequences)
			result.QueueSequence(*c);
		return result;
	}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	void CreateLightingTechnique(
		std::promise<std::shared_ptr<CompiledLightingTechnique>>&& promise,
		const std::shared_ptr<Techniques::IPipelineAcceleratorPool>& pipelineAccelerators,
		const std::shared_ptr<Techniques::PipelineCollection>& pipelinePool,
		const std::shared_ptr<SharedTechniqueDelegateBox>& techDelBox,
		IteratorRange<const LightSourceOperatorDesc*> resolveOperators,
		IteratorRange<const ShadowOperatorDesc*> shadowOperators,
		const ChainedOperatorDesc* globalOperators,
		IteratorRange<const Techniques::PreregisteredAttachment*> preregisteredAttachments)
	{
		// Convenience function to select from one of the built-in lighting techniques
		// We'll scan the list of operator descs and decide on a technique type from what we
		// find there
		bool foundForwardTechnique = false, foundDeferredTechnique = false, foundUtility = false;
		auto* op = globalOperators;
		while (op) {
			switch (op->_structureType) {
			case TypeHashCode<ForwardLightingTechniqueDesc>:
				foundForwardTechnique = true;
				break;
			case TypeHashCode<DeferredLightingTechniqueDesc>:
				foundDeferredTechnique = true;
				break;
			case TypeHashCode<UtilityLightingTechniqueDesc>:
				foundUtility = true;
				break;
			}
			op = op->_next;
		}
		if ((unsigned(foundForwardTechnique) + unsigned(foundDeferredTechnique) + unsigned(foundUtility)) > 1)
			Throw(std::runtime_error("Multiple top level lighting technique types found. There can only be one"));

		if (foundDeferredTechnique) {
			CreateDeferredLightingTechnique(
				std::move(promise),
				pipelineAccelerators, pipelinePool, techDelBox,
				resolveOperators, shadowOperators,
				globalOperators,
				preregisteredAttachments);
		} else if (foundUtility) {
			CreateUtilityLightingTechnique(
				std::move(promise),
				pipelineAccelerators, pipelinePool, techDelBox,
				globalOperators,
				preregisteredAttachments);
		} else {
			CreateForwardLightingTechnique(
				std::move(promise),
				pipelineAccelerators, pipelinePool, techDelBox,
				resolveOperators, shadowOperators,
				globalOperators,
				preregisteredAttachments);
		}
	}

	// Simplified construction --
	std::future<std::shared_ptr<CompiledLightingTechnique>> CreateLightingTechnique(
		const std::shared_ptr<LightingEngineApparatus>& apparatus,
		IteratorRange<const LightSourceOperatorDesc*> resolveOperators,
		IteratorRange<const ShadowOperatorDesc*> shadowGenerators,
		const ChainedOperatorDesc* globalOperators,
		IteratorRange<const Techniques::PreregisteredAttachment*> preregisteredAttachments)
	{
		std::promise<std::shared_ptr<CompiledLightingTechnique>> promisedTechnique;
		auto result = promisedTechnique.get_future();
		CreateLightingTechnique(
			std::move(promisedTechnique),
			apparatus->_pipelineAccelerators,
			apparatus->_lightingOperatorCollection,
			apparatus->_sharedDelegates,
			resolveOperators, shadowGenerators, globalOperators,
			preregisteredAttachments);
		return result;
	}

}}
