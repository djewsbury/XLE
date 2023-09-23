// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include <memory>

namespace RenderCore { class IResourceView; class FrameBufferProperties; }
namespace RenderCore { namespace Techniques { class FragmentStitchingContext; class IComputeShaderOperator; class IShaderOperator; class PipelineCollection; class ParsingContext; struct FrameBufferTarget; }}
namespace Assets { class DependencyValidation; }
namespace std { template<typename T> class promise; }

namespace RenderCore { namespace LightingEngine 
{
	class RenderStepFragmentInterface;

	struct TAAOperatorDesc
	{
		// The time constant is approximately the number of frames for a brightness of 1.0 to decay to .63, assuming the 
		// new signal is black (in practice our clamping and other tricks cause faster adaption in this particular case, though)
		// Basically, large numbers result in more smoothing
		// See https://en.wikipedia.org/wiki/Exponential_smoothing
		// The default, 15.5, is quite a lot of smoothing
		float _timeConstant = 15.5f;

		// Search for the pixel closet to the camera in a 3x3 and use that motion vector
		// May help on boundaries (particularly against the sky)
		bool _findOptimalMotionVector = true;

		// Sample the historical buffer using Catmull-Rom curves for blending
		// Effectively weights in the nearby 4x4 pixels
		bool _catmullRomSampling = true;

		// Apply simple sharpening filter to the "yesterday" buffer. This can offset the softening that the anti-aliasing
		// otherwise gives
		bool _sharpenHistory = true;
	};

	class TAAOperator : public std::enable_shared_from_this<TAAOperator>
	{
	public:
		void Execute(
			Techniques::ParsingContext& parsingContext,
			IResourceView& hdrColor,
			IResourceView& output,
			IResourceView& outputPrev,
			IResourceView& motion,
			IResourceView& depth,
			IResourceView* outputShaderResource = nullptr,
			IResourceView* outputPrevUnorderedAccess = nullptr);

		void SecondStageConstruction(
			std::promise<std::shared_ptr<TAAOperator>>&& promise,
			const Techniques::FrameBufferTarget& fbTarget);
		void PreregisterAttachments(Techniques::FragmentStitchingContext& stitchingContext, const FrameBufferProperties& fbProps);
		RenderStepFragmentInterface CreateFragment(const FrameBufferProperties& fbProps);

		::Assets::DependencyValidation GetDependencyValidation() const;

		TAAOperator(
			std::shared_ptr<Techniques::PipelineCollection> pipelinePool,
			const TAAOperatorDesc& desc);
		~TAAOperator();
	private:
		std::shared_ptr<Techniques::IComputeShaderOperator> _aaResolve;
		std::shared_ptr<Techniques::IComputeShaderOperator> _sharpenFutureYesterday;
		std::shared_ptr<Techniques::PipelineCollection> _pool;
		unsigned _secondStageConstructionState = 0;		// debug usage only
		TAAOperatorDesc _desc;
		bool _firstFrame = true;
	};

}}

