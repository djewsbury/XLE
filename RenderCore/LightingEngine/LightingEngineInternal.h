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

	struct FragmentLinkResults
	{
		std::vector<std::pair<uint64_t, AttachmentName>> _generatedAttachments;
		std::vector<uint64_t> _consumedAttachments;
	};

	class CompiledLightingTechnique
	{
	public:
		using StepFnSig = void(LightingTechniqueIterator&);
		void CreateStep_CallFunction(std::function<StepFnSig>&&);
		void CreateStep_ParseScene(Techniques::BatchFilter);
		void CreateStep_ExecuteDrawables(
			std::shared_ptr<Techniques::SequencerConfig> sequencerConfig,
			std::shared_ptr<Techniques::IShaderResourceDelegate> uniformDelegate);
		using FragmentInterfaceRegistration = unsigned;
		FragmentInterfaceRegistration CreateStep_RunFragments(RenderStepFragmentInterface&& fragmentInterface);

		void CreatePrepareOnlyStep_ParseScene(Techniques::BatchFilter);
		void CreatePrepareOnlyStep_ExecuteDrawables(std::shared_ptr<Techniques::SequencerConfig> sequencerConfig);

		void CompleteConstruction();

		std::pair<const FrameBufferDesc*, unsigned> GetResolvedFrameBufferDesc(FragmentInterfaceRegistration) const;

		ILightScene& GetLightScene();

		std::shared_ptr<Techniques::IPipelineAcceleratorPool> _pipelineAccelerators;

		const ::Assets::DependencyValidation& GetDependencyValidation() const { return _depVal; }
		::Assets::DependencyValidation _depVal;

		CompiledLightingTechnique(
			const std::shared_ptr<Techniques::IPipelineAcceleratorPool>& pipelineAccelerators,
			Techniques::FragmentStitchingContext& stitchingContext,
			const std::shared_ptr<ILightScene>& lightScene);
		~CompiledLightingTechnique();

	private:
		// PendingCreateFragmentStep is used internally to merge subsequent CreateStep_ calls
		// into single render passes
		std::vector<std::pair<RenderStepFragmentInterface, FragmentInterfaceRegistration>> _pendingCreateFragmentSteps;
		bool _isConstructionCompleted = false;

		struct Step
		{
			enum class Type { ParseScene, DrawSky, CallFunction, ExecuteDrawables, BeginRenderPassInstance, EndRenderPassInstance, NextRenderPassStep, PrepareOnly_ParseScene, PrepareOnly_ExecuteDrawables, None };
			Type _type = Type::None;
			Techniques::BatchFilter _batch = Techniques::BatchFilter::Max;
			std::shared_ptr<Techniques::SequencerConfig> _sequencerConfig;
			std::shared_ptr<Techniques::IShaderResourceDelegate> _shaderResourceDelegate;
			unsigned _fbDescIdx = ~0u;
			FragmentLinkResults _fragmentLinkResults;

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

		std::shared_ptr<ILightScene> _lightScene;

		friend class LightingTechniqueIterator;
		friend class LightingTechniqueInstance;

		void ResolvePendingCreateFragmentSteps();
	};

    class LightingTechniqueIterator
	{
	public:
		Techniques::RenderPassInstance _rpi;
		Techniques::DrawablesPacket _drawablePkt;

		IThreadContext* _threadContext = nullptr;
		Techniques::ParsingContext* _parsingContext = nullptr;
		Techniques::IPipelineAcceleratorPool* _pipelineAcceleratorPool = nullptr;
		const CompiledLightingTechnique* _compiledTechnique = nullptr;

		void PushFollowingStep(std::function<CompiledLightingTechnique::StepFnSig>&& fn);
        void PushFollowingStep(Techniques::BatchFilter batchFilter);
		void PushFollowingStep(std::shared_ptr<Techniques::SequencerConfig> seqConfig, std::shared_ptr<Techniques::IShaderResourceDelegate> uniformDelegate);
		LightingTechniqueIterator(
			IThreadContext& threadContext,
			Techniques::ParsingContext& parsingContext,
			Techniques::IPipelineAcceleratorPool& pipelineAcceleratorPool,
			const CompiledLightingTechnique& compiledTechnique);

	private:
		std::vector<CompiledLightingTechnique::Step> _steps;
		std::vector<CompiledLightingTechnique::Step>::iterator _stepIterator;
		std::vector<CompiledLightingTechnique::Step>::iterator _pushFollowingIterator;

		friend class LightingTechniqueInstance;
	};

}}

