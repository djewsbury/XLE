// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../Techniques/TechniqueUtils.h"
#include "../StateDesc.h"
#include "../Format.h"
#include "../Types.h"
#include "../../Utility/MemoryUtils.h"
#include <memory>

namespace RenderCore { namespace Techniques
{
	class IPipelineAcceleratorPool;
	class RenderPassInstance;
	class ParsingContext;
	class FrameBufferPool;
	class AttachmentPool;
	class SequencerConfig;
	class IShaderResourceDelegate;
}}
namespace RenderCore { class IThreadContext; class IDescriptorSet; }
namespace RenderCore { namespace Assets { class PredefinedDescriptorSetLayout; }}
namespace std { template<typename T> class future; }

namespace RenderCore { namespace LightingEngine
{
	enum class ShadowProjectionMode { Arbitrary, Ortho, ArbitraryCubeMap };
	enum class ShadowResolveType { DepthTexture, RayTraced, Probe };
	enum class ShadowFilterModel { None, PoissonDisc, Smooth };

	class ShadowOperatorDesc
	{
	public:
			/// @{
			/// Shadow texture definition
		RenderCore::Format	_format = RenderCore::Format::D16_UNORM;
		uint32_t        	_width = 2048u;
		uint32_t        	_height = 2048u;
			/// @}

			/// @{
			/// single sided depth bias
		Techniques::RSDepthBias _singleSidedBias;
			/// @}

			/// @{
			/// Double sided depth bias
			/// This is useful when flipping the culling mode during shadow
			/// gen. In this case single sided geometry doesn't cause acne
			/// (so we can have very small bias values). But double sided 
			/// geometry still gets acne, so needs a larger bias!
		Techniques::RSDepthBias _doubleSidedBias;
			/// @}

		ShadowProjectionMode	_projectionMode = ShadowProjectionMode::Arbitrary;
		RenderCore::CullMode	_cullMode = RenderCore::CullMode::Back;
		ShadowResolveType		_resolveType = ShadowResolveType::DepthTexture;
		ShadowFilterModel		_filterModel = ShadowFilterModel::PoissonDisc;
		bool					_enableContactHardening = false;
		unsigned				_normalProjCount = 1u;
		bool					_enableNearCascade = false;
		bool					_dominantLight = false;
		bool					_multiViewInstancingPath = true;

		uint64_t GetHash(uint64_t seed = DefaultSeed64) const;
	};

///////////////////////////////////////////////////////////////////////////////////////////////////////////

	class IPreparedShadowResult
	{
	public:
		virtual IDescriptorSet* GetDescriptorSet() const = 0;
		virtual ~IPreparedShadowResult();
	};

	namespace Internal { class ILightBase; }
	class LightingTechniqueIterator;

	class ICompiledShadowPreparer
	{
	public:
		virtual Techniques::RenderPassInstance Begin(
			IThreadContext& threadContext, 
			Techniques::ParsingContext& parsingContext,
			Internal::ILightBase& projection,
			Techniques::FrameBufferPool& shadowGenFrameBufferPool,
			Techniques::AttachmentPool& shadowGenAttachmentPool) = 0;
		virtual void End(
			IThreadContext& threadContext, 
			Techniques::ParsingContext& parsingContext,
			Techniques::RenderPassInstance& rpi,
			PipelineType descSetPipelineType,
			IPreparedShadowResult& res) = 0;
		virtual std::pair<std::shared_ptr<Techniques::SequencerConfig>, std::shared_ptr<Techniques::IShaderResourceDelegate>> GetSequencerConfig() = 0;
		virtual std::shared_ptr<IPreparedShadowResult> CreatePreparedShadowResult() = 0;
		virtual void SetDescriptorSetLayout(
			const std::shared_ptr<RenderCore::Assets::PredefinedDescriptorSetLayout>& descSetLayout,
			PipelineType pipelineType) = 0;
		virtual ~ICompiledShadowPreparer();
	};

	class ShadowOperatorDesc;
	class SharedTechniqueDelegateBox;
	std::future<std::shared_ptr<ICompiledShadowPreparer>> CreateCompiledShadowPreparer(
		const ShadowOperatorDesc& desc,
		const std::shared_ptr<Techniques::IPipelineAcceleratorPool>& pipelineAccelerator,
		const std::shared_ptr<SharedTechniqueDelegateBox>& delegatesBox);

	class DynamicShadowPreparers
	{
	public:
		struct Preparer
		{
			std::shared_ptr<ICompiledShadowPreparer> _preparer;
			ShadowOperatorDesc _desc;
		};
		std::vector<Preparer> _preparers;

		std::pair<std::unique_ptr<Internal::ILightBase>, std::shared_ptr<ICompiledShadowPreparer>> CreateShadowProjection(unsigned operatorIdx);
	};
	std::future<std::shared_ptr<DynamicShadowPreparers>> CreateDynamicShadowPreparers(
		IteratorRange<const ShadowOperatorDesc*> shadowGenerators, 
		const std::shared_ptr<Techniques::IPipelineAcceleratorPool>& pipelineAccelerator,
		const std::shared_ptr<SharedTechniqueDelegateBox>& delegatesBox);

	class LightingTechniqueSequence;
	using TechniqueSequenceParseId = unsigned;
	TechniqueSequenceParseId CreateShadowParseInSequence(
		LightingTechniqueIterator& iterator,
		LightingTechniqueSequence& sequence,
		Internal::ILightBase& proj,
		std::shared_ptr<XLEMath::ArbitraryConvexVolumeTester> volumeTester);

	class IDynamicShadowProjectionScheduler
	{
	public:
		virtual void SetDescriptorSetLayout(
			const std::shared_ptr<RenderCore::Assets::PredefinedDescriptorSetLayout>& descSetLayout,
			PipelineType pipelineType) = 0;
		virtual ~IDynamicShadowProjectionScheduler() = default;
	};

///////////////////////////////////////////////////////////////////////////////////////////////////////////

	namespace Internal
	{
		struct ShadowResolveParam
		{
			enum class Shadowing { NoShadows, PerspectiveShadows, OrthShadows, OrthShadowsNearCascade, OrthHybridShadows, CubeMapShadows, Probe };
			Shadowing _shadowing = Shadowing::NoShadows;
			ShadowFilterModel _filterModel = ShadowFilterModel::PoissonDisc;
			unsigned _normalProjCount = 1u;
			bool _enableContactHardening = false;

			friend bool operator==(const ShadowResolveParam& lhs, const ShadowResolveParam& rhs)
			{
				return lhs._shadowing == rhs._shadowing && lhs._filterModel == rhs._filterModel && lhs._normalProjCount == rhs._normalProjCount && lhs._enableContactHardening == rhs._enableContactHardening;
			}

			void WriteShaderSelectors(ParameterBox& selectors) const;
		};

		ShadowResolveParam MakeShadowResolveParam(const ShadowOperatorDesc& shadowOp);
	}

	const char* AsString(ShadowProjectionMode);
	std::optional<ShadowProjectionMode> AsShadowProjectionMode(StringSection<>);
	const char* AsString(ShadowResolveType);
	std::optional<ShadowResolveType> AsShadowResolveType(StringSection<>);
	const char* AsString(ShadowFilterModel);
	std::optional<ShadowFilterModel> AsShadowFilterModel(StringSection<>);

}}
