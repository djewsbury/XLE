// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "StandardLightScene.h"
#include "../Format.h"
#include "../ResourceDesc.h"
#include "../Metal/Forward.h"		// for Metal::GraphicsPipeline
#include "../../Assets/AssetsCore.h"
#include "../../Utility/MemoryUtils.h"
#include <memory>

namespace RenderCore { class IResourceView; class IDevice; class FrameBufferProperties; class IThreadContext; class ICompiledPipelineLayout; }
namespace RenderCore { namespace Techniques { class FragmentStitchingContext; class IComputeShaderOperator; class IShaderOperator; class PipelineCollection; class ParsingContext; struct FrameBufferTarget; }}
namespace std { template<typename T> class promise; }

namespace RenderCore { namespace LightingEngine 
{
	class RenderStepFragmentInterface;

	struct ToneMapAcesOperatorDesc
	{
		/// <summary>Color primaries written to output ColorLDR</summary>
		/// Usually this is exposed to the user, because it should match however their monitor is calibrated
		PresentationColorSpace _outputColorSpace = PresentationColorSpace::SRGB_NonLinear;

		/// <summary>Pixel format for CoLorHDR (ie, pre-tonemapping light accumulation buffer)</summary>
		/// Typical values: Format::R11G11B10_FLOAT, Format::R16G16B16A16_FLOAT, Format::R32G32B32A32_FLOAT
		Format _lightAccumulationBufferFormat = Format::R11G11B10_FLOAT;

		/// <summary>Maximum radius for "large bloom" effect</summary>
		/// Set _broadBloomMaxRadius to greater than 0.0 in order to enable the large bloom radius
		/// 
		/// We allow for 2 separate bloom operations (which can both be used at the same time)
		/// This one is a large radius / soft bloom -- with this bloom, small highlights become very soft, but
		/// bright pixels effect a larger area.
		float _broadBloomMaxRadius = 0.f;

		/// <summary>Enable the small bloom</summary>
		/// This is the second bloom effect. It can be used instead of, or alongside the "large bloom" effect.
		///
		/// This one uses a more accurate blur over a much smaller radius. It can give a nice tight highlight around 
		/// small details
		bool _enablePreciseBloom = false;

		uint64_t GetHash(uint64_t seed = DefaultSeed64) const;
	};

	class IBloom
	{
	public:
		virtual void SetThreshold(float) = 0;
		virtual float GetThreshold() const = 0;

		virtual void SetDesaturationFactor(float) = 0;
		virtual float GetDesaturationFactor() const = 0;

		/// Using exact powers of 2 is recommended for the large radius
		virtual void SetBroadRadius(float) = 0;
		virtual float GetBroadRadius() const = 0;

		virtual void SetPreciseRadius(float) = 0;
		virtual float GetPreciseRadius() const = 0;

		virtual void SetBroadBrightness(Float3) = 0;
		virtual Float3 GetBroadBrightness() const = 0;

		virtual void SetPreciseBrightness(Float3) = 0;
		virtual Float3 GetPreciseBrightness() const = 0;

		virtual ~IBloom();
	};

	class IExposure
	{
	public:
		virtual void SetExposure(float exposureControl) = 0;
		virtual float GetExposure() const = 0;

		virtual ~IExposure();
	};

	class ToneMapAcesOperator : public IBloom, public IExposure, public std::enable_shared_from_this<ToneMapAcesOperator>
	{
	public:
		void Execute(
			Techniques::ParsingContext& parsingContext,
			IResourceView& ldrOutput, IResourceView& hdrInput,
			IteratorRange<IResourceView const*const*> brightPassMipChainUAV,
			IResourceView* brightPassMipChainSRV,
			IResourceView* brightPassHighResBlurWorkingUAV, IResourceView* brightPassHighResBlurWorkingSRV);

		RenderStepFragmentInterface CreateFragment(const FrameBufferProperties& fbProps);
		void PreregisterAttachments(Techniques::FragmentStitchingContext& stitchingContext, const FrameBufferProperties& fbProps);

