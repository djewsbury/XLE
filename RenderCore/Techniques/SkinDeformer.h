// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../Assets/AnimationBindings.h"
#include "../../Math/Matrix.h"
#include "../../Utility/FunctionUtils.h"
#include <memory>

namespace RenderCore { class IDevice; }
namespace RenderCore { namespace Assets { class ModelRendererConstruction; }}
namespace RenderCore { namespace Techniques
{
	class DeformerConstruction;
	class PipelineCollection;
	namespace Internal { class DeformerPipelineCollection; }

	class SkinDeformerSystem
	{
	public:
		void ConfigureCPUSkinDeformers(
			DeformerConstruction&,
			const Assets::ModelRendererConstruction&);

		void ConfigureGPUSkinDeformers(
			DeformerConstruction&,
			const Assets::ModelRendererConstruction&);

		static SkinDeformerSystem* GetInstance();

		SkinDeformerSystem(std::shared_ptr<PipelineCollection> pipelineCollection);
		~SkinDeformerSystem();
	protected:
		std::shared_ptr<RenderCore::Techniques::Internal::DeformerPipelineCollection> _pipelineCollection;
	};

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
