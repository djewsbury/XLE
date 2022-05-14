// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../Assets/AnimationBindings.h"
#include "../../Math/Matrix.h"
#include "../../Utility/FunctionUtils.h"
#include <memory>

namespace RenderCore { class IDevice; }
namespace RenderCore { namespace Techniques
{
	class DeformerConstruction;
	class ModelRendererConstruction;
	class PipelineCollection;
	namespace Internal { class DeformerPipelineCollection; }

	void ConfigureCPUSkinDeformers(
		DeformerConstruction&,
		const ModelRendererConstruction&);

	void ConfigureGPUSkinDeformers(
		DeformerConstruction&,
		const ModelRendererConstruction&,
		std::shared_ptr<Internal::DeformerPipelineCollection>);

	std::shared_ptr<Internal::DeformerPipelineCollection> CreateGPUSkinPipelineCollection(
		std::shared_ptr<PipelineCollection> pipelineCollection);

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