		::Assets::DependencyValidation GetDependencyValidation() const;

		void SecondStageConstruction(
			std::promise<std::shared_ptr<ToneMapAcesOperator>>&& promise,
			const Techniques::FrameBufferTarget& fbTarget);
		void CompleteInitialization(IThreadContext& threadContext);

		ToneMapAcesOperator(
			std::shared_ptr<Techniques::PipelineCollection> pipelinePool,
			const ToneMapAcesOperatorDesc& desc);
		~ToneMapAcesOperator();
	private:
		std::shared_ptr<Techniques::IComputeShaderOperator> _toneMap;
		std::shared_ptr<Metal::ComputePipeline> _brightPass;
		std::shared_ptr<Metal::ComputePipeline> _brightDownsample;
		std::shared_ptr<Metal::ComputePipeline> _brightUpsample;
		std::shared_ptr<Metal::ComputePipeline> _gaussianFilter;
		std::shared_ptr<Metal::BoundUniforms> _brightPassBoundUniforms;
		std::shared_ptr<ICompiledPipelineLayout> _compiledPipelineLayout;
		std::shared_ptr<IResourceView> _params[3];
		std::shared_ptr<IResourceView> _brightPassParams[3];
		std::shared_ptr<IResourceView> _atomicCounterBufferView;
		std::shared_ptr<IResourceView> _lookupTable;
		unsigned _paramsBufferCounter = 0;
		unsigned _paramsBufferCopyCountdown = 0;
		std::vector<uint8_t> _paramsData;
		unsigned _secondStageConstructionState = 0;		// debug usage only
		ToneMapAcesOperatorDesc _desc;
		std::shared_ptr<Techniques::PipelineCollection> _pool;
		::Assets::DependencyValidation _depVal;
		unsigned _brightPassMipCountCount = 0;
		TextureSamples _samples = TextureSamples::Create();

		float _brightPassLargeRadius = 1.f;
		float _brightPassSmallRadius = 1.f;
		float _bloomThreshold = 2.f;

		mutable bool _lookupTableInitialized = false;

		// IBloom interface
		void SetBroadRadius(float) override;
		float GetBroadRadius() const override;
		void SetPreciseRadius(float) override;
		float GetPreciseRadius() const override;
		void SetThreshold(float) override;
		float GetThreshold() const override;
		void SetDesaturationFactor(float) override;
		float GetDesaturationFactor() const override;
		void SetBroadBrightness(Float3) override;
		Float3 GetBroadBrightness() const override;
		void SetPreciseBrightness(Float3) override;
		Float3 GetPreciseBrightness() const override;

		// IExposure
		void SetExposure(float exposureControl) override;
		float GetExposure() const override;
	};

	class CopyToneMapOperator : public std::enable_shared_from_this<CopyToneMapOperator>
	{
	public:
		void Execute(Techniques::ParsingContext& parsingContext, IResourceView& hdrInput);

		RenderStepFragmentInterface CreateFragment(const FrameBufferProperties& fbProps);
		void PreregisterAttachments(Techniques::FragmentStitchingContext& stitchingContext, const FrameBufferProperties& fbProps);

		::Assets::DependencyValidation GetDependencyValidation() const;

		void SecondStageConstruction(
			std::promise<std::shared_ptr<CopyToneMapOperator>>&& promise,
			const Techniques::FrameBufferTarget& fbTarget);
		void CompleteInitialization(IThreadContext& threadContext);

		CopyToneMapOperator(std::shared_ptr<Techniques::PipelineCollection> pipelinePool);
		~CopyToneMapOperator();
	private:
		std::shared_ptr<Techniques::IShaderOperator> _shader;
		std::shared_ptr<Techniques::PipelineCollection> _pool;
		unsigned _secondStageConstructionState = 0;		// debug usage only
	};

}}

