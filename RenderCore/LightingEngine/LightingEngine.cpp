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
		Step newStep;
		newStep._type = Step::Type::CallFunction;
		newStep._function = std::move(fn);
		_steps.emplace_back(std::move(newStep));
	}

	auto LightingTechniqueSequence::CreateStep_ParseScene(Techniques::BatchFilter batch) -> ParseId
	{
		assert(!_frozen);
		ResolvePendingCreateFragmentSteps();
		Step newStep;
		newStep._type = Step::Type::ParseScene;
		newStep._batch = batch;
		newStep._fbDescIdx = _nextParseId++;
		_steps.emplace_back(std::move(newStep));
		return newStep._fbDescIdx;
	}

	auto LightingTechniqueSequence::CreateStep_ParseScene(Techniques::BatchFilter batch, std::shared_ptr<XLEMath::ArbitraryConvexVolumeTester> complexCullingVolume) -> ParseId
	{
		assert(!_frozen);
		ResolvePendingCreateFragmentSteps();
		Step newStep;
		newStep._type = Step::Type::ParseScene;
		newStep._batch = batch;
		newStep._fbDescIdx = _nextParseId++;
		newStep._complexCullingVolume = std::move(complexCullingVolume);
		_steps.emplace_back(std::move(newStep));
		return newStep._fbDescIdx;
	}

	void LightingTechniqueSequence::CreateStep_ExecuteDrawables(
		std::shared_ptr<Techniques::SequencerConfig> sequencerConfig,
		std::shared_ptr<Techniques::IShaderResourceDelegate> uniformDelegate,
		ParseId parseId)
	{
		assert(!_frozen);
		ResolvePendingCreateFragmentSteps();
		Step newStep;
		newStep._type = Step::Type::ParseScene;
		newStep._sequencerConfig = std::move(sequencerConfig);
		newStep._shaderResourceDelegate = std::move(uniformDelegate);
		newStep._fbDescIdx = parseId;
		_steps.emplace_back(std::move(newStep));
	}

	void LightingTechniqueSequence::CreatePrepareOnlyStep_ParseScene(Techniques::BatchFilter batch, ParseId parseId)
	{
		assert(!_frozen);
		ResolvePendingCreateFragmentSteps();
		Step newStep;
		newStep._type = Step::Type::ParseScene;
		newStep._batch = batch;
		newStep._fbDescIdx = parseId;
		_steps.emplace_back(std::move(newStep));
	}

	void LightingTechniqueSequence::CreatePrepareOnlyStep_ExecuteDrawables(std::shared_ptr<Techniques::SequencerConfig> sequencerConfig, ParseId parseId)
	{
		assert(!_frozen);
		Step newStep;
		newStep._type = Step::Type::PrepareOnly_ExecuteDrawables;
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
		++_nextFragmentInterfaceRegistration;
		return _nextFragmentInterfaceRegistration-1;
	}

	void LightingTechniqueSequence::CreateStep_ReadyInstances()
	{
		Step newStep;
		newStep._type = Step::Type::ReadyInstances;
		_steps.emplace_back(std::move(newStep));
	}

	static const std::string s_defaultSequencerCfgName = "lighting-technique";

	void LightingTechniqueSequence::ResolvePendingCreateFragmentSteps()
	{
		if (_pendingCreateFragmentSteps.empty()) return;

		std::vector<Techniques::FrameBufferDescFragment> fragments;
		for (auto& step:_pendingCreateFragmentSteps)
			fragments.emplace_back(Techniques::FrameBufferDescFragment{step.first.GetFrameBufferDescFragment()});

		auto merged = _stitchingContext->TryStitchFrameBufferDesc(MakeIteratorRange(fragments));

		#if defined(_DEBUG)
			Log(Warning) << "Merged fragment in lighting technique:" << std::endl << merged._log << std::endl;
		#endif

		_stitchingContext->UpdateAttachments(merged);
		_fbDescs.emplace_back(std::move(merged));

		unsigned drawablePacketCounter = 0;
		for (auto& fragments:_pendingCreateFragmentSteps) {
			for (unsigned c=0; c<fragments.first.GetSubpassAddendums().size(); ++c) {
				auto& sb = fragments.first.GetSubpassAddendums()[c];
				using SubpassExtension = RenderStepFragmentInterface::SubpassExtension;
				if (sb._type == SubpassExtension::Type::ExecuteDrawables) {
					Step parseStep;
					parseStep._type = Step::Type::ParseScene;
					parseStep._batch = sb._batchFilter;
					parseStep._fbDescIdx = drawablePacketCounter++;
					_steps.emplace_back(std::move(parseStep));
				}
			}
		}

		_steps.push_back({Step::Type::ReadyInstances});

		// Generate commands for walking through the render pass
		Step beginStep;
		beginStep._type = Step::Type::BeginRenderPassInstance;
		beginStep._fbDescIdx = (unsigned)_fbDescs.size()-1;
		_steps.emplace_back(std::move(beginStep));
		
		drawablePacketCounter = 0;
		unsigned stepCounter = 0;
		for (auto& fragments:_pendingCreateFragmentSteps) {
			assert(_fragmentInterfaceMappings.size() == fragments.second);
			_fragmentInterfaceMappings.push_back({beginStep._fbDescIdx, stepCounter});

			assert(!fragments.first.GetSubpassAddendums().empty());
			for (unsigned c=0; c<fragments.first.GetSubpassAddendums().size(); ++c) {
				if (stepCounter != 0) _steps.push_back({Step::Type::NextRenderPassStep});
				auto& sb = fragments.first.GetSubpassAddendums()[c];

				using SubpassExtension = RenderStepFragmentInterface::SubpassExtension;
				if (sb._type == SubpassExtension::Type::ExecuteDrawables) {
					assert(sb._techniqueDelegate);

					Step drawStep;
					drawStep._type = Step::Type::ExecuteDrawables;
					#if defined(_DEBUG)
						auto name = _fbDescs[beginStep._fbDescIdx]._fbDesc.GetSubpasses()[c]._name;
						if (name.empty()) name = s_defaultSequencerCfgName;
					#else
						auto name = s_defaultSequencerCfgName;
					#endif
					drawStep._fbDescIdx = drawablePacketCounter++;
					drawStep._sequencerConfig = _pipelineAccelerators->CreateSequencerConfig(
						name,
						sb._techniqueDelegate, sb._sequencerSelectors, 
						_fbDescs[beginStep._fbDescIdx]._fbDesc, c);
					drawStep._shaderResourceDelegate = sb._shaderResourceDelegate;
					_steps.emplace_back(std::move(drawStep));
				} else if (sb._type == SubpassExtension::Type::ExecuteSky) {
					_steps.push_back({Step::Type::DrawSky});
				} else if (sb._type == SubpassExtension::Type::CallLightingIteratorFunction) {
					Step newStep;
					newStep._type = Step::Type::CallFunction;
					newStep._function = std::move(sb._lightingIteratorFunction);
					_steps.emplace_back(std::move(newStep));
				} else {
					assert(sb._type == SubpassExtension::Type::HandledByPrevious);
				}

				++stepCounter;
			}
		}

		Step endStep;
		endStep._type = Step::Type::EndRenderPassInstance;
		_steps.push_back(std::move(endStep));

		_pendingCreateFragmentSteps.clear();
	}

	void LightingTechniqueSequence::Reset()
	{
		_pendingCreateFragmentSteps.clear();
		_steps.clear();
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

	const LightingTechniqueSequence::Step* LightingTechniqueIterator::Advance()
	{
		if (_sequenceIterator == _sequenceEnd) return nullptr;
		while (_stepIterator == _stepEnd) {
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

	void LightingTechniqueIterator::ExecuteDrawables(
		LightingTechniqueSequence::ParseId parseId, 
		Techniques::SequencerConfig& sequencerCfg,
		const std::shared_ptr<Techniques::IShaderResourceDelegate>& uniformDelegate)
	{
		assert(parseId < _drawablePkt.size());
		if (uniformDelegate)
			_parsingContext->GetUniformDelegateManager()->AddShaderResourceDelegate(uniformDelegate);
		TRY {
			Techniques::Draw(*_parsingContext, *_pipelineAcceleratorPool, _deformAcceleratorPool, sequencerCfg, _drawablePkt[parseId]);
			_drawablePkt[parseId].Reset();
		} CATCH(...) {
			if (uniformDelegate)
				_parsingContext->GetUniformDelegateManager()->RemoveShaderResourceDelegate(*uniformDelegate);
			throw;
		} CATCH_END
		if (uniformDelegate)
			_parsingContext->GetUniformDelegateManager()->RemoveShaderResourceDelegate(*uniformDelegate);
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
		_sequenceIterator = compiledTechnique._sequences.begin();
		_sequenceEnd = compiledTechnique._sequences.end();
		_stepIterator = _stepEnd = {};
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

		if (_iterator->_pendingDynamicSequenceGen) {
			for (auto& sequence:_iterator->_compiledTechnique->_sequences) {
				if (sequence._dynamicFn) {
					sequence._sequence->Reset();
					sequence._dynamicFn(*_iterator, *sequence._sequence);
				}
			}
			_iterator->_pendingDynamicSequenceGen = false;
		}

		const LightingTechniqueSequence::Step* next;
		while ((next=_iterator->Advance())) {
			switch (next->_type) {
			case LightingTechniqueSequence::Step::Type::ParseScene:
				assert(next->_fbDescIdx != ~0u);
				while (next->_fbDescIdx >= _iterator->_drawablePkt.size())
					_iterator->_drawablePkt.push_back(_iterator->_parsingContext->GetTechniqueContext()._drawablesPacketsPool->Allocate());
				return { StepType::ParseScene, next->_batch, &_iterator->_drawablePkt[next->_fbDescIdx], next->_complexCullingVolume.get() };

			case LightingTechniqueSequence::Step::Type::CallFunction:
				TRY {
					next->_function(*_iterator);
				} CATCH(const std::exception& e) {
					StringMeldAppend(_iterator->_parsingContext->_stringHelpers->_errorString) << e.what() << "\n";
				} CATCH_END
				break;

			case LightingTechniqueSequence::Step::Type::ExecuteDrawables:
				_iterator->ExecuteDrawables(next->_fbDescIdx, *next->_sequencerConfig, next->_shaderResourceDelegate);
				break;

			case LightingTechniqueSequence::Step::Type::DrawSky:
				return { StepType::DrawSky };

			case LightingTechniqueSequence::Step::Type::BeginRenderPassInstance:
				{
					assert(next->_fbDescIdx < _iterator->_sequenceIterator->_sequence->_fbDescs.size());
					Techniques::RenderPassBeginDesc beginDesc;
					beginDesc._frameIdx = _iterator->_compiledTechnique->_frameIdx;
					_iterator->_rpi = Techniques::RenderPassInstance{
						*_iterator->_parsingContext,
						_iterator->_sequenceIterator->_sequence->_fbDescs[next->_fbDescIdx],
						beginDesc};
				}
				break;

			case LightingTechniqueSequence::Step::Type::EndRenderPassInstance:
				_iterator->_rpi.End();
				_iterator->_rpi = {};
				break;

			case LightingTechniqueSequence::Step::Type::NextRenderPassStep:
				_iterator->_rpi.NextSubpass();
				break;

			case LightingTechniqueSequence::Step::Type::ReadyInstances:
				return { StepType::ReadyInstances };

			case LightingTechniqueSequence::Step::Type::PrepareOnly_ParseScene:
			case LightingTechniqueSequence::Step::Type::PrepareOnly_ExecuteDrawables:
				break;

			case LightingTechniqueSequence::Step::Type::None:
				assert(0);
				break;
			}
		}

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

		std::vector<LightingTechniqueSequence::Step>::const_iterator _stepIterator;
		std::vector<LightingTechniqueSequence::Step>::const_iterator _stepEnd;

		std::vector<CompiledLightingTechnique::Sequence>::const_iterator _sequenceIterator;
		std::vector<CompiledLightingTechnique::Sequence>::const_iterator _sequenceEnd;

		Techniques::IPipelineAcceleratorPool* _pipelineAcceleratorPool = nullptr;
		Techniques::DrawablesPacketPool _drawablesPacketPool;
	
		const LightingTechniqueSequence::Step* Advance()
		{
			if (_sequenceIterator == _sequenceEnd) return nullptr;
			while (_stepIterator == _stepEnd) {
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
	};

	auto LightingTechniqueInstance::GetNextPrepareResourcesStep() -> Step
	{
		assert(_prepareResourcesIterator);
		const LightingTechniqueSequence::Step* next;
		while ((next=_prepareResourcesIterator->Advance())) {
			switch (next->_type) {
			case LightingTechniqueSequence::Step::Type::PrepareOnly_ParseScene:
			case LightingTechniqueSequence::Step::Type::ParseScene:
				while (next->_fbDescIdx >= _prepareResourcesIterator->_drawablePkt.size())
					_prepareResourcesIterator->_drawablePkt.push_back(_prepareResourcesIterator->_drawablesPacketPool.Allocate());
				return { StepType::ParseScene, next->_batch, &_prepareResourcesIterator->_drawablePkt[next->_fbDescIdx] };

			case LightingTechniqueSequence::Step::Type::DrawSky:
				return { StepType::DrawSky };

			case LightingTechniqueSequence::Step::Type::PrepareOnly_ExecuteDrawables:
			case LightingTechniqueSequence::Step::Type::ExecuteDrawables:
				{
					assert(next->_fbDescIdx < _prepareResourcesIterator->_drawablePkt.size());
					auto preparation = Techniques::PrepareResources(*_prepareResourcesIterator->_pipelineAcceleratorPool, *next->_sequencerConfig, _prepareResourcesIterator->_drawablePkt[next->_fbDescIdx]);
					if (preparation)
						_prepareResourcesIterator->_requiredResources.push_back(std::move(preparation));
					_prepareResourcesIterator->_drawablePkt[next->_fbDescIdx].Reset();
				}
				break;

			case LightingTechniqueSequence::Step::Type::CallFunction:
			case LightingTechniqueSequence::Step::Type::BeginRenderPassInstance:
			case LightingTechniqueSequence::Step::Type::EndRenderPassInstance:
			case LightingTechniqueSequence::Step::Type::NextRenderPassStep:
				break;

			case LightingTechniqueSequence::Step::Type::ReadyInstances:
				break;

			case LightingTechniqueSequence::Step::Type::None:
				assert(0);
				break;
			}
		}

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
		_prepareResourcesIterator->_sequenceIterator = technique._sequences.begin();
		_prepareResourcesIterator->_sequenceEnd = technique._sequences.end();
		_prepareResourcesIterator->_stepIterator = _prepareResourcesIterator->_stepEnd = {};
	}

}}
