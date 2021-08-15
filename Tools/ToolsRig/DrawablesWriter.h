// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../../RenderCore/Techniques/Drawables.h"
#include "../../Math/Vector.h"
#include "../../Math/Matrix.h"
#include "../../Math/ProjectionMath.h"

namespace ToolsRig
{
	class IDrawablesWriter
	{
	public:
		virtual void WriteDrawables(RenderCore::Techniques::DrawablesPacket& pkt) = 0;
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
			const Float4x4& cullingVolume) = 0;
		virtual ~IExtendedDrawablesWriter() = default; 
	};

	std::shared_ptr<IDrawablesWriter> CreateSphereDrawablesWriter(RenderCore::IDevice& device, RenderCore::Techniques::IPipelineAcceleratorPool& pipelineAcceleratorPool);
	std::shared_ptr<IDrawablesWriter> CreateShapeStackDrawableWriter(RenderCore::IDevice& device, RenderCore::Techniques::IPipelineAcceleratorPool& pipelineAcceleratorPool);
	std::shared_ptr<IDrawablesWriter> CreateStonehengeDrawableWriter(RenderCore::IDevice& device, RenderCore::Techniques::IPipelineAcceleratorPool& pipelineAcceleratorPool);
	std::shared_ptr<IDrawablesWriter> CreateFlatPlaneDrawableWriter(RenderCore::IDevice& device, RenderCore::Techniques::IPipelineAcceleratorPool& pipelineAcceleratorPool);
	std::shared_ptr<IDrawablesWriter> CreateSharpContactDrawableWriter(RenderCore::IDevice& device, RenderCore::Techniques::IPipelineAcceleratorPool& pipelineAcceleratorPool);
	std::shared_ptr<IDrawablesWriter> CreateShapeWorldDrawableWriter(
		RenderCore::IDevice& device, RenderCore::Techniques::IPipelineAcceleratorPool& pipelineAcceleratorPool,
		const Float2& worldMins, const Float2& worldMaxs);
}