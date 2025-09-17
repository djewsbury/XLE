// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../Assets/AssetsCore.h"
#include "../Assets/AssetHeap.h"		// for IAssetTracking
#include "../Math/Matrix.h"
#include "../Utility/IteratorUtils.h"
#include <utility>

namespace RenderCore { namespace Assets
{
	class ModelScaffold;
	class CompiledMaterialSet;
	class ModelRendererConstruction;
}}

namespace Assets { class AssetHeapRecord; class OperationContext; }
namespace RenderCore { namespace BufferUploads { class IManager; class IBatchedResources; using CommandListID = uint32_t; }}
namespace RenderCore { class IThreadContext; }
namespace RenderCore { namespace Techniques
{
	class DrawableConstructor;
	class IPipelineAcceleratorPool;
	class IDeformAcceleratorPool;
	class IDrawablesPool;
	class DeformerConstruction;
	class ProjectionDesc;
	class DrawablesPacket;
}}
namespace XLEMath { class ArbitraryConvexVolumeTester; }
namespace std { template<class T> class future; }

namespace SceneEngine
{
	class ExecuteSceneContext;
	class IRigidModelScene : public ::Assets::IAssetTracking
	{
	public:
		using OpaquePtr = std::shared_ptr<void>;
		virtual OpaquePtr CreateModel(std::shared_ptr<RenderCore::Assets::ModelRendererConstruction>) = 0;
		virtual OpaquePtr CreateDeformers(std::shared_ptr<RenderCore::Techniques::DeformerConstruction>) = 0;
		virtual OpaquePtr CreateRenderer(OpaquePtr model, OpaquePtr deformers) = 0;

		struct BuildDrawablesHelper;
		BuildDrawablesHelper BeginBuildDrawables(
			IteratorRange<RenderCore::Techniques::DrawablesPacket** const> = {},
			IteratorRange<const RenderCore::Techniques::ProjectionDesc*> = {},
			const XLEMath::ArbitraryConvexVolumeTester* = nullptr);

		BuildDrawablesHelper BeginBuildDrawables(ExecuteSceneContext&);

		virtual void OnFrameBarrier() = 0;
		virtual void CancelConstructions() = 0;
		virtual std::shared_ptr<RenderCore::BufferUploads::IBatchedResources> GetVBResources() = 0;
		virtual std::shared_ptr<RenderCore::BufferUploads::IBatchedResources> GetIBResources() = 0;
		virtual std::shared_ptr<Assets::OperationContext> GetLoadingContext() = 0;

		struct ModelInfo
		{
			std::pair<Float3, Float3> _boundingBox;
		};
		virtual std::future<ModelInfo> GetModelInfo(OpaquePtr model) = 0;
		virtual std::future<void> FutureForRenderer(OpaquePtr renderer) = 0;
		virtual RenderCore::BufferUploads::CommandListID GetCompletionCommandList(void* renderer) = 0;

		struct Config
		{
			unsigned _modelScaffoldCount = 2000;
			unsigned _materialScaffoldCount = 2000;
			unsigned _rendererCount = 200;
			bool _disableRepositionableGeometry = false;
		};
		virtual ~IRigidModelScene();
	};

	std::shared_ptr<IRigidModelScene> CreateRigidModelScene(
		std::shared_ptr<RenderCore::Techniques::IDrawablesPool> drawablesPool, 
		std::shared_ptr<RenderCore::Techniques::IPipelineAcceleratorPool> pipelineAcceleratorPool, 
		std::shared_ptr<RenderCore::Techniques::IDeformAcceleratorPool> deformAcceleratorPool,
		std::shared_ptr<RenderCore::BufferUploads::IManager> bufferUploads,
		std::shared_ptr<::Assets::OperationContext> loadingContext,
		const IRigidModelScene::Config& cfg = IRigidModelScene::Config{});

	namespace RigidModelSceneInternal { struct Renderer; struct Animator; }
	class DrawableMetadataLookupContext;
	struct IRigidModelScene::BuildDrawablesHelper
	{
		bool SetRenderer(void* renderer);
		void BuildDrawables(
			unsigned instanceIdx,
			const Float3x4& localToWorld, uint32_t viewMask = 1, uint64_t cmdStream = 0);

		void BuildDrawablesInstancedFixedSkeleton(
			IteratorRange<const Float3x4*> objectToWorlds,
			IteratorRange<const unsigned*> viewMasks,
			uint64_t cmdStream = 0);

		void BuildDrawablesInstancedFixedSkeleton(
			IteratorRange<const Float3x4*> objectToWorlds,
			uint64_t cmdStream = 0);

		void CullAndBuildDrawables(
			unsigned instanceIdx, const Float3x4& localToWorld);

		bool IntersectViewFrustumTest(const Float3x4& localToWorld);

		unsigned GetDrawableCount(unsigned pktIndex) const;
		void LookupMetadataProvider(DrawableMetadataLookupContext& context);

		BuildDrawablesHelper(
			IRigidModelScene& scene,
			IteratorRange<RenderCore::Techniques::DrawablesPacket** const> pkts,
			IteratorRange<const RenderCore::Techniques::ProjectionDesc*> views = {},
			const XLEMath::ArbitraryConvexVolumeTester* complexCullingVolume = nullptr);

		BuildDrawablesHelper(
			IRigidModelScene& scene,
			SceneEngine::ExecuteSceneContext& executeContext);
	private:
		RigidModelSceneInternal::Renderer* _activeRenderer;
		IteratorRange<RenderCore::Techniques::DrawablesPacket** const> _pkts;
		IteratorRange<const RenderCore::Techniques::ProjectionDesc*> _views;
		const XLEMath::ArbitraryConvexVolumeTester* _complexCullingVolume;
		IRigidModelScene* _scene;
	};

	inline auto IRigidModelScene::BeginBuildDrawables(
		IteratorRange<RenderCore::Techniques::DrawablesPacket** const> pkt,
		IteratorRange<const RenderCore::Techniques::ProjectionDesc*> viewDesc,
		const XLEMath::ArbitraryConvexVolumeTester* complexCullingTester) -> BuildDrawablesHelper
	{
		return BuildDrawablesHelper { *this, pkt, viewDesc, complexCullingTester };
	}

	inline auto IRigidModelScene::BeginBuildDrawables(
		ExecuteSceneContext& executeContext) -> BuildDrawablesHelper
	{
		return BuildDrawablesHelper { *this, executeContext };
	}

}

