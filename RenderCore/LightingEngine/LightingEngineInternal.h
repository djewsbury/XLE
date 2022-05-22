// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "LightingEngine.h"
#include "RenderStepFragments.h"
#include "../Techniques/RenderPass.h"
#include "../Techniques/TechniqueUtils.h"
#include "../../Assets/DepVal.h"

namespace RenderCore { namespace LightingEngine
{
	class LightingTechniqueIterator;
    class RenderStepFragmentInterface;

	class LightingTechniqueSequence
	{
	public:
		using StepFnSig = void(LightingTechniqueIterator&);
		using ParseId = unsigned;

		ParseId CreateParseScene(Techniques::BatchFlags::BitField);
		ParseId CreateParseScene(Techniques::BatchFlags::BitField batchFilter, std::shared_ptr<XLEMath::ArbitraryConvexVolumeTester> complexCullingVolume);

		void CreateStep_CallFunction(std::function<StepFnSig>&&);
		void CreateStep_ExecuteDrawables(
			std::shared_ptr<Techniques::SequencerConfig> sequencerConfig,
			std::shared_ptr<Techniques::IShaderResourceDelegate> uniformDelegate,
			ParseId parseId=0);
		using FragmentInterfaceRegistration = unsigned;
		FragmentInterfaceRegistration CreateStep_RunFragments(RenderStepFragmentInterface&& fragmentInterface);

		ParseId CreatePrepareOnlyParseScene(Techniques::BatchFlags::BitField);
		void CreatePrepareOnlyStep_ExecuteDrawables(std::shared_ptr<Techniques::SequencerConfig> sequencerConfig, ParseId parseId=0);

		// Ensure that we retain attachment data for the given semantic. This is typically used for debugging
		//		-- ie, keeping an intermediate attachment that would otherwise be discarded after usage
		void ForceRetainAttachment(uint64_t semantic, BindFlag::BitField layout);

		void ResolvePendingCreateFragmentSteps();
		void CompleteAndSeal(
			Techniques::IPipelineAcceleratorPool& pipelineAccelerators,
			Techniques::FragmentStitchingContext& stitchingContext);
		void Reset();
		unsigned DrawablePktsToReserve() const { return _nextParseId; }

		std::pair<const FrameBufferDesc*, unsigned> GetResolvedFrameBufferDesc(FragmentInterfaceRegistration) const;

		LightingTechniqueSequence();
		~LightingTechniqueSequence();

	private:
		// PendingCreateFragmentStep is used internally to merge subsequent CreateStep_ calls
		// into single render passes
		std::vector<std::pair<RenderStepFragmentInterface, FragmentInterfaceRegistration>> _pendingCreateFragmentSteps;

		struct ExecuteStep
		{
			enum class Type { DrawSky, CallFunction, ExecuteDrawables, BeginRenderPassInstance, EndRenderPassInstance, NextRenderPassStep, PrepareOnly_ExecuteDrawables, None };
			Type _type = Type::None;
			std::shared_ptr<Techniques::SequencerConfig> _sequencerConfig;
			std::shared_ptr<Techniques::IShaderResourceDelegate> _shaderResourceDelegate;
			unsigned _fbDescIdx = ~0u;		// also used for drawable pkt index
			std::function<StepFnSig> _function;
		};
		std::vector<ExecuteStep> _steps;
		struct ParseStep
		{
			Techniques::BatchFlags::BitField _batches = 0u;
			ParseId _parseId;
			std::shared_ptr<XLEMath::ArbitraryConvexVolumeTester> _complexCullingVolume;
			bool _prepareOnly = false;
		};
		std::vector<ParseStep> _parseSteps;

		std::vector<std::vector<Techniques::FrameBufferDescFragment>> _fbDescsPendingStitch;
		std::vector<Techniques::FragmentStitchingContext::StitchResult> _fbDescs;
		std::vector<std::pair<uint64_t, BindFlag::BitField>> _forceRetainSemantics;

		struct SequencerConfigPendingConstruction
		{
			unsigned _stepIndex = ~0u;
			std::string _name;
			std::shared_ptr<Techniques::ITechniqueDelegate> _delegate;
			ParameterBox _sequencerSelectors;
			unsigned _fbDescIndex = ~0u;
			unsigned _subpassIndex = ~0u;
		};
		std::vector<SequencerConfigPendingConstruction> _sequencerConfigsPendingConstruction;
		
