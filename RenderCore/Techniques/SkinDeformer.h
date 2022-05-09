// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../Assets/AnimationBindings.h"
#include "../Assets/ModelImmutableData.h"
#include "../../Math/Matrix.h"
#include <memory>

namespace RenderCore { class IDevice; }
namespace RenderCore { namespace Assets { class RendererConstruction; }}
namespace RenderCore { namespace Techniques
{
	class DeformerConstruction;
	class PipelineCollection;

	void ConfigureCPUSkinDeformers(
		DeformerConstruction&,
		const Assets::RendererConstruction&);

	void ConfigureGPUSkinDeformers(
		DeformerConstruction&,
		const Assets::RendererConstruction&,
		std::shared_ptr<PipelineCollection>);

	class ISkinDeformer
	{
	public:
		virtual RenderCore::Assets::SkeletonBinding CreateBinding(
			const RenderCore::Assets::SkeletonMachine::OutputInterface& skeletonMachineOutputInterface) const = 0;

		virtual void FeedInSkeletonMachineResults(
			unsigned instanceIdx,
			IteratorRange<const Float4x4*> skeletonMachineOutput,
			const RenderCore::Assets::SkeletonBinding& binding) = 0;

		virtual ~ISkinDeformer();
	};
}}

