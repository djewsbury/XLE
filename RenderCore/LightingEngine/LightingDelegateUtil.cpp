// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "LightingDelegateUtil.h"
#include "ShadowUniforms.h"
#include "ShadowPreparer.h"
#include "LightingEngineInternal.h"
#include "../Techniques/RenderPass.h"
#include "../Utility/PtrUtils.h"
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

}}}