		struct FragmentInterfaceMapping
		{
			unsigned _fbDesc = ~0u;
			unsigned _subpassBegin = ~0u;
		};
		std::vector<FragmentInterfaceMapping> _fragmentInterfaceMappings;
		FragmentInterfaceRegistration _nextFragmentInterfaceRegistration = 0;

		ParseId _nextParseId = 0;
		bool _frozen = false;

		void PropagateReverseAttachmentDependencies(Techniques::FragmentStitchingContext& stitchingContext);

		friend class LightingTechniqueIterator;
		friend class LightingTechniqueInstance;
		friend class CompiledLightingTechnique;
		friend class LightingTechniqueStepper;
	};

	class CompiledLightingTechnique
	{
	public:
		LightingTechniqueSequence& CreateSequence();
		using DynamicSequenceFn = std::function<void(LightingTechniqueIterator&, LightingTechniqueSequence&)>;
		void CreateDynamicSequence(DynamicSequenceFn&& fn);
		void CompleteConstruction(
			std::shared_ptr<Techniques::IPipelineAcceleratorPool> pipelineAccelerators,
			Techniques::FragmentStitchingContext& stitchingContext);
		void PreSequenceSetup(std::function<void(LightingTechniqueIterator&)>&&);

		ILightScene& GetLightScene();

		std::shared_ptr<Techniques::IPipelineAcceleratorPool> _pipelineAccelerators;

		const ::Assets::DependencyValidation& GetDependencyValidation() const { return _depVal; }
		::Assets::DependencyValidation _depVal;

		CompiledLightingTechnique(const std::shared_ptr<ILightScene>& lightScene = nullptr);
		~CompiledLightingTechnique();

		mutable unsigned _frameIdx = 0;
		mutable Techniques::ProjectionDesc _prevProjDesc;
		mutable bool _hasPrevProjDesc = false;

	private:
		std::shared_ptr<ILightScene> _lightScene;
		bool _isConstructionCompleted = false;

		struct Sequence
		{
			std::shared_ptr<LightingTechniqueSequence>_sequence;
			DynamicSequenceFn _dynamicFn;
		};
		std::vector<Sequence> _sequences;
		std::function<void(LightingTechniqueIterator&)> _preSequenceSetup;

		friend class LightingTechniqueIterator;
		friend class LightingTechniqueInstance;
		friend class LightingTechniqueStepper;
	};

	class LightingTechniqueStepper
	{
	public:
		std::vector<LightingTechniqueSequence::ExecuteStep>::const_iterator _stepIterator;
		std::vector<LightingTechniqueSequence::ExecuteStep>::const_iterator _stepEnd;

		std::vector<LightingTechniqueSequence::ParseStep>::const_iterator _parseStepIterator;
		std::vector<LightingTechniqueSequence::ParseStep>::const_iterator _parseStepEnd;

		std::vector<CompiledLightingTechnique::Sequence>::const_iterator _sequenceIterator;
		std::vector<CompiledLightingTechnique::Sequence>::const_iterator _sequenceEnd;

		const LightingTechniqueSequence::ExecuteStep* AdvanceExecuteStep();
		const LightingTechniqueSequence::ParseStep* AdvanceParseStep();
		unsigned _drawablePktIdxOffset = 0;

		LightingTechniqueStepper(const CompiledLightingTechnique& technique);
		LightingTechniqueStepper() = default;
	};

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
			LightingTechniqueSequence::ParseId parseId,
			Techniques::SequencerConfig& sequencerCfg,
			const std::shared_ptr<Techniques::IShaderResourceDelegate>& uniformDelegate = nullptr);
		void GetPkts(IteratorRange<Techniques::DrawablesPacket**> result, LightingTechniqueSequence::ParseId parse);

		LightingTechniqueIterator(
			Techniques::ParsingContext& parsingContext,
			const CompiledLightingTechnique& compiledTechnique);

	private:
		std::vector<Techniques::DrawablesPacket> _drawablePkt;
		std::vector<bool> _drawablePktsReserved;
		LightingTechniqueStepper _stepper;
		enum class Phase { SequenceSetup, SceneParse, Execute };
		Phase _currentPhase = Phase::SequenceSetup;
		void ResetIteration(Phase newPhase);

		void GetOrAllocatePkts(IteratorRange<Techniques::DrawablesPacket**> result, LightingTechniqueSequence::ParseId parse, Techniques::BatchFlags::BitField batches);

		friend class LightingTechniqueInstance;
	};
}}

