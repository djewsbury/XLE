// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "LightingEngine.h"
#include "SequenceIterator.h"
#include "RenderStepFragments.h"
#include "../Techniques/RenderPass.h"
#include "../Techniques/TechniqueUtils.h"
#include "../../Assets/DepVal.h"
#include <variant>
#include <memory>
#include <vector>
#include <functional>

namespace RenderCore { namespace Techniques { class ProjectionDesc; }}
namespace RenderCore { namespace LightingEngine
{
	class SequenceIterator;
    class RenderStepFragmentInterface;
	using SequenceParseId = unsigned;

	class Sequence
	{
	public:
		using StepFnSig = void(SequenceIterator&);

		SequenceParseId CreateParseScene(Techniques::BatchFlags::BitField);
		SequenceParseId CreateParseScene(Techniques::BatchFlags::BitField batchFilter, std::shared_ptr<XLEMath::ArbitraryConvexVolumeTester> complexCullingVolume);
		SequenceParseId CreateMultiViewParseScene(
			Techniques::BatchFlags::BitField batchFilter,
			std::vector<Techniques::ProjectionDesc>&& projDescs,
			std::shared_ptr<XLEMath::ArbitraryConvexVolumeTester> complexCullingVolume);

		void CreateStep_CallFunction(std::function<StepFnSig>&&);
		void CreateStep_ExecuteDrawables(
			std::shared_ptr<Techniques::SequencerConfig> sequencerConfig,
			std::shared_ptr<Techniques::IShaderResourceDelegate> uniformDelegate,
			SequenceParseId parseId=0);
		using FragmentInterfaceRegistration = unsigned;
		FragmentInterfaceRegistration CreateStep_RunFragments(RenderStepFragmentInterface&& fragmentInterface);

		SequenceParseId CreatePrepareOnlyParseScene(Techniques::BatchFlags::BitField);
		void CreatePrepareOnlyStep_ExecuteDrawables(std::shared_ptr<Techniques::SequencerConfig> sequencerConfig, SequenceParseId parseId=0);

		void CreateStep_BindDelegate(std::shared_ptr<Techniques::IShaderResourceDelegate> uniformDelegate);
		void CreateStep_InvalidateUniforms();
		void CreateStep_BringUpToDateUniforms();

		// Ensure that we retain attachment data for the given semantic. This is typically used for debugging
		//		-- ie, keeping an intermediate attachment that would otherwise be discarded after usage
		void ForceRetainAttachment(uint64_t semantic, BindFlag::BitField layout);

		template<typename Type> void AddInterface(std::shared_ptr<Type> interf) { AddInterface(TypeHashCode<Type>, std::move(interf)); }
		template<typename Type> Type* QueryInterface() { return (Type*)QueryInterface(TypeHashCode<Type>); }

		void AddInterface(uint64_t typeCode, std::shared_ptr<void>);
		void* QueryInterface(uint64_t);
		std::vector<std::pair<uint64_t, std::shared_ptr<void>>> _interfaces;

		void ResolvePendingCreateFragmentSteps();
		void CompleteAndSeal(
			Techniques::IPipelineAcceleratorPool& pipelineAccelerators,
			Techniques::FragmentStitchingContext& stitchingContext,
			const FrameBufferProperties& fbProps);
		void Reset();
		void TryDynamicInitialization(SequenceIterator&);
		unsigned DrawablePktsToReserve() const { return _nextParseId; }

		std::pair<const FrameBufferDesc*, unsigned> GetResolvedFrameBufferDesc(FragmentInterfaceRegistration) const;

		using DynamicSequenceFn = std::function<void(SequenceIterator&, Sequence&)>;

		Sequence();
		Sequence(DynamicSequenceFn&&);
		~Sequence();

	private:
		struct ExecuteStep
		{
			enum class Type { DrawSky, CallFunction, ExecuteDrawables, BeginRenderPassInstance, EndRenderPassInstance, NextRenderPassStep, PrepareOnly_ExecuteDrawables, BindDelegate, InvalidateUniforms, BringUpToDateUniforms, None };
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
			SequenceParseId _parseId;
			std::shared_ptr<XLEMath::ArbitraryConvexVolumeTester> _complexCullingVolume;
			std::vector<Techniques::ProjectionDesc> _multiViewProjections;		// subframe allocation candidate (for dynamic sequencers)
			bool _prepareOnly = false;
		};
		std::vector<ParseStep> _parseSteps;

		// PendingCreateFragmentStep is used internally to merge subsequent CreateStep_ calls
		// into single render passes
		using PendingCreateFragmentPair = std::pair<RenderStepFragmentInterface, FragmentInterfaceRegistration>;
		using PendingCreateFragmentVariant = std::variant<PendingCreateFragmentPair, ExecuteStep>;
		std::vector<PendingCreateFragmentVariant> _pendingCreateFragmentSteps;

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

		SequenceParseId _nextParseId = 0;
		bool _frozen = false;

		DynamicSequenceFn _dynamicFn;

		void PropagateReverseAttachmentDependencies(Techniques::FragmentStitchingContext& stitchingContext);

		friend class SequenceIterator;
		friend class SequencePlayback;
		friend class CompiledLightingTechnique;
		friend class LightingTechniqueStepper;
	};

	struct FrameToFrameProperties
	{
		unsigned _frameIdx = 0;
		Techniques::ProjectionDesc _prevProjDesc;
		bool _hasPrevProjDesc = false;
	};

	class CompiledLightingTechnique
	{
	public:
		Sequence& CreateSequence();
		void CreateDynamicSequence(Sequence::DynamicSequenceFn&& fn);
		void CompleteConstruction(
			std::shared_ptr<Techniques::IPipelineAcceleratorPool> pipelineAccelerators,
			Techniques::FragmentStitchingContext& stitchingContext,
			const FrameBufferProperties& fbProps);

		BufferUploads::CommandListID GetCompletionCommandList() const { return _completionCommandList; }
		const ::Assets::DependencyValidation& GetDependencyValidation() const { return _depVal; }
		::Assets::DependencyValidation _depVal;
		BufferUploads::CommandListID _completionCommandList = 0;

		IteratorRange<const Techniques::DoubleBufferAttachment*> GetDoubleBufferAttachments() const { return _doubleBufferAttachments; }

		CompiledLightingTechnique();
		~CompiledLightingTechnique();

		std::function<void*(uint64_t)> _queryInterfaceHelper;

		bool _isConstructionCompleted = false;

		std::vector<std::shared_ptr<Sequence>> _sequences;

		std::vector<RenderCore::Techniques::DoubleBufferAttachment> _doubleBufferAttachments;

		FrameToFrameProperties _frameToFrameProperties;

		friend class SequenceIterator;
		friend class SequencePlayback;
		friend class LightingTechniqueStepper;
	};

}}

