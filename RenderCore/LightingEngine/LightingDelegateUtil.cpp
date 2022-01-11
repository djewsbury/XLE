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
	static LightingTechniqueSequence::ParseId SetupShadowParse(
		LightingTechniqueIterator& iterator,
		LightingTechniqueSequence& sequence,
		Internal::ILightBase& proj,
		ILightScene& lightScene, ILightScene::LightSourceId associatedLightId)
	{
		auto& standardProj = *checked_cast<Internal::StandardShadowProjection*>(&proj);
		std::shared_ptr<XLEMath::ArbitraryConvexVolumeTester> volumeTester;

		// Call the driver if one exists
		if (standardProj._driver) {
			// Note the TryGetLightSourceInterface is expensive particular, and scales poorly with the number of
			// lights in the scene
			auto* positionalLight = lightScene.TryGetLightSourceInterface<IPositionalLightSource>(associatedLightId);
			if (positionalLight)
				volumeTester = ((Internal::IShadowProjectionDriver*)standardProj._driver->QueryInterface(typeid(Internal::IShadowProjectionDriver).hash_code()))->UpdateProjections(
					*iterator._parsingContext, *positionalLight, standardProj);
		}

		// todo - cull out any offscreen projections
		if (volumeTester) {
			return sequence.CreateParseScene(Techniques::BatchFlags::Opaque, std::move(volumeTester));
		} else
			return sequence.CreateParseScene(Techniques::BatchFlags::Opaque);
	}

	std::shared_ptr<IPreparedShadowResult> SetupShadowPrepare(
		LightingTechniqueIterator& iterator,
		LightingTechniqueSequence& sequence,
		Internal::ILightBase& proj,
		ILightScene& lightScene, ILightScene::LightSourceId associatedLightId,
		Techniques::FrameBufferPool& shadowGenFrameBufferPool,
		Techniques::AttachmentPool& shadowGenAttachmentPool)
	{
		auto parseId = SetupShadowParse(iterator, sequence, proj, lightScene, associatedLightId);

		auto& standardProj = *checked_cast<Internal::StandardShadowProjection*>(&proj);
		auto& preparer = *standardProj._preparer;
		auto res = preparer.CreatePreparedShadowResult();
		sequence.CreateStep_CallFunction(
			[&preparer, &proj, &shadowGenFrameBufferPool, &shadowGenAttachmentPool, parseId, res](LightingTechniqueIterator& iterator) {
				auto rpi = preparer.Begin(
					*iterator._threadContext,
					*iterator._parsingContext,
					proj,
					shadowGenFrameBufferPool,
					shadowGenAttachmentPool);
				iterator.ExecuteDrawables(parseId, *preparer.GetSequencerConfig().first, preparer.GetSequencerConfig().second);
				rpi.End();
				preparer.End(*iterator._threadContext, *iterator._parsingContext, rpi, *res);
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

	::Assets::MarkerPtr<Techniques::IShaderResourceDelegate> CreateBuildGBufferResourceDelegate()
	{
		auto normalsFittingTexture = ::Assets::MakeAssetPtr<Techniques::DeferredShaderResource>(NORMALS_FITTING_TEXTURE);
		::Assets::MarkerPtr<Techniques::IShaderResourceDelegate> result("gbuffer-srdelegate");
		::Assets::WhenAll(normalsFittingTexture).ThenConstructToPromise(
			result.AdoptPromise(),
			[](auto nft) { return std::make_shared<BuildGBufferResourceDelegate>(*nft); });
		return result;
	}

}}}

