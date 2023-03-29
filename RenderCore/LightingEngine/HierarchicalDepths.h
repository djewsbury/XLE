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

namespace RenderCore { namespace Techniques { class FragmentStitchingContext; class IComputeShaderOperator; class PipelineCollection; struct FrameBufferTarget; }}
namespace RenderCore { namespace BufferUploads { using CommandListID = uint32_t; }}

namespace RenderCore { namespace LightingEngine
{
	class SequenceIterator;
	class RenderStepFragmentInterface;

	class HierarchicalDepthsOperator : public std::enable_shared_from_this<HierarchicalDepthsOperator>
	{
	public:
		void Execute(RenderCore::LightingEngine::SequenceIterator& iterator);

		RenderCore::LightingEngine::RenderStepFragmentInterface CreateFragment(const FrameBufferProperties& fbProps);
		void PreregisterAttachments(RenderCore::Techniques::FragmentStitchingContext& stitchingContext);
		::Assets::DependencyValidation GetDependencyValidation() const { assert(_secondStageConstructionState==2); return _depVal; }

		void SecondStageConstruction(
			std::promise<std::shared_ptr<HierarchicalDepthsOperator>>&& promise,
			const Techniques::FrameBufferTarget& fbTarget);

		HierarchicalDepthsOperator(
			std::shared_ptr<RenderCore::Techniques::PipelineCollection> pipelinePool);
		~HierarchicalDepthsOperator();
	private:
		std::shared_ptr<Techniques::IComputeShaderOperator> _resolveOp;
		std::shared_ptr<IResourceView> _atomicCounterBufferView;
		std::shared_ptr<RenderCore::Techniques::PipelineCollection> _pipelinePool;
		::Assets::DependencyValidation _depVal;
		unsigned _secondStageConstructionState = 0;		// debug usage only
	};
}}
