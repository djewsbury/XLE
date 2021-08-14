// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../../RenderCore/Techniques/Drawables.h"
#include "../../Math/Vector.h"
#include "../../Math/Matrix.h"

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
				unsigned vertexCount, const Float4x4& localToWorld) = 0;
			virtual ~CustomDrawDelegate() = default;
		};
		virtual void WriteDrawables(
			RenderCore::Techniques::DrawablesPacket& pkt,
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