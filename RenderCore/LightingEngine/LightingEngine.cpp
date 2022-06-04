// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "LightingEngine.h"
#include "LightingEngineInternal.h"
#include "RenderStepFragments.h"
#include "LightingEngineApparatus.h"
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
	void LightingTechniqueSequence::CreateStep_CallFunction(std::function<StepFnSig>&& fn)
	{
		assert(!_frozen);
		ResolvePendingCreateFragmentSteps();
		ExecuteStep newStep;
		newStep._type = ExecuteStep::Type::CallFunction;
		newStep._function = std::move(fn);
		_steps.emplace_back(std::move(newStep));
	}

	auto LightingTechniqueSequence::CreateParseScene(Techniques::BatchFlags::BitField batches) -> ParseId
	{
		assert(!_frozen);
		for (auto& s:_parseSteps)
			if (!s._complexCullingVolume) {
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

	auto LightingTechniqueSequence::CreateParseScene(Techniques::BatchFlags::BitField batches, std::shared_ptr<XLEMath::ArbitraryConvexVolumeTester> complexCullingVolume) -> ParseId
	{
		assert(!_frozen);
		for (auto& s:_parseSteps)
			if (s._complexCullingVolume == complexCullingVolume) {
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

	auto LightingTechniqueSequence::CreatePrepareOnlyParseScene(Techniques::BatchFlags::BitField batches) -> ParseId
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

	void LightingTechniqueSequence::CreateStep_ExecuteDrawables(
		std::shared_ptr<Techniques::SequencerConfig> sequencerConfig,
		std::shared_ptr<Techniques::IShaderResourceDelegate> uniformDelegate,
		ParseId parseId)
	{
		assert(!_frozen);
		ResolvePendingCreateFragmentSteps();
		ExecuteStep newStep;
		newStep._type = ExecuteStep::Type::PrepareOnly_ExecuteDrawables;
		newStep._sequencerConfig = std::move(sequencerConfig);
		newStep._shaderResourceDelegate = std::move(uniformDelegate);
		newStep._fbDescIdx = parseId;
		_steps.emplace_back(std::move(newStep));
	}

	void LightingTechniqueSequence::CreatePrepareOnlyStep_ExecuteDrawables(std::shared_ptr<Techniques::SequencerConfig> sequencerConfig, ParseId parseId)
	{
		assert(!_frozen);
		ExecuteStep newStep;
		newStep._type = ExecuteStep::Type::PrepareOnly_ExecuteDrawables;
		newStep._sequencerConfig = std::move(sequencerConfig);
		newStep._fbDescIdx = parseId;
		_steps.emplace_back(std::move(newStep));
	}

	auto LightingTechniqueSequence::CreateStep_RunFragments(RenderStepFragmentInterface&& fragments) -> FragmentInterfaceRegistration
	{
		assert(!_frozen);
		if (!_pendingCreateFragmentSteps.empty() && _pendingCreateFragmentSteps[0].first.GetPipelineType() != fragments.GetPipelineType())
			ResolvePendingCreateFragmentSteps();
		_pendingCreateFragmentSteps.emplace_back(std::make_pair(std::move(fragments), _nextFragmentInterfaceRegistration));
		return _nextFragmentInterfaceRegistration++;
	}

	void LightingTechniqueSequence::CreateStep_BindDelegate(std::shared_ptr<Techniques::IShaderResourceDelegate> uniformDelegate)
	{
		assert(!_frozen);
		ExecuteStep newStep;
		newStep._type = ExecuteStep::Type::BindDelegate;
		newStep._shaderResourceDelegate = std::move(uniformDelegate);
		_steps.emplace_back(std::move(newStep));
	}

	void LightingTechniqueSequence::CreateStep_InvalidateUniforms()
	{
		assert(!_frozen);
		ExecuteStep newStep;
		newStep._type = ExecuteStep::Type::InvalidateUniforms;
		_steps.emplace_back(std::move(newStep));
	}

	void LightingTechniqueSequence::ForceRetainAttachment(uint64_t semantic, BindFlag::BitField layout)
	{
		assert(!_frozen);
		_forceRetainSemantics.emplace_back(semantic, layout);
	}

	static const std::string s_defaultSequencerCfgName = "lighting-technique";

	void LightingTechniqueSequence::ResolvePendingCreateFragmentSteps()
	{
		assert(!_frozen);
		if (_pendingCreateFragmentSteps.empty()) return;

		{
			std::vector<Techniques::FrameBufferDescFragment> fragments;
			for (auto& step:_pendingCreateFragmentSteps)
				fragments.emplace_back(Techniques::FrameBufferDescFragment{step.first.GetFrameBufferDescFragment()});
			_fbDescsPendingStitch.emplace_back(std::move(fragments));
		}

		// Generate commands for walking through the render pass
		ExecuteStep beginStep;
		beginStep._type = ExecuteStep::Type::BeginRenderPassInstance;
		beginStep._fbDescIdx = (unsigned)_fbDescsPendingStitch.size()-1;
		_steps.emplace_back(std::move(beginStep));
		
		unsigned stepCounter = 0;
		for (auto& fragmentStep:_pendingCreateFragmentSteps) {
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
							beginStep._fbDescIdx, c });
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
		}

		ExecuteStep endStep;
		endStep._type = ExecuteStep::Type::EndRenderPassInstance;
		_steps.push_back(std::move(endStep));

		_pendingCreateFragmentSteps.clear();
	}

	void LightingTechniqueSequence::CompleteAndSeal(
		Techniques::IPipelineAcceleratorPool& pipelineAccelerators,
		Techniques::FragmentStitchingContext& stitchingContext)
	{
		// complete all frame buffers in _fbDescsPendingStitch & fill in the sequencer configs pointed to by _sequencerConfigsPendingConstruction
		ResolvePendingCreateFragmentSteps();
		_frozen = true;
		PropagateReverseAttachmentDependencies(stitchingContext);

		assert(_fbDescs.empty());
		_fbDescs.reserve(_fbDescsPendingStitch.size());
		for (const auto& stitchOp:_fbDescsPendingStitch) {
			auto mergedFB = stitchingContext.TryStitchFrameBufferDesc(MakeIteratorRange(stitchOp));

			#if defined(_DEBUG)
				Log(Warning) << "Merged fragment in lighting technique:" << std::endl << mergedFB._log << std::endl;
			#endif

			stitchingContext.UpdateAttachments(mergedFB);
			_fbDescs.emplace_back(std::move(mergedFB));
		}
		_fbDescsPendingStitch.clear();

		for (const auto& createSequencerConfig:_sequencerConfigsPendingConstruction) {
			auto seqCfg = pipelineAccelerators.CreateSequencerConfig(
				createSequencerConfig._name,
				createSequencerConfig._delegate, createSequencerConfig._sequencerSelectors, 
				_fbDescs[createSequencerConfig._fbDescIndex]._fbDesc, createSequencerConfig._subpassIndex);
			assert(createSequencerConfig._stepIndex < _steps.size());
			assert(_steps[createSequencerConfig._stepIndex]._type == ExecuteStep::Type::ExecuteDrawables);
			assert(_steps[createSequencerConfig._stepIndex]._sequencerConfig == nullptr);
			_steps[createSequencerConfig._stepIndex]._sequencerConfig = std::move(seqCfg);
		}
		_sequencerConfigsPendingConstruction.clear();
	}

	void LightingTechniqueSequence::PropagateReverseAttachmentDependencies(Techniques::FragmentStitchingContext& stitchingContext)
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
	}

	void LightingTechniqueSequence::Reset()
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

	std::pair<const FrameBufferDesc*, unsigned> LightingTechniqueSequence::GetResolvedFrameBufferDesc(FragmentInterfaceRegistration regId) const
	{
		assert(_frozen);
		assert(regId < _fragmentInterfaceMappings.size());
		return std::make_pair(
			&_fbDescs[_fragmentInterfaceMappings[regId]._fbDesc]._fbDesc,
			_fragmentInterfaceMappings[regId]._subpassBegin);
	}

	LightingTechniqueSequence::LightingTechniqueSequence()
	{}

	LightingTechniqueSequence::~LightingTechniqueSequence()
	{}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////

	static const unsigned s_drawablePktsPerParse = (unsigned)Techniques::Batch::Max;

	void CompiledLightingTechnique::CompleteConstruction(
		std::shared_ptr<Techniques::IPipelineAcceleratorPool> pipelineAccelerators,
		Techniques::FragmentStitchingContext& stitchingContext)
	{
		assert(!_isConstructionCompleted);
		for (auto&s:_sequences)
			if (!s._dynamicFn)
				s._sequence->CompleteAndSeal(*pipelineAccelerators, stitchingContext);
		_isConstructionCompleted = true;
		_pipelineAccelerators = std::move(pipelineAccelerators);
	}

	LightingTechniqueSequence& CompiledLightingTechnique::CreateSequence()
	{
		Sequence newSequence;
		newSequence._sequence = std::make_shared<LightingTechniqueSequence>();
		auto* res = newSequence._sequence.get();
		_sequences.push_back(std::move(newSequence));
		return *res;
	}

	void CompiledLightingTechnique::CreateDynamicSequence(DynamicSequenceFn&& fn)
	{
		auto newSequence = std::make_shared<LightingTechniqueSequence>();
		_sequences.emplace_back(Sequence{std::move(newSequence), std::move(fn)});
	}

	void CompiledLightingTechnique::PreSequenceSetup(std::function<void(LightingTechniqueIterator&)>&& fn)
	{
		assert(!_preSequenceSetup);
		_preSequenceSetup = std::move(fn);
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

	CompiledLightingTechnique::CompiledLightingTechnique(const std::shared_ptr<ILightScene>& lightScene)
	: _lightScene(lightScene)
	{}

	CompiledLightingTechnique::~CompiledLightingTechnique() {}

	const LightingTechniqueSequence::ExecuteStep* LightingTechniqueStepper::AdvanceExecuteStep()
	{
		if (_sequenceIterator == _sequenceEnd) return nullptr;
		while (_stepIterator == _stepEnd) {
			_drawablePktIdxOffset += s_drawablePktsPerParse*_sequenceIterator->_sequence->DrawablePktsToReserve();
			++_sequenceIterator;
			_stepIterator = _stepEnd = {};
			if (_sequenceIterator ==_sequenceEnd) return nullptr;
			_stepIterator = _sequenceIterator->_sequence->_steps.begin();
			_stepEnd = _sequenceIterator->_sequence->_steps.end();
		}
		auto* result = AsPointer(_stepIterator);
		++_stepIterator;
		return result;
	}

	const LightingTechniqueSequence::ParseStep* LightingTechniqueStepper::AdvanceParseStep()
	{
		if (_sequenceIterator == _sequenceEnd) return nullptr;
		while (_parseStepIterator == _parseStepEnd) {
			_drawablePktIdxOffset += s_drawablePktsPerParse*_sequenceIterator->_sequence->DrawablePktsToReserve();
			++_sequenceIterator;
			_parseStepIterator = _parseStepEnd = {};
			if (_sequenceIterator ==_sequenceEnd) return nullptr;
			_parseStepIterator = _sequenceIterator->_sequence->_parseSteps.begin();
			_parseStepEnd = _sequenceIterator->_sequence->_parseSteps.end();
		}
		auto* result = AsPointer(_parseStepIterator);
		++_parseStepIterator;
		return result;
	}

	LightingTechniqueStepper::LightingTechniqueStepper(const CompiledLightingTechnique& technique)
	{
		_sequenceIterator = technique._sequences.begin();
		_sequenceEnd = technique._sequences.end();
		if (_sequenceIterator != _sequenceEnd) {
			_stepIterator = _sequenceIterator->_sequence->_steps.begin();
			_stepEnd = _sequenceIterator->_sequence->_steps.end();
			_parseStepIterator = _sequenceIterator->_sequence->_parseSteps.begin();
			_parseStepEnd = _sequenceIterator->_sequence->_parseSteps.end();
		} else {
			_stepIterator = _stepEnd = {};
			_parseStepIterator = _parseStepEnd = {};
		}
		_drawablePktIdxOffset = 0;
	}

	void LightingTechniqueIterator::ExecuteDrawables(
		LightingTechniqueSequence::ParseId parseId,
		Techniques::SequencerConfig& sequencerCfg,
		const std::shared_ptr<Techniques::IShaderResourceDelegate>& uniformDelegate)
	{
		Techniques::DrawablesPacket* pkts[(unsigned)Techniques::Batch::Max];
		GetPkts(MakeIteratorRange(pkts), parseId);
		if (uniformDelegate)
			_parsingContext->GetUniformDelegateManager()->AddShaderResourceDelegate(uniformDelegate);
		for (unsigned c=0; c<(unsigned)Techniques::Batch::Max; ++c) {
			if (!pkts[c] || pkts[c]->_drawables.empty()) continue;
			TRY {
				Techniques::Draw(*_parsingContext, *_pipelineAcceleratorPool, sequencerCfg, *pkts[c]);
			} CATCH(...) {
				if (uniformDelegate)
					_parsingContext->GetUniformDelegateManager()->RemoveShaderResourceDelegate(*uniformDelegate);
				throw;
			} CATCH_END
		}
		if (uniformDelegate)
			_parsingContext->GetUniformDelegateManager()->RemoveShaderResourceDelegate(*uniformDelegate);
	}

	void LightingTechniqueIterator::ResetIteration(Phase newPhase)
	{
		_stepper = LightingTechniqueStepper{*_compiledTechnique};
		_currentPhase = newPhase;
	}

	void LightingTechniqueIterator::GetPkts(IteratorRange<Techniques::DrawablesPacket**> result, LightingTechniqueSequence::ParseId parseId)
	{
		auto realParseId = parseId & 0xffff;
		auto batchFlags = parseId >> 16;
		auto pktIdx = _stepper._drawablePktIdxOffset+realParseId*s_drawablePktsPerParse;
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

	void LightingTechniqueIterator::GetOrAllocatePkts(IteratorRange<Techniques::DrawablesPacket**> result, LightingTechniqueSequence::ParseId parseId, Techniques::BatchFlags::BitField batches)
	{
		auto realParseId = parseId & 0xffff;
		auto pktIdx = _stepper._drawablePktIdxOffset+realParseId*s_drawablePktsPerParse;
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

	LightingTechniqueIterator::LightingTechniqueIterator(
		Techniques::ParsingContext& parsingContext,
		const CompiledLightingTechnique& compiledTechnique)
	: _threadContext(&parsingContext.GetThreadContext())
	, _parsingContext(&parsingContext)
	, _pipelineAcceleratorPool(compiledTechnique._pipelineAccelerators.get())
	, _deformAcceleratorPool(nullptr)
	, _compiledTechnique(&compiledTechnique)
	{
		assert(_pipelineAcceleratorPool);
		// If you hit this, it probably means that there's a missing call to CompiledLightingTechnique::CompleteConstruction()
		// (which should have happened at the end of the technique construction process)
		assert(compiledTechnique._isConstructionCompleted); 
		_currentPhase = Phase::SequenceSetup;
	}

	static void Remove(std::vector<Techniques::PreregisteredAttachment>& prereg, uint64_t semantic)
	{
		auto i = std::find_if(prereg.begin(), prereg.end(), [semantic](const auto& c) { return c._semantic == semantic; });
		if (i != prereg.end()) prereg.erase(i);
	}

	auto LightingTechniqueInstance::GetNextStep() -> Step
	{
		if (!_iterator)
			return GetNextPrepareResourcesStep();

		if (_iterator->_currentPhase == LightingTechniqueIterator::Phase::SequenceSetup) {
			if (_iterator->_compiledTechnique->_hasPrevProjDesc) {
				_iterator->_parsingContext->GetPrevProjectionDesc() = _iterator->_compiledTechnique->_prevProjDesc;
				_iterator->_parsingContext->GetEnablePrevProjectionDesc() = true;
			}
			_iterator->_compiledTechnique->_prevProjDesc = _iterator->_parsingContext->GetProjectionDesc();
			_iterator->_compiledTechnique->_hasPrevProjDesc = true;

			if (_iterator->_compiledTechnique->_preSequenceSetup)
				_iterator->_compiledTechnique->_preSequenceSetup(*_iterator);
			for (auto& sequence:_iterator->_compiledTechnique->_sequences) {
				if (sequence._dynamicFn) {
					sequence._sequence->Reset();
					sequence._dynamicFn(*_iterator, *sequence._sequence);
				}
			}
			_iterator->ResetIteration(LightingTechniqueIterator::Phase::SceneParse);
		}

		if (_iterator->_currentPhase == LightingTechniqueIterator::Phase::SceneParse) {
			const LightingTechniqueSequence::ParseStep* next;
			while ((next=_iterator->_stepper.AdvanceParseStep())) {
				if (!next->_prepareOnly) {
					std::vector<Techniques::DrawablesPacket*> pkts;
					pkts.resize((unsigned)Techniques::Batch::Max);
					_iterator->GetOrAllocatePkts(MakeIteratorRange(pkts), next->_parseId, next->_batches);
					return { StepType::ParseScene, std::move(pkts), next->_complexCullingVolume.get() };
				}
			}

			_iterator->ResetIteration(LightingTechniqueIterator::Phase::Execute);
			return { StepType::ReadyInstances };
		}

		const LightingTechniqueSequence::ExecuteStep* next;
		while ((next=_iterator->_stepper.AdvanceExecuteStep())) {
			switch (next->_type) {
			case LightingTechniqueSequence::ExecuteStep::Type::CallFunction:
				TRY {
					next->_function(*_iterator);
				} CATCH(const std::exception& e) {
					StringMeldAppend(_iterator->_parsingContext->_stringHelpers->_errorString) << e.what() << "\n";
				} CATCH_END
				break;

			case LightingTechniqueSequence::ExecuteStep::Type::ExecuteDrawables:
				_iterator->ExecuteDrawables(next->_fbDescIdx, *next->_sequencerConfig, next->_shaderResourceDelegate);
				break;

			case LightingTechniqueSequence::ExecuteStep::Type::DrawSky:
				{
					Step result;
					result._type = StepType::DrawSky;
					result._parsingContext = _iterator->_parsingContext;
					return result;
				}

			case LightingTechniqueSequence::ExecuteStep::Type::BeginRenderPassInstance:
				{
					assert(next->_fbDescIdx < _iterator->_stepper._sequenceIterator->_sequence->_fbDescs.size());
					Techniques::RenderPassBeginDesc beginDesc;
					beginDesc._frameIdx = _iterator->_compiledTechnique->_frameIdx;
					_iterator->_rpi = Techniques::RenderPassInstance{
						*_iterator->_parsingContext,
						_iterator->_stepper._sequenceIterator->_sequence->_fbDescs[next->_fbDescIdx],
						beginDesc};
				}
				break;

			case LightingTechniqueSequence::ExecuteStep::Type::EndRenderPassInstance:
				_iterator->_rpi.End();
				_iterator->_rpi = {};
				break;

			case LightingTechniqueSequence::ExecuteStep::Type::NextRenderPassStep:
				_iterator->_rpi.NextSubpass();
				break;

			case LightingTechniqueSequence::ExecuteStep::Type::PrepareOnly_ExecuteDrawables:
				break;

			case LightingTechniqueSequence::ExecuteStep::Type::BindDelegate:
				_iterator->_parsingContext->GetUniformDelegateManager()->AddShaderResourceDelegate(next->_shaderResourceDelegate);
				_iterator->_delegatesPendingUnbind.push_back(next->_shaderResourceDelegate.get());
				break;

			case LightingTechniqueSequence::ExecuteStep::Type::InvalidateUniforms:
				_iterator->_parsingContext->GetUniformDelegateManager()->InvalidateUniforms();
				break;

			case LightingTechniqueSequence::ExecuteStep::Type::None:
				assert(0);
				break;
			}
		}

		CleanupPostIteration();
		return Step { StepType::None };
	}

	void LightingTechniqueInstance::CleanupPostIteration()
	{
		// release all drawables now we're complete
		for (auto& pkt:_iterator->_drawablePkt)
			pkt.Reset();

		auto& delegateMan = *_iterator->_parsingContext->GetUniformDelegateManager();
		for (auto delegate:_iterator->_delegatesPendingUnbind)
			delegateMan.RemoveShaderResourceDelegate(*delegate);
		_iterator->_delegatesPendingUnbind.clear();
	}

	LightingTechniqueInstance::LightingTechniqueInstance(
		Techniques::ParsingContext& parsingContext,
		CompiledLightingTechnique& compiledTechnique)
	{
		_iterator = std::make_unique<LightingTechniqueIterator>(parsingContext, compiledTechnique);
	}

	LightingTechniqueInstance::~LightingTechniqueInstance() 
	{
		if (_iterator) {
			// in case of exception, ensure that we've cleaned up everything from the iteration
			CleanupPostIteration();
			++_iterator->_compiledTechnique->_frameIdx;
		}
	}

	class LightingTechniqueInstance::PrepareResourcesIterator
	{
	public:
		std::vector<Techniques::DrawablesPacket> _drawablePkt;
		std::vector<std::future<Techniques::PreparedResourcesVisibility>> _requiredResources;

		Techniques::IPipelineAcceleratorPool* _pipelineAcceleratorPool = nullptr;
		const CompiledLightingTechnique* _compiledTechnique = nullptr;

		LightingTechniqueStepper _stepper;
		enum class Phase { SequenceSetup, SceneParse, Execute };
		Phase _currentPhase = Phase::SequenceSetup;
		void ResetIteration(Phase newPhase)
		{
			_stepper = LightingTechniqueStepper{*_compiledTechnique};
			_currentPhase = newPhase;
		}

		void GetOrAllocatePkts(IteratorRange<Techniques::DrawablesPacket**> result, LightingTechniqueSequence::ParseId parseId, Techniques::BatchFlags::BitField batches)
		{
			auto realParseId = parseId & 0xffff;
			auto pktIdx = _stepper._drawablePktIdxOffset+realParseId*s_drawablePktsPerParse;
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

	auto LightingTechniqueInstance::GetNextPrepareResourcesStep() -> Step
	{
		assert(_prepareResourcesIterator);
		if (_prepareResourcesIterator->_currentPhase == PrepareResourcesIterator::Phase::SequenceSetup) {
			// We can't call _preSequenceSetup or initialize the dynamic sequences because we don't have a LightingTechniqueIterator
			_prepareResourcesIterator->ResetIteration(PrepareResourcesIterator::Phase::SceneParse);
		}

		if (_prepareResourcesIterator->_currentPhase == PrepareResourcesIterator::Phase::SceneParse) {
			const LightingTechniqueSequence::ParseStep* next;
			while ((next=_prepareResourcesIterator->_stepper.AdvanceParseStep())) {
				assert(next->_parseId != ~0u);
				std::vector<Techniques::DrawablesPacket*> pkts;
				pkts.resize((unsigned)Techniques::Batch::Max);
				_prepareResourcesIterator->GetOrAllocatePkts(MakeIteratorRange(pkts), next->_parseId, next->_batches);
				return { StepType::ParseScene, std::move(pkts), next->_complexCullingVolume.get() };
			}

			_prepareResourcesIterator->ResetIteration(PrepareResourcesIterator::Phase::Execute);
		}

		const LightingTechniqueSequence::ExecuteStep* next;
		while ((next=_prepareResourcesIterator->_stepper.AdvanceExecuteStep())) {
			switch (next->_type) {
			case LightingTechniqueSequence::ExecuteStep::Type::DrawSky:
				return { StepType::DrawSky };

			case LightingTechniqueSequence::ExecuteStep::Type::PrepareOnly_ExecuteDrawables:
			case LightingTechniqueSequence::ExecuteStep::Type::ExecuteDrawables:
				{
					auto pktIdx = _prepareResourcesIterator->_stepper._drawablePktIdxOffset+(next->_fbDescIdx&0xffff);
					assert(pktIdx < _prepareResourcesIterator->_drawablePkt.size());
					std::promise<Techniques::PreparedResourcesVisibility> promise;
					auto future = promise.get_future();
					Techniques::PrepareResources(std::move(promise), *_prepareResourcesIterator->_pipelineAcceleratorPool, *next->_sequencerConfig, _prepareResourcesIterator->_drawablePkt[pktIdx]);
					_prepareResourcesIterator->_requiredResources.emplace_back(std::move(future));
				}
				break;

			case LightingTechniqueSequence::ExecuteStep::Type::CallFunction:
			case LightingTechniqueSequence::ExecuteStep::Type::BeginRenderPassInstance:
			case LightingTechniqueSequence::ExecuteStep::Type::EndRenderPassInstance:
			case LightingTechniqueSequence::ExecuteStep::Type::NextRenderPassStep:
			case LightingTechniqueSequence::ExecuteStep::Type::BindDelegate:
			case LightingTechniqueSequence::ExecuteStep::Type::InvalidateUniforms:
				break;

			case LightingTechniqueSequence::ExecuteStep::Type::None:
				assert(0);
				break;
			}
		}

		for (auto& pkt:_prepareResourcesIterator->_drawablePkt)
			pkt.Reset();

		return Step { StepType::None };
	}

	std::future<Techniques::PreparedResourcesVisibility> LightingTechniqueInstance::GetResourcePreparationMarker()
	{
		if (!_prepareResourcesIterator || _prepareResourcesIterator->_requiredResources.empty()) return {};
		
		TRY {
			struct Futures
			{
				std::vector<std::future<Techniques::PreparedResourcesVisibility>> _pendingFutures;
				std::vector<std::future<Techniques::PreparedResourcesVisibility>> _readyFutures;
				Techniques::PreparedResourcesVisibility _starterVisibility;
			};
			auto futures = std::make_shared<Futures>();

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
			
			std::promise<Techniques::PreparedResourcesVisibility> promise;
			auto future = promise.get_future();
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
			return future;
		} CATCH(...) {
			std::promise<Techniques::PreparedResourcesVisibility> exceptionPromise;
			exceptionPromise.set_exception(std::current_exception());
			return exceptionPromise.get_future();
		} CATCH_END
	}

	void LightingTechniqueInstance::SetDeformAcceleratorPool(Techniques::IDeformAcceleratorPool& pool)
	{
		_iterator->_deformAcceleratorPool = &pool;
	}

	LightingTechniqueInstance::LightingTechniqueInstance(
		CompiledLightingTechnique& technique)
	{
		_prepareResourcesIterator = std::make_unique<PrepareResourcesIterator>();
		_prepareResourcesIterator->_pipelineAcceleratorPool = technique._pipelineAccelerators.get();
		_prepareResourcesIterator->_compiledTechnique = &technique;
	}

}}
