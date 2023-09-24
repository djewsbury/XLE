// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "../Format.h"
#include <memory>
#include <optional>

namespace RenderCore { class IResourceView; class FrameBufferProperties; }
namespace RenderCore { namespace Techniques { class FragmentStitchingContext; class IComputeShaderOperator; class IShaderOperator; class PipelineCollection; class ParsingContext; struct FrameBufferTarget; }}
namespace Assets { class DependencyValidation; }
namespace std { template<typename T> class promise; }

namespace RenderCore { namespace LightingEngine 
{
	class RenderStepFragmentInterface;
	struct ChainedOperatorDesc;

	struct SharpenOperatorDesc
	{
		float _amount = 0.8f;
	};

	class FragmentAttachmentUniformsHelper;

	class PostProcessOperator : public std::enable_shared_from_this<PostProcessOperator>
	{
	public:
		void SecondStageConstruction(
			std::promise<std::shared_ptr<PostProcessOperator>>&& promise,
			const Techniques::FrameBufferTarget& fbTarget);
		void PreregisterAttachments(Techniques::FragmentStitchingContext& stitchingContext, const FrameBufferProperties& fbProps);
		RenderStepFragmentInterface CreateFragment(const FrameBufferProperties& fbProps);

		::Assets::DependencyValidation GetDependencyValidation() const;

		struct CombinedDesc
		{
			std::optional<SharpenOperatorDesc> _sharpen;
		};
		static std::optional<CombinedDesc> MakeCombinedDesc(const ChainedOperatorDesc* descChain);

		PostProcessOperator(
			std::shared_ptr<Techniques::PipelineCollection> pipelinePool,
			const CombinedDesc& desc);
		~PostProcessOperator();
	private:
		std::shared_ptr<Techniques::IComputeShaderOperator> _shader;
		std::shared_ptr<Techniques::PipelineCollection> _pool;
		unsigned _secondStageConstructionState = 0;		// debug usage only
		CombinedDesc _desc;
		std::unique_ptr<FragmentAttachmentUniformsHelper> _uniformsHelper;
	};

}}

