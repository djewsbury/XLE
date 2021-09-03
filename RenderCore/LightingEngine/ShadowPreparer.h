// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "LightScene.h"		// for IShadowProjectionFactory
#include "../StateDesc.h"
#include "../Format.h"
#include "../../Assets/AssetsCore.h"
#include "../../Utility/MemoryUtils.h"
#include "../../Utility/ParameterBox.h"
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
		float				_slopeScaledBias = 0.f;
		float				_depthBiasClamp = 0.f;
		int					_rasterDepthBias = 0;
			/// @}

			/// @{
			/// Double sided depth bias
			/// This is useful when flipping the culling mode during shadow
			/// gen. In this case single sided geometry doesn't cause acne
			/// (so we can have very small bias values). But double sided 
			/// geometry still gets acne, so needs a larger bias!
		float				_dsSlopeScaledBias = 0.f;
		float				_dsDepthBiasClamp = 0.f;
		int					_dsRasterDepthBias = 0;
			/// @}

		ShadowProjectionMode	_projectionMode = ShadowProjectionMode::Arbitrary;
		RenderCore::CullMode	_cullMode = RenderCore::CullMode::Back;
		ShadowResolveType		_resolveType = ShadowResolveType::DepthTexture;
		ShadowFilterModel		_filterModel = ShadowFilterModel::PoissonDisc;
		bool					_enableContactHardening = false;
		unsigned				_normalProjCount = 1u;
		bool					_enableNearCascade = false;

		uint64_t Hash(uint64_t seed = DefaultSeed64) const;
	};

///////////////////////////////////////////////////////////////////////////////////////////////////////////

	class IPreparedShadowResult
	{
	public:
		virtual const std::shared_ptr<IDescriptorSet>& GetDescriptorSet() const = 0;
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
			IPreparedShadowResult& res) = 0;
		virtual std::pair<std::shared_ptr<Techniques::SequencerConfig>, std::shared_ptr<Techniques::IShaderResourceDelegate>> GetSequencerConfig() = 0;
		virtual std::shared_ptr<IPreparedShadowResult> CreatePreparedShadowResult() = 0;
		~ICompiledShadowPreparer();
	};

	class ShadowOperatorDesc;
	class SharedTechniqueDelegateBox;
	::Assets::PtrToFuturePtr<ICompiledShadowPreparer> CreateCompiledShadowPreparer(
		const ShadowOperatorDesc& desc,
		const std::shared_ptr<Techniques::IPipelineAcceleratorPool>& pipelineAccelerator,
		const std::shared_ptr<SharedTechniqueDelegateBox>& delegatesBox,
		const std::shared_ptr<RenderCore::Assets::PredefinedDescriptorSetLayout>& descSetLayout);

	class DynamicShadowPreparationOperators
	{
	public:
		struct Operator
		{
			std::shared_ptr<ICompiledShadowPreparer> _preparer;
			ShadowOperatorDesc _desc;
		};
		std::vector<Operator> _operators;

		std::unique_ptr<Internal::ILightBase> CreateShadowProjection(unsigned operatorIdx);
	};
	::Assets::PtrToFuturePtr<DynamicShadowPreparationOperators> CreateDynamicShadowPreparationOperators(
		IteratorRange<const ShadowOperatorDesc*> shadowGenerators, 
		const std::shared_ptr<Techniques::IPipelineAcceleratorPool>& pipelineAccelerator,
		const std::shared_ptr<SharedTechniqueDelegateBox>& delegatesBox,
		const std::shared_ptr<RenderCore::Assets::PredefinedDescriptorSetLayout>& descSetLayout);

///////////////////////////////////////////////////////////////////////////////////////////////////////////

	namespace Internal
	{
		struct ShadowResolveParam
		{
			enum class Shadowing { NoShadows, PerspectiveShadows, OrthShadows, OrthShadowsNearCascade, OrthHybridShadows, CubeMapShadows };
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
}}
