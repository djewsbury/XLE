// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../Math/Matrix.h"
#include "../Utility/StringUtils.h"
#include "../Utility/IteratorUtils.h"
#include <memory>

namespace RenderCore { namespace Techniques { class ModelRendererConstruction; class DeformerConstruction; class IDrawablesPool; class IPipelineAcceleratorPool; class IDeformAcceleratorPool; class DrawablesPacket; class ProjectionDesc; }}
namespace RenderCore { namespace BufferUploads { class IManager; }}
namespace RenderCore { class IThreadContext; }
namespace RenderCore { namespace Assets { class SkeletonMachine; }}
namespace Assets { class OperationContext; }
namespace XLEMath { class ArbitraryConvexVolumeTester; }
namespace SceneEngine
{
	class ExecuteSceneContext;

	class ICharacterScene
	{
	public:
		using OpaquePtr = std::shared_ptr<void>;
		virtual OpaquePtr CreateModel(std::shared_ptr<RenderCore::Techniques::ModelRendererConstruction>) = 0;
		virtual OpaquePtr CreateDeformers(std::shared_ptr<RenderCore::Techniques::DeformerConstruction>) = 0;
		virtual OpaquePtr CreateAnimationSet(StringSection<>) = 0;
		virtual OpaquePtr CreateRenderer(OpaquePtr model, OpaquePtr deformers, OpaquePtr animationSet) = 0;

		struct BuildDrawablesHelper;
		BuildDrawablesHelper BeginBuildDrawables(
			RenderCore::IThreadContext&, IteratorRange<RenderCore::Techniques::DrawablesPacket** const>,
			IteratorRange<const RenderCore::Techniques::ProjectionDesc*> = {},
			const XLEMath::ArbitraryConvexVolumeTester* = nullptr);

		BuildDrawablesHelper BeginBuildDrawables(RenderCore::IThreadContext&, ExecuteSceneContext&);

		struct AnimationConfigureHelper;
		AnimationConfigureHelper BeginAnimationConfigure();

		virtual void OnFrameBarrier() = 0;
		virtual void CancelConstructions() = 0;
		virtual std::shared_ptr<Assets::OperationContext> GetLoadingContext() = 0;

		virtual ~ICharacterScene();
	};

	std::shared_ptr<ICharacterScene> CreateCharacterScene(
		std::shared_ptr<RenderCore::Techniques::IDrawablesPool> drawablesPool,
		std::shared_ptr<RenderCore::Techniques::IPipelineAcceleratorPool> pipelineAcceleratorPool,
		std::shared_ptr<RenderCore::Techniques::IDeformAcceleratorPool> deformAcceleratorPool,
		std::shared_ptr<RenderCore::BufferUploads::IManager> bufferUploads,
		std::shared_ptr<Assets::OperationContext> loadingContext);

	namespace CharacterSceneInternal { struct Renderer; struct Animator; }
	struct ICharacterScene::BuildDrawablesHelper
	{
		bool SetRenderer(void* renderer);
		void BuildDrawables(
			unsigned instanceIdx,
			const Float3x4& localToWorld, uint32_t viewMask = 1, uint64_t cmdStream = 0);

		void CullAndBuildDrawables(
			unsigned instanceIdx, const Float3x4& localToWorld);

		BuildDrawablesHelper(
			RenderCore::IThreadContext& threadContext,
			ICharacterScene& scene,
			IteratorRange<RenderCore::Techniques::DrawablesPacket** const> pkts,
			IteratorRange<const RenderCore::Techniques::ProjectionDesc*> views = {},
			const XLEMath::ArbitraryConvexVolumeTester* complexCullingVolume = nullptr);

		BuildDrawablesHelper(
			RenderCore::IThreadContext& threadContext,
			ICharacterScene& scene,
			SceneEngine::ExecuteSceneContext& executeContext);
	private:
		CharacterSceneInternal::Renderer* _activeRenderer;
		IteratorRange<RenderCore::Techniques::DrawablesPacket** const> _pkts;
		IteratorRange<const RenderCore::Techniques::ProjectionDesc*> _views;
		const XLEMath::ArbitraryConvexVolumeTester* _complexCullingVolume;
	};

	unsigned CharacterInstanceAllocate(void* renderer);
	void CharacterInstanceRelease(void* renderer, unsigned instanceIdx);

	class ICharacterScene::AnimationConfigureHelper
	{
	public:
		bool SetRenderer(void* renderer);
		void ApplySingleAnimation(unsigned instanceIdx, uint64_t id, float time);
		AnimationConfigureHelper(ICharacterScene& scene);
	private:
		ICharacterScene* _scene;
		CharacterSceneInternal::Animator* _activeAnimator;
		const RenderCore::Assets::SkeletonMachine* _activeSkeletonMachine;
	};

	inline auto ICharacterScene::BeginBuildDrawables(
		RenderCore::IThreadContext& threadContext, IteratorRange<RenderCore::Techniques::DrawablesPacket** const> pkt,
		IteratorRange<const RenderCore::Techniques::ProjectionDesc*> viewDesc,
		const XLEMath::ArbitraryConvexVolumeTester* complexCullingTester) -> BuildDrawablesHelper
	{
		return BuildDrawablesHelper { threadContext, *this, pkt, viewDesc, complexCullingTester };
	}

	inline auto ICharacterScene::BeginBuildDrawables(
		RenderCore::IThreadContext& threadContext, ExecuteSceneContext& executeContext) -> BuildDrawablesHelper
	{
		return BuildDrawablesHelper { threadContext, *this, executeContext };
	}

	inline auto ICharacterScene::BeginAnimationConfigure() -> AnimationConfigureHelper
	{
		return AnimationConfigureHelper { *this };
	}

}
