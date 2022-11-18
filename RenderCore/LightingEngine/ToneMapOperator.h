// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "StandardLightScene.h"
#include "../../Assets/AssetsCore.h"
#include <memory>
#include <future>

namespace RenderCore { class IResourceView; class IDevice; class FrameBufferProperties; class IThreadContext; }
namespace RenderCore { namespace Techniques { class FragmentStitchingContext; class IComputeShaderOperator; class PipelineCollection; class ParsingContext; struct FrameBufferTarget; }}
namespace RenderCore { namespace LightingEngine 
{
	class RenderStepFragmentInterface;

	struct ToneMapAcesOperatorDesc
	{
		uint64_t GetHash() const;
	};

	class ToneMapAcesOperator : public std::enable_shared_from_this<ToneMapAcesOperator>
	{
	public:
		void Execute(Techniques::ParsingContext& parsingContext, IResourceView& ldrOutput, IResourceView& hdrInput);

		RenderStepFragmentInterface CreateFragment(const FrameBufferProperties& fbProps);
		void PreregisterAttachments(Techniques::FragmentStitchingContext& stitchingContext);

		::Assets::DependencyValidation GetDependencyValidation() const;

		void CompleteInitialization(IThreadContext& threadContext);

		ToneMapAcesOperator(
			const ToneMapAcesOperatorDesc& desc,
			std::shared_ptr<Techniques::IComputeShaderOperator> shader,
			std::shared_ptr<Techniques::PipelineCollection> pipelinePool);
		~ToneMapAcesOperator();

		static void ConstructToPromise(
			std::promise<std::shared_ptr<ToneMapAcesOperator>>&& promise,
			std::shared_ptr<Techniques::PipelineCollection> pipelinePool,
			const ToneMapAcesOperatorDesc& desc);
	private:
		std::shared_ptr<Techniques::IComputeShaderOperator> _shader;
		std::shared_ptr<Techniques::PipelineCollection> _pool;
		std::shared_ptr<IDevice> _device;
		std::shared_ptr<IResourceView> _params;
	};

}}


