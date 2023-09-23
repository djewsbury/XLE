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

	};

	class TAAOperator : public std::enable_shared_from_this<TAAOperator>
	{
	public:
		void Execute(
			Techniques::ParsingContext& parsingContext,
			IResourceView& hdrInputAndOutput,
			IResourceView& hdrColorPrev,
			IResourceView& motion,
			IResourceView& depth);

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
		std::shared_ptr<Techniques::PipelineCollection> _pool;
		unsigned _secondStageConstructionState = 0;		// debug usage only
		TAAOperatorDesc _desc;
	};

}}

