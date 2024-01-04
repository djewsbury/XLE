// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../../Utility/IteratorUtils.h"
#include "../../Utility/MemoryUtils.h"
#include <memory>
#include <vector>

namespace RenderCore { class IThreadContext; }
namespace RenderCore { namespace Techniques { class ProjectionDesc; class IDeformAcceleratorPool; class DrawablesPacket; class ParsingContext; struct PreparedResourcesVisibility; }}
namespace RenderCore { namespace Techniques { struct PreregisteredAttachment; class PipelineCollection; class IPipelineAcceleratorPool; struct DoubleBufferAttachment; } }
namespace RenderCore { namespace BufferUploads { using CommandListID = uint32_t; }}
namespace Assets { class IAsyncMarker; class DependencyValidation; }
namespace XLEMath { class ArbitraryConvexVolumeTester; }
namespace std { template<typename Type> class promise; template<typename Type> class future; }

namespace RenderCore { namespace LightingEngine
{
	class LightingEngineApparatus;
	class SharedTechniqueDelegateBox;
	class CompiledLightingTechnique;
	struct LightSourceOperatorDesc;
	struct ShadowOperatorDesc;

	struct ChainedOperatorDesc
	{
		const ChainedOperatorDesc* _next = nullptr;
		uint64_t _structureType = 0;
		ChainedOperatorDesc(size_t structureType=0) : _structureType{structureType} {}
	};

	template <typename Type>
		struct ChainedOperatorTemplate : public ChainedOperatorDesc
	{
		Type _desc;
		ChainedOperatorTemplate() : ChainedOperatorDesc(TypeHashCode<Type>) {}
	};

	struct CreationUtility
	{
		struct OutputTarget
		{
			IteratorRange<const Techniques::PreregisteredAttachment*> _preregisteredAttachments;
		};

		void CreateToPromise(
			std::promise<std::shared_ptr<CompiledLightingTechnique>>&& promise,
			IteratorRange<const LightSourceOperatorDesc*> resolveOperators,
			IteratorRange<const ShadowOperatorDesc*> shadowOperators,
			const ChainedOperatorDesc* globalOperators,
			OutputTarget outputTarget);

		[[nodiscard]] std::future<std::shared_ptr<CompiledLightingTechnique>> CreateToFuture(
			IteratorRange<const LightSourceOperatorDesc*> resolveOperators,
			IteratorRange<const ShadowOperatorDesc*> shadowOperators,
			const ChainedOperatorDesc* globalOperators,
			OutputTarget outputTarget);

		CreationUtility(
			std::shared_ptr<Techniques::IPipelineAcceleratorPool> pipelineAccelerators,
			std::shared_ptr<Techniques::PipelineCollection> pipelinePool,
			std::shared_ptr<SharedTechniqueDelegateBox> techDelBox);

		CreationUtility(LightingEngineApparatus& apparatus);

		std::shared_ptr<Techniques::IPipelineAcceleratorPool> _pipelineAccelerators;
		std::shared_ptr<Techniques::PipelineCollection> _pipelinePool;
		std::shared_ptr<SharedTechniqueDelegateBox> _techDelBox;
	};

	class SequencePlayback;
	// When calling BeginLightingTechniquePlayback, the CompiledLightingTechnique must out-live the returned
	// SequencePlayback
	[[nodiscard]] SequencePlayback BeginLightingTechniquePlayback(Techniques::ParsingContext&, CompiledLightingTechnique&);

	[[nodiscard]] SequencePlayback BeginPrepareResourcesInstance(Techniques::IPipelineAcceleratorPool&, CompiledLightingTechnique&);

	enum class StepType { ParseScene, MultiViewParseScene, DrawSky, ReadyInstances, None, Abort };
	struct FrameToFrameProperties;
	class Sequence;
	class SequenceIterator;
	class LightingTechniqueStepper;

	class SequencePlayback
	{
	public:
		struct Step
		{
			StepType _type = StepType::Abort;
			Techniques::ParsingContext* _parsingContext = nullptr;
			std::vector<Techniques::DrawablesPacket*> _pkts;			// todo -- candidate for subframe heap
			XLEMath::ArbitraryConvexVolumeTester* _complexCullingVolume = nullptr;
			std::vector<Techniques::ProjectionDesc> _multiViewDesc;		// todo -- candidate for subframe heap

			operator bool() const { return _type != StepType::None && _type != StepType::Abort; }
		};
		Step GetNextStep();

		void QueueSequence(Sequence&);

		SequencePlayback(Techniques::ParsingContext&, FrameToFrameProperties& frameToFrameProps);
		~SequencePlayback();

		// For ensuring that required resources are prepared/loaded
		void FulfillWhenNotPending(std::promise<Techniques::PreparedResourcesVisibility>&& promise);
		void AddRequiredCommandList(BufferUploads::CommandListID);
		SequencePlayback(Techniques::IPipelineAcceleratorPool& pipelineAccelerators);

		SequencePlayback(SequencePlayback&&);
		SequencePlayback& operator=(SequencePlayback&&);
	private:
		std::unique_ptr<SequenceIterator> _iterator;
		FrameToFrameProperties* _frameToFrameProps = nullptr;

		std::unique_ptr<LightingTechniqueStepper> _stepper;
		enum class Phase { SequenceSetup, SceneParse, Execute };
		Phase _currentPhase = Phase::SequenceSetup;
		std::vector<Sequence*> _sequences;		// candidate for subframe heap
		void ResetIteration(Phase newPhase);
		bool _begunIteration = false;

		class PrepareResourcesIterator;
		std::unique_ptr<PrepareResourcesIterator> _prepareResourcesIterator;
		Step GetNextPrepareResourcesStep();
		void CleanupPostIteration();
	};

	class ILightScene;
	ILightScene& GetLightScene(CompiledLightingTechnique&);
	const ::Assets::DependencyValidation& GetDependencyValidation(CompiledLightingTechnique&);
	IteratorRange<const Techniques::DoubleBufferAttachment*> GetDoubleBufferAttachments(CompiledLightingTechnique&);
	namespace Internal { void* QueryInterface(CompiledLightingTechnique&, uint64_t typeCode); }
	template<typename Type>
		Type* QueryInterface(CompiledLightingTechnique& technique)
		{
			return (Type*)Internal::QueryInterface(technique, TypeHashCode<Type>);
		}

}}
