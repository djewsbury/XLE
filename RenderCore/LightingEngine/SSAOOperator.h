// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../../Assets/AssetsCore.h"
#include <memory>

namespace RenderCore { class IResourceView; class IDevice; class FrameBufferProperties; }
namespace RenderCore { namespace Techniques { class FragmentStitchingContext; class IComputeShaderOperator; class PipelineCollection; }}

namespace RenderCore { namespace LightingEngine
{
	class LightingTechniqueIterator;
	class RenderStepFragmentInterface;

	class SSAOOperator : public std::enable_shared_from_this<SSAOOperator>
	{
	public:
		void Execute(
			RenderCore::LightingEngine::LightingTechniqueIterator& iterator,
			RenderCore::IResourceView& inputDepthsSRV,
			RenderCore::IResourceView& inputNormalsSRV,
			RenderCore::IResourceView& inputVelocitiesSRV,
			RenderCore::IResourceView& accumulation0UAV,
			RenderCore::IResourceView& accumulation1UAV,
			RenderCore::IResourceView& aoOutputUAV,
			IResourceView& hierarchicalDepths);

		RenderCore::LightingEngine::RenderStepFragmentInterface CreateFragment(const RenderCore::FrameBufferProperties& fbProps);
		void PreregisterAttachments(RenderCore::Techniques::FragmentStitchingContext& stitchingContext);

		void ResetAccumulation();
		::Assets::DependencyValidation GetDependencyValidation() const;
		uint32_t GetCompletionCommandList() const { return 0; }

		SSAOOperator(
			std::shared_ptr<RenderCore::Techniques::IComputeShaderOperator> computeOp,
			std::shared_ptr<RenderCore::Techniques::IComputeShaderOperator> upsampleOp);
		~SSAOOperator();

		static void ConstructToPromise(
			std::promise<std::shared_ptr<SSAOOperator>>&& promise,
			std::shared_ptr<RenderCore::Techniques::PipelineCollection> pipelinePool);
	private:
		std::shared_ptr<RenderCore::Techniques::IComputeShaderOperator> _computeOp;
		std::shared_ptr<RenderCore::Techniques::IComputeShaderOperator> _upsampleOp;
		::Assets::DependencyValidation _depVal;
		unsigned _pingPongCounter = ~0u;
	};
}}
