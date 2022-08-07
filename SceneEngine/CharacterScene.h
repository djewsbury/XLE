// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../Math/Matrix.h"
#include "../Utility/StringUtils.h"
#include "../Utility/IteratorUtils.h"
#include <memory>

namespace RenderCore { namespace Techniques { class ModelRendererConstruction; class DeformerConstruction; class IDrawablesPool; class IPipelineAcceleratorPool; class IDeformAcceleratorPool; class DrawablesPacket; }}
namespace RenderCore { namespace BufferUploads { class IManager; }}
namespace RenderCore { class IThreadContext; }
namespace Assets { class OperationContext; }
namespace SceneEngine
{
	class ICharacterScene
	{
	public:
		using OpaquePtr = std::shared_ptr<void>;
		virtual OpaquePtr CreateModel(std::shared_ptr<RenderCore::Techniques::ModelRendererConstruction>) = 0;
		virtual OpaquePtr CreateDeformers(std::shared_ptr<RenderCore::Techniques::DeformerConstruction>) = 0;
		virtual OpaquePtr CreateAnimationSet(StringSection<>) = 0;
		virtual OpaquePtr CreateRenderer(OpaquePtr model, OpaquePtr deformers, OpaquePtr animationSet) = 0;

		class CmdListBuilder;
		virtual std::unique_ptr<CmdListBuilder, void(*)(CmdListBuilder*)> BeginCmdList(
			RenderCore::IThreadContext& threadContext,
			IteratorRange<RenderCore::Techniques::DrawablesPacket** const> pkts) = 0;
		virtual void OnFrameBarrier() = 0;
		virtual void CancelConstructions() = 0;

		virtual ~ICharacterScene();
	};

	std::shared_ptr<ICharacterScene> CreateCharacterScene(
		std::shared_ptr<RenderCore::Techniques::IDrawablesPool> drawablesPool,
		std::shared_ptr<RenderCore::Techniques::IPipelineAcceleratorPool> pipelineAcceleratorPool,
		std::shared_ptr<RenderCore::Techniques::IDeformAcceleratorPool> deformAcceleratorPool,
		std::shared_ptr<RenderCore::BufferUploads::IManager> bufferUploads,
		std::shared_ptr<Assets::OperationContext> loadingContext);

	class ICharacterScene::CmdListBuilder
	{
	public:
		void BeginRenderer(void*);

		void ApplyAnimation(uint64_t id, float time);
		void RenderInstance(const Float3x4& localToWorld, uint32_t viewMask=1, uint64_t cmdStream=0);
		void NextInstance();
	};

}
