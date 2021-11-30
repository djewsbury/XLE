// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../Assets/SkeletonScaffoldInternal.h"
#include "../Assets/ModelImmutableData.h"
#include "../../Math/Matrix.h"
#include <memory>

namespace RenderCore { class IDevice; }
namespace RenderCore { namespace Techniques
{
	class IDeformOperationFactory;
	std::shared_ptr<IDeformOperationFactory> CreateCPUSkinDeformerFactory();

	class PipelineCollection;
	std::shared_ptr<IDeformOperationFactory> CreateGPUSkinDeformerFactory(
		std::shared_ptr<IDevice> device,
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

