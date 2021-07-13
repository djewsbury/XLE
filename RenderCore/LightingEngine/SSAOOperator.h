// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../../Assets/AssetsCore.h"
#include <memory>

namespace RenderCore { class IResourceView; class IDevice; }
namespace RenderCore { namespace Techniques { class FragmentStitchingContext; class IComputeShaderOperator; class PipelinePool; }}

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
			RenderCore::IResourceView& downresDepthsUAV,
			RenderCore::IResourceView& accumulation0UAV,
			RenderCore::IResourceView& accumulation1UAV,
			RenderCore::IResourceView& aoOutputUAV);

		RenderCore::LightingEngine::RenderStepFragmentInterface CreateFragment();
		void PregisterAttachments(RenderCore::Techniques::FragmentStitchingContext& stitchingContext);

		void ResetAccumulation();
		::Assets::DependencyValidation GetDependencyValidation() const;

		SSAOOperator(
			std::shared_ptr<RenderCore::Techniques::IComputeShaderOperator> computeOp,
			std::shared_ptr<RenderCore::Techniques::IComputeShaderOperator> upsampleOp);
		~SSAOOperator();

		static void ConstructToFuture(
			::Assets::FuturePtr<SSAOOperator>& future,
			std::shared_ptr<RenderCore::Techniques::PipelinePool> pipelinePool);
	private:
		std::shared_ptr<RenderCore::Techniques::IComputeShaderOperator> _computeOp;
		std::shared_ptr<RenderCore::Techniques::IComputeShaderOperator> _upsampleOp;
		::Assets::DependencyValidation _depVal;
		unsigned _pingPongCounter = ~0u;
	};
}}
