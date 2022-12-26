// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "StandardLightScene.h"
#include "../Format.h"
#include "../../Assets/AssetsCore.h"
#include "../../Utility/MemoryUtils.h"
#include <memory>
#include <future>

namespace RenderCore { class IResourceView; class IDevice; class FrameBufferProperties; class IThreadContext; }
namespace RenderCore { namespace Techniques { class FragmentStitchingContext; class IComputeShaderOperator; class IShaderOperator; class PipelineCollection; class ParsingContext; struct FrameBufferTarget; }}
namespace RenderCore { namespace LightingEngine 
{
	class RenderStepFragmentInterface;

	enum OutputColorSpace {
		SRGB,
		Rec709,			// Rec 709, BT.709 uses the same primaries as SRGB, but has a different monitor curve (without the linear part), and perhaps some other minor specification differences
		/* potential alternative color primaries (p3, Rec2020, etc)*/
	};

	struct ToneMapAcesOperatorDesc
	{
		/// <summary>Color primaries written to output ColorLDR</summary>
		/// Usually this is exposed to the user, because it should match however their monitor is calibrated
		OutputColorSpace _outputColorSpace = OutputColorSpace::Rec709;

		/// <summary>Pixel format for CoLorHDR (ie, pre-tonemapping light accumulation buffer)</summary>
		/// Typical values: Format::R11G11B10_FLOAT, Format::R16G16B16A16_FLOAT, Format::R32G32B32A32_FLOAT
		Format _lightAccumulationBufferFormat = Format::R11G11B10_FLOAT;

		uint64_t GetHash(uint64_t seed = DefaultSeed64) const;
	};

	class ToneMapAcesOperator : public std::enable_shared_from_this<ToneMapAcesOperator>
	{
	public:
		void Execute(
			Techniques::ParsingContext& parsingContext,
			IResourceView& ldrOutput, IResourceView& hdrInput,
			IResourceView& brightPassTempUAV, IResourceView& brightPassTempSRV, 
			IteratorRange<IResourceView const*const*> brightPassMipChainUAV,
			IResourceView& brightPassMipChainSRV);

		RenderStepFragmentInterface CreateFragment(const FrameBufferProperties& fbProps);
		void PreregisterAttachments(Techniques::FragmentStitchingContext& stitchingContext);

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
		std::shared_ptr<Techniques::IComputeShaderOperator> _brightPass;
		std::shared_ptr<Techniques::IComputeShaderOperator> _brightDownsample;
		std::shared_ptr<Techniques::IComputeShaderOperator> _brightUpsample;
		std::shared_ptr<IResourceView> _params[3];
		std::shared_ptr<IResourceView> _atomicCounterBufferView;
		unsigned _paramsBufferCounter = 0;
		unsigned _paramsBufferCopyCountdown = 0;
		std::vector<uint8_t> _paramsData;
		unsigned _secondStageConstructionState = 0;		// debug usage only
		ToneMapAcesOperatorDesc _desc;
		std::shared_ptr<Techniques::PipelineCollection> _pool;
		::Assets::DependencyValidation _depVal;
		unsigned _brightPassMipCountCount = 0;
	};

	class CopyToneMapOperator : public std::enable_shared_from_this<CopyToneMapOperator>
	{
	public:
		void Execute(Techniques::ParsingContext& parsingContext, IResourceView& hdrInput);

		RenderStepFragmentInterface CreateFragment(const FrameBufferProperties& fbProps);
		void PreregisterAttachments(Techniques::FragmentStitchingContext& stitchingContext);

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

