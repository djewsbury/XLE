// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "LightingEngine.h"
#include "RenderStepFragments.h"
#include "../Techniques/RenderPass.h"

namespace RenderCore { namespace LightingEngine
{
	class LightingTechniqueIterator;
    class RenderStepFragmentInterface;

	class LightingTechniqueSequence
	{
	public:
		using StepFnSig = void(LightingTechniqueIterator&);
		using ParseId = unsigned;
		void CreateStep_CallFunction(std::function<StepFnSig>&&);
		ParseId CreateStep_ParseScene(Techniques::BatchFilter);
		ParseId CreateStep_ParseScene(Techniques::BatchFilter batchFilter, std::shared_ptr<XLEMath::ArbitraryConvexVolumeTester> complexCullingVolume);
		void CreateStep_ExecuteDrawables(
			std::shared_ptr<Techniques::SequencerConfig> sequencerConfig,
			std::shared_ptr<Techniques::IShaderResourceDelegate> uniformDelegate,
			ParseId parseId=0);
		void CreateStep_ReadyInstances();
		using FragmentInterfaceRegistration = unsigned;
		FragmentInterfaceRegistration CreateStep_RunFragments(RenderStepFragmentInterface&& fragmentInterface);

		void CreatePrepareOnlyStep_ParseScene(Techniques::BatchFilter, ParseId parseId=0);
		void CreatePrepareOnlyStep_ExecuteDrawables(std::shared_ptr<Techniques::SequencerConfig> sequencerConfig, ParseId parseId=0);

		void ResolvePendingCreateFragmentSteps();
		void Reset();

		std::pair<const FrameBufferDesc*, unsigned> GetResolvedFrameBufferDesc(FragmentInterfaceRegistration) const;

		LightingTechniqueSequence(
			std::shared_ptr<Techniques::IPipelineAcceleratorPool> pipelineAccelerators,
			Techniques::FragmentStitchingContext& stitchingContext);
		~LightingTechniqueSequence();

	private:
		// PendingCreateFragmentStep is used internally to merge subsequent CreateStep_ calls
		// into single render passes
		std::vector<std::pair<RenderStepFragmentInterface, FragmentInterfaceRegistration>> _pendingCreateFragmentSteps;

		struct Step
		{
			enum class Type { ParseScene, DrawSky, CallFunction, ExecuteDrawables, BeginRenderPassInstance, EndRenderPassInstance, NextRenderPassStep, PrepareOnly_ParseScene, PrepareOnly_ExecuteDrawables, ReadyInstances, None };
			Type _type = Type::None;
			Techniques::BatchFilter _batch = Techniques::BatchFilter::Max;
			std::shared_ptr<Techniques::SequencerConfig> _sequencerConfig;
			std::shared_ptr<Techniques::IShaderResourceDelegate> _shaderResourceDelegate;
			unsigned _fbDescIdx = ~0u;		// also used for drawable pkt index
			std::shared_ptr<XLEMath::ArbitraryConvexVolumeTester> _complexCullingVolume;

			std::function<StepFnSig> _function;
		};
		std::vector<Step> _steps;
		std::vector<Techniques::FragmentStitchingContext::StitchResult> _fbDescs;
		
		struct FragmentInterfaceMapping
		{
			unsigned _fbDesc = ~0u;
			unsigned _subpassBegin = ~0u;
		};
		std::vector<FragmentInterfaceMapping> _fragmentInterfaceMappings;
		FragmentInterfaceRegistration _nextFragmentInterfaceRegistration = 0;

		Techniques::FragmentStitchingContext* _stitchingContext = nullptr;
		std::shared_ptr<Techniques::IPipelineAcceleratorPool> _pipelineAccelerators;
		ParseId _nextParseId = 0;
		bool _frozen = false;

		friend class LightingTechniqueIterator;
		friend class LightingTechniqueInstance;
		friend class CompiledLightingTechnique;
	};

	class CompiledLightingTechnique
	{
	public:
		LightingTechniqueSequence& CreateSequence();
		using DynamicSequenceFn = std::function<void(LightingTechniqueIterator&, LightingTechniqueSequence&)>;
		void CreateDynamicSequence(DynamicSequenceFn&& fn);
		void CompleteConstruction();

		ILightScene& GetLightScene();

		std::shared_ptr<Techniques::IPipelineAcceleratorPool> _pipelineAccelerators;

		const ::Assets::DependencyValidation& GetDependencyValidation() const { return _depVal; }
		::Assets::DependencyValidation _depVal;

		CompiledLightingTechnique(
			const std::shared_ptr<Techniques::IPipelineAcceleratorPool>& pipelineAccelerators,
			Techniques::FragmentStitchingContext& stitchingContext,
			const std::shared_ptr<ILightScene>& lightScene);
		~CompiledLightingTechnique();

		mutable unsigned _frameIdx = 0;

	private:
		Techniques::FragmentStitchingContext* _stitchingContext = nullptr;
		std::shared_ptr<ILightScene> _lightScene;
		bool _isConstructionCompleted = false;

		struct Sequence
		{
			std::shared_ptr<LightingTechniqueSequence>_sequence;
			DynamicSequenceFn _dynamicFn;
		};
		std::vector<Sequence> _sequences;

		friend class LightingTechniqueIterator;
		friend class LightingTechniqueInstance;
	};

    class LightingTechniqueIterator
	{
	public:
		Techniques::RenderPassInstance _rpi;
		std::vector<Techniques::DrawablesPacket> _drawablePkt;

		IThreadContext* _threadContext = nullptr;
		Techniques::ParsingContext* _parsingContext = nullptr;
		Techniques::IPipelineAcceleratorPool* _pipelineAcceleratorPool = nullptr;
		Techniques::IDeformAcceleratorPool* _deformAcceleratorPool = nullptr;
		const CompiledLightingTechnique* _compiledTechnique = nullptr;

		void ExecuteDrawables(
			LightingTechniqueSequence::ParseId parseId, 
			Techniques::SequencerConfig& sequencerCfg,
			const std::shared_ptr<Techniques::IShaderResourceDelegate>& uniformDelegate = nullptr);

		const LightingTechniqueSequence::Step* Advance();

		LightingTechniqueIterator(
			Techniques::ParsingContext& parsingContext,
			const CompiledLightingTechnique& compiledTechnique);

	private:
		std::vector<LightingTechniqueSequence::Step>::const_iterator _stepIterator;
		std::vector<LightingTechniqueSequence::Step>::const_iterator _stepEnd;

		std::vector<CompiledLightingTechnique::Sequence>::const_iterator _sequenceIterator;
		std::vector<CompiledLightingTechnique::Sequence>::const_iterator _sequenceEnd;

		bool _pendingDynamicSequenceGen = true;

		friend class LightingTechniqueInstance;
	};

}}

