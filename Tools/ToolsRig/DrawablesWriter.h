// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../../RenderCore/Techniques/Drawables.h"
#include "../../Math/Vector.h"
#include "../../Math/Matrix.h"
#include "../../Math/ProjectionMath.h"

namespace RenderCore { namespace BufferUploads { class IManager; }}

namespace ToolsRig
{
	class IDrawablesWriter
	{
	public:
		virtual void WriteDrawables(RenderCore::Techniques::DrawablesPacket& pkt, uint32_t viewMask = 1) = 0;
		virtual ~IDrawablesWriter() = default;
	};

	class IExtendedDrawablesWriter
	{
	public:
		class CustomDrawDelegate
		{
		public:
			virtual void OnDraw(
				RenderCore::Techniques::ParsingContext& parsingContext, const RenderCore::Techniques::ExecuteDrawableContext& drawFnContext, const RenderCore::Techniques::Drawable& drawable,
				unsigned vertexCount, const Float4x4& localToWorld,
				uint64_t viewMask) = 0;
			virtual ~CustomDrawDelegate() = default;
		};
		class CullingDelegate
		{
		public:
			virtual void TestSphere(
				/* out */ uint64_t& boundaryViewMask,
				/* out */ uint64_t& withinViewMask,
				uint64_t testViewMask,
				Float3 center, float radius) const = 0;
			virtual void TestAABB(
				/* out */ uint64_t& boundaryViewMask,
				/* out */ uint64_t& withinViewMask,
				uint64_t testViewMask,
				Float3 mins, Float3 maxs) const = 0;
			virtual ~CullingDelegate() = default;
		};
		virtual void WriteDrawables(
			RenderCore::Techniques::DrawablesPacket& pkt,
			const std::shared_ptr<CustomDrawDelegate>& customDraw) = 0;
		virtual void WriteDrawables(
			RenderCore::Techniques::DrawablesPacket& pkt,
			CullingDelegate& cullingDelegate,
			uint64_t viewMask,
			const std::shared_ptr<CustomDrawDelegate>& customDraw) = 0;
		virtual void WriteDrawables(
			RenderCore::Techniques::DrawablesPacket& pkt,
			const Float4x4& cullingVolume,
			uint32_t viewMask = 1) = 0;
		virtual ~IExtendedDrawablesWriter() = default; 
	};

	struct DrawablesWriterHelper
	{
		std::shared_ptr<IDrawablesWriter> CreateSphereDrawablesWriter();
		std::shared_ptr<IDrawablesWriter> CreateShapeStackDrawableWriter();
		std::shared_ptr<IDrawablesWriter> CreateStonehengeDrawableWriter();
		std::shared_ptr<IDrawablesWriter> CreateFlatPlaneDrawableWriter();
		std::shared_ptr<IDrawablesWriter> CreateFlatPlaneAndBlockerDrawableWriter();
		std::shared_ptr<IDrawablesWriter> CreateSharpContactDrawableWriter();
		std::shared_ptr<IDrawablesWriter> CreateShapeWorldDrawableWriter(const Float2& worldMins, const Float2& worldMaxs);

		DrawablesWriterHelper(
			RenderCore::IDevice& device, RenderCore::Techniques::IDrawablesPool& drawablesPool,
			RenderCore::Techniques::IPipelineAcceleratorPool& pipelineAcceleratorPool);
	protected:
		RenderCore::IDevice* _device;
		RenderCore::Techniques::IDrawablesPool* _drawablesPool;
		RenderCore::Techniques::IPipelineAcceleratorPool* _pipelineAcceleratorPool;
	};

	std::pair<std::shared_ptr<RenderCore::Techniques::DrawableGeo>, size_t> CreateSphereGeo(
		RenderCore::BufferUploads::IManager&, RenderCore::Techniques::IDrawablesPool&);

	std::pair<std::shared_ptr<RenderCore::Techniques::DrawableGeo>, size_t> CreateCubeGeo(
		RenderCore::BufferUploads::IManager&, RenderCore::Techniques::IDrawablesPool&);

	std::pair<std::shared_ptr<RenderCore::Techniques::DrawableGeo>, size_t> CreateTriangleBasePyramidGeo(
		RenderCore::BufferUploads::IManager&, RenderCore::Techniques::IDrawablesPool&);

	void BuildSimpleDrawable(
		RenderCore::Techniques::ParsingContext& parsingContext,
		RenderCore::Techniques::DrawablesPacket& pkt,
		RenderCore::Techniques::PipelineAccelerator& pipelineAccelerator,
		RenderCore::Techniques::DescriptorSetAccelerator* descriptorSetAccelerator,
		RenderCore::Techniques::DrawableGeo& geo,
		size_t vertexCount,
		const Float4x4& localToWorld);

	std::shared_ptr<RenderCore::Techniques::PipelineAccelerator> CreateSimplePipelineAccelerator(RenderCore::Techniques::IPipelineAcceleratorPool&);
}