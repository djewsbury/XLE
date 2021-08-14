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
		virtual void WriteDrawables(
			RenderCore::Techniques::DrawablesPacket& pkt,
			const Float4x4& cullingVolumn) = 0;
		virtual ~IDrawablesWriter() = default;
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