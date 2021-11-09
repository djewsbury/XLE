// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../../Assets/AssetsCore.h"
#include <memory>

namespace RenderCore
{
	class FrameBufferProperties;
	class IDevice;
	class IResourceView;
}

namespace RenderCore { namespace Techniques { class FragmentStitchingContext; class IComputeShaderOperator; class PipelineCollection; }}
namespace BufferUploads { using CommandListID = uint32_t; }

namespace RenderCore { namespace LightingEngine
{
	class LightingTechniqueIterator;
	class RenderStepFragmentInterface;

	class HierarchicalDepthsOperator : public std::enable_shared_from_this<HierarchicalDepthsOperator>
	{
	public:
		void Execute(RenderCore::LightingEngine::LightingTechniqueIterator& iterator);
		RenderCore::LightingEngine::RenderStepFragmentInterface CreateFragment(const FrameBufferProperties& fbProps);

		void PreregisterAttachments(RenderCore::Techniques::FragmentStitchingContext& stitchingContext);
		::Assets::DependencyValidation GetDependencyValidation() const { return _depVal; }
		BufferUploads::CommandListID GetCompletionCommandList() const { return _completionCommandList; }

		HierarchicalDepthsOperator(
			std::shared_ptr<RenderCore::Techniques::IComputeShaderOperator> resolveOp,
			std::shared_ptr<RenderCore::IDevice> device);
		~HierarchicalDepthsOperator();

		static void ConstructToPromise(
			std::promise<std::shared_ptr<HierarchicalDepthsOperator>>&& future,
			std::shared_ptr<RenderCore::Techniques::PipelineCollection> pipelinePool);
	private:
		std::shared_ptr<Techniques::IComputeShaderOperator> _resolveOp;
		std::shared_ptr<IResourceView> _atomicCounterBufferView;
		BufferUploads::CommandListID _completionCommandList = 0;
		::Assets::DependencyValidation _depVal;
	};
}}
