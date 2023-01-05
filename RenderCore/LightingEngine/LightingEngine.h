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

	void CreateLightingTechnique(
		std::promise<std::shared_ptr<CompiledLightingTechnique>>&& promise,
		const std::shared_ptr<Techniques::IPipelineAcceleratorPool>& pipelineAccelerators,
		const std::shared_ptr<Techniques::PipelineCollection>& pipelinePool,
		const std::shared_ptr<SharedTechniqueDelegateBox>& techDelBox,
		IteratorRange<const LightSourceOperatorDesc*> resolveOperators,
		IteratorRange<const ShadowOperatorDesc*> shadowOperators,
		const ChainedOperatorDesc* globalOperators,
		IteratorRange<const Techniques::PreregisteredAttachment*> preregisteredAttachmentsInit);

	// Simplified construction --
	std::future<std::shared_ptr<CompiledLightingTechnique>> CreateLightingTechnique(
		const std::shared_ptr<LightingEngineApparatus>& apparatus,
		IteratorRange<const LightSourceOperatorDesc*> resolveOperators,
		IteratorRange<const ShadowOperatorDesc*> shadowGenerators,
		const ChainedOperatorDesc* globalOperators,
		IteratorRange<const Techniques::PreregisteredAttachment*> preregisteredAttachments);

	enum class StepType { ParseScene, MultiViewParseScene, DrawSky, ReadyInstances, None, Abort };

	class LightingTechniqueIterator;
	class LightingTechniqueInstance
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

		void SetDeformAcceleratorPool(Techniques::IDeformAcceleratorPool&);

		LightingTechniqueInstance(
			Techniques::ParsingContext&,
			CompiledLightingTechnique&);
		~LightingTechniqueInstance();

		// For ensuring that required resources are prepared/loaded
		void FulfillWhenNotPending(std::promise<Techniques::PreparedResourcesVisibility>&& promise);
		LightingTechniqueInstance(
			CompiledLightingTechnique&);
	private:
		std::unique_ptr<LightingTechniqueIterator> _iterator;

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
