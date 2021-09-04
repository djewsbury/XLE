// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "LightingDelegateUtil.h"
#include "ShadowUniforms.h"
#include "ShadowPreparer.h"
#include "LightingEngineInternal.h"
#include "../Techniques/RenderPass.h"
#include "../Techniques/DrawableDelegates.h"
#include "../Techniques/DeferredShaderResource.h"
#include "../Techniques/ParsingContext.h"
#include "../../Utility/PtrUtils.h"
#include "../../Assets/Assets.h"
#include "../../xleres/FileList.h"
#include <utility>

namespace RenderCore { namespace LightingEngine { namespace Internal
{
	std::shared_ptr<IPreparedShadowResult> SetupShadowPrepare(
		LightingTechniqueIterator& iterator,
		Internal::ILightBase& proj,
		Techniques::FrameBufferPool& shadowGenFrameBufferPool,
		Techniques::AttachmentPool& shadowGenAttachmentPool)
	{
		auto& preparer = *checked_cast<Internal::ShadowProjectionDesc*>(&proj)->_preparer;
		auto res = preparer.CreatePreparedShadowResult();
		iterator.PushFollowingStep(
			[&preparer, &proj, &shadowGenFrameBufferPool, &shadowGenAttachmentPool](LightingTechniqueIterator& iterator) {
				iterator._rpi = preparer.Begin(
					*iterator._threadContext,
					*iterator._parsingContext,
					proj,
					shadowGenFrameBufferPool,
					shadowGenAttachmentPool);
			});
		iterator.PushFollowingStep(Techniques::BatchFilter::General);
		auto cfg = preparer.GetSequencerConfig();
		iterator.PushFollowingStep(std::move(cfg.first), std::move(cfg.second));
		iterator.PushFollowingStep(
			[res, &preparer](LightingTechniqueIterator& iterator) {
				iterator._rpi.End();
				preparer.End(
					*iterator._threadContext,
					*iterator._parsingContext,
					iterator._rpi,
					*res);
			});
		return res;
	}

	class BuildGBufferResourceDelegate : public Techniques::IShaderResourceDelegate
	{
	public:
        virtual void WriteResourceViews(Techniques::ParsingContext& context, const void* objectContext, uint64_t bindingFlags, IteratorRange<IResourceView**> dst)
		{
			assert(bindingFlags == 1<<0);
			dst[0] = _normalsFitting.get();
			context.RequireCommandList(_completionCmdList);
		}

		BuildGBufferResourceDelegate(Techniques::DeferredShaderResource& normalsFittingResource)
		{
			BindResourceView(0, Utility::Hash64("NormalsFittingTexture"));
			_normalsFitting = normalsFittingResource.GetShaderResource();
			_completionCmdList = normalsFittingResource.GetCompletionCommandList();
		}
		std::shared_ptr<IResourceView> _normalsFitting;
		BufferUploads::CommandListID _completionCmdList;
	};

	::Assets::FuturePtr<Techniques::IShaderResourceDelegate> CreateBuildGBufferResourceDelegate()
	{
		auto normalsFittingTexture = ::Assets::MakeAsset<Techniques::DeferredShaderResource>(NORMALS_FITTING_TEXTURE);
		::Assets::FuturePtr<Techniques::IShaderResourceDelegate> result("gbuffer-srdelegate");
		::Assets::WhenAll(normalsFittingTexture).ThenConstructToFuture(
			result,
			[](auto nft) { return std::make_shared<BuildGBufferResourceDelegate>(*nft); });
		return result;
	}

}}}

