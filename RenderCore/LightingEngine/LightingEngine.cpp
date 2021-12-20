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

	auto LightingTechniqueSequence::CreateParseScene(Techniques::BatchFilter batch) -> ParseId
	{
		assert(!_frozen);
		for (auto& s:_parseSteps)
			if (s._batch == batch && !s._complexCullingVolume) {
				s._prepareOnly = false;
				return s._parseId;
			}
		ParseStep newStep;
		newStep._batch = batch;
		newStep._parseId = _nextParseId++;
		_parseSteps.emplace_back(std::move(newStep));
		return newStep._parseId;
	}

	auto LightingTechniqueSequence::CreateParseScene(Techniques::BatchFilter batch, std::shared_ptr<XLEMath::ArbitraryConvexVolumeTester> complexCullingVolume) -> ParseId
	{
		assert(!_frozen);
		for (auto& s:_parseSteps)
			if (s._batch == batch && s._complexCullingVolume == complexCullingVolume) {
				s._prepareOnly = false;
				return s._parseId;
			}
		ParseStep newStep;
		newStep._batch = batch;
		newStep._parseId = _nextParseId++;
		newStep._complexCullingVolume = std::move(complexCullingVolume);
		_parseSteps.emplace_back(std::move(newStep));
		return newStep._parseId;
	}

	auto LightingTechniqueSequence::CreatePrepareOnlyParseScene(Techniques::BatchFilter batch) -> ParseId
	{
		assert(!_frozen);
		for (auto& s:_parseSteps)
			if (s._batch == batch && !s._complexCullingVolume)
				return s._parseId;
		ParseStep newStep;
		newStep._batch = batch;
		newStep._parseId = _nextParseId++;
		newStep._prepareOnly = true;
		_parseSteps.emplace_back(std::move(newStep));
		return newStep._parseId;
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

	static const std::string s_defaultSequencerCfgName = "lighting-technique";

	void LightingTechniqueSequence::ResolvePendingCreateFragmentSteps()
	{
		if (_pendingCreateFragmentSteps.empty()) return;

		Techniques::FragmentStitchingContext::StitchResult mergedFB;
		{
			std::vector<Techniques::FrameBufferDescFragment> fragments;
			for (auto& step:_pendingCreateFragmentSteps)
				fragments.emplace_back(Techniques::FrameBufferDescFragment{step.first.GetFrameBufferDescFragment()});
			mergedFB = _stitchingContext->TryStitchFrameBufferDesc(MakeIteratorRange(fragments));
		}

		#if defined(_DEBUG)
			Log(Warning) << "Merged fragment in lighting technique:" << std::endl << mergedFB._log << std::endl;
		#endif

		_stitchingContext->UpdateAttachments(mergedFB);
		_fbDescs.emplace_back(std::move(mergedFB));

		// Generate commands for walking through the render pass
		ExecuteStep beginStep;
		beginStep._type = ExecuteStep::Type::BeginRenderPassInstance;
		beginStep._fbDescIdx = (unsigned)_fbDescs.size()-1;
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
						auto name = _fbDescs[beginStep._fbDescIdx]._fbDesc.GetSubpasses()[c]._name;
						if (name.empty()) name = s_defaultSequencerCfgName;
					#else
						auto name = s_defaultSequencerCfgName;
					#endif
					drawStep._fbDescIdx = CreateParseScene(sb._batchFilter);
					drawStep._sequencerConfig = _pipelineAccelerators->CreateSequencerConfig(
						name,
						sb._techniqueDelegate, sb._sequencerSelectors, 
						_fbDescs[beginStep._fbDescIdx]._fbDesc, c);
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

	void LightingTechniqueSequence::Reset()
	{
		_pendingCreateFragmentSteps.clear();
		_steps.clear();
		_parseSteps.clear();
		_fbDescs.clear();
		_fragmentInterfaceMappings.clear();
		_nextFragmentInterfaceRegistration = 0;
		_stitchingContext = nullptr;
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

	LightingTechniqueSequence::LightingTechniqueSequence(
		std::shared_ptr<Techniques::IPipelineAcceleratorPool> pipelineAccelerators,
		Techniques::FragmentStitchingContext& stitchingContext)
	: _pipelineAccelerators(std::move(pipelineAccelerators))
	, _stitchingContext(&stitchingContext)
	{}

	LightingTechniqueSequence::~LightingTechniqueSequence()
	{}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////

	void CompiledLightingTechnique::CompleteConstruction()
	{
		assert(!_isConstructionCompleted);
		for (auto&s:_sequences) {
			if (!s._dynamicFn) {
				s._sequence->ResolvePendingCreateFragmentSteps();
				s._sequence->_frozen = true;
			}
		}
		_isConstructionCompleted = true;
		_stitchingContext = nullptr;
	}

	LightingTechniqueSequence& CompiledLightingTechnique::CreateSequence()
	{
		Sequence newSequence;
		newSequence._sequence = std::make_shared<LightingTechniqueSequence>(_pipelineAccelerators, *_stitchingContext);
		auto* res = newSequence._sequence.get();
		_sequences.push_back(std::move(newSequence));
		return *res;
	}

	void CompiledLightingTechnique::CreateDynamicSequence(DynamicSequenceFn&& fn)
	{
		auto newSequence = std::make_shared<LightingTechniqueSequence>(_pipelineAccelerators, *_stitchingContext);
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

	CompiledLightingTechnique::CompiledLightingTechnique(
		const std::shared_ptr<Techniques::IPipelineAcceleratorPool>& pipelineAccelerators,
		Techniques::FragmentStitchingContext& stitchingContext,
		const std::shared_ptr<ILightScene>& lightScene)
	: _pipelineAccelerators(pipelineAccelerators)
	, _stitchingContext(&stitchingContext)
	, _lightScene(lightScene)
	{
	}

	CompiledLightingTechnique::~CompiledLightingTechnique() {}

	const LightingTechniqueSequence::ExecuteStep* LightingTechniqueStepper::AdvanceExecuteStep()
	{
		if (_sequenceIterator == _sequenceEnd) return nullptr;
		while (_stepIterator == _stepEnd) {
			_drawablePktIdxOffset += _sequenceIterator->_sequence->DrawablePktsToReserve();
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
			_drawablePktIdxOffset += _sequenceIterator->_sequence->DrawablePktsToReserve();
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
		auto pktIdx = _stepper._drawablePktIdxOffset+parseId;
		assert(pktIdx < _drawablePkt.size());
		if (uniformDelegate)
			_parsingContext->GetUniformDelegateManager()->AddShaderResourceDelegate(uniformDelegate);
		TRY {
			Techniques::Draw(*_parsingContext, *_pipelineAcceleratorPool, sequencerCfg, _drawablePkt[pktIdx]);
		} CATCH(...) {
			if (uniformDelegate)
				_parsingContext->GetUniformDelegateManager()->RemoveShaderResourceDelegate(*uniformDelegate);
			throw;
		} CATCH_END
		if (uniformDelegate)
			_parsingContext->GetUniformDelegateManager()->RemoveShaderResourceDelegate(*uniformDelegate);
	}

	void LightingTechniqueIterator::ResetIteration(Phase newPhase)
	{
		_stepper = LightingTechniqueStepper{*_compiledTechnique};
		_currentPhase = newPhase;
	}

	const Techniques::DrawablesPacket& LightingTechniqueIterator::GetDrawablesPacket(LightingTechniqueSequence::ParseId parseId)
	{
		auto pktIdx = _stepper._drawablePktIdxOffset+parseId;
		assert(pktIdx < _drawablePkt.size());
		return _drawablePkt[pktIdx];
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
					assert(next->_parseId != ~0u);
					auto pktId = _iterator->_stepper._drawablePktIdxOffset+next->_parseId;
					while (pktId >= _iterator->_drawablePkt.size())
						_iterator->_drawablePkt.push_back(_iterator->_parsingContext->GetTechniqueContext()._drawablesPacketsPool->Allocate());
					return { StepType::ParseScene, next->_batch, &_iterator->_drawablePkt[pktId], next->_complexCullingVolume.get() };
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
				return { StepType::DrawSky };

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

			case LightingTechniqueSequence::ExecuteStep::Type::None:
				assert(0);
				break;
			}
		}

		// release all drawables now we're complete
		for (auto& pkt:_iterator->_drawablePkt)
			pkt.Reset();

		return Step { StepType::None };
	}

	LightingTechniqueInstance::LightingTechniqueInstance(
		Techniques::ParsingContext& parsingContext,
		CompiledLightingTechnique& compiledTechnique)
	{
		_iterator = std::make_unique<LightingTechniqueIterator>(parsingContext, compiledTechnique);
	}

	LightingTechniqueInstance::~LightingTechniqueInstance() 
	{
		if (_iterator)
			++_iterator->_compiledTechnique->_frameIdx;
	}

	class LightingTechniqueInstance::PrepareResourcesIterator
	{
	public:
		std::vector<Techniques::DrawablesPacket> _drawablePkt;
		std::vector<std::shared_ptr<::Assets::IAsyncMarker>> _requiredResources;

		Techniques::IPipelineAcceleratorPool* _pipelineAcceleratorPool = nullptr;
		const CompiledLightingTechnique* _compiledTechnique = nullptr;
		Techniques::DrawablesPacketPool _drawablesPacketPool;

		LightingTechniqueStepper _stepper;
		enum class Phase { SequenceSetup, SceneParse, Execute };
		Phase _currentPhase = Phase::SequenceSetup;
		void ResetIteration(Phase newPhase)
		{
			_stepper = LightingTechniqueStepper{*_compiledTechnique};
			_currentPhase = newPhase;
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
				auto pktId = _prepareResourcesIterator->_stepper._drawablePktIdxOffset+next->_parseId;
				while (pktId >= _prepareResourcesIterator->_drawablePkt.size())
					_prepareResourcesIterator->_drawablePkt.push_back(_prepareResourcesIterator->_drawablesPacketPool.Allocate());
				return { StepType::ParseScene, next->_batch, &_prepareResourcesIterator->_drawablePkt[pktId], next->_complexCullingVolume.get() };
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
					auto pktIdx = _prepareResourcesIterator->_stepper._drawablePktIdxOffset+next->_fbDescIdx;
					assert(pktIdx < _prepareResourcesIterator->_drawablePkt.size());
					auto preparation = Techniques::PrepareResources(*_prepareResourcesIterator->_pipelineAcceleratorPool, *next->_sequencerConfig, _prepareResourcesIterator->_drawablePkt[pktIdx]);
					if (preparation)
						_prepareResourcesIterator->_requiredResources.push_back(std::move(preparation));
				}
				break;

			case LightingTechniqueSequence::ExecuteStep::Type::CallFunction:
			case LightingTechniqueSequence::ExecuteStep::Type::BeginRenderPassInstance:
			case LightingTechniqueSequence::ExecuteStep::Type::EndRenderPassInstance:
			case LightingTechniqueSequence::ExecuteStep::Type::NextRenderPassStep:
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

	std::shared_ptr<::Assets::IAsyncMarker> LightingTechniqueInstance::GetResourcePreparationMarker()
	{
		if (!_prepareResourcesIterator || _prepareResourcesIterator->_requiredResources.empty()) return {};
		
		auto marker = std::make_shared<::Assets::AsyncMarkerGroup>();
		for (const auto& c:_prepareResourcesIterator->_requiredResources)
			marker->Add(c, {});
		return marker;
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
