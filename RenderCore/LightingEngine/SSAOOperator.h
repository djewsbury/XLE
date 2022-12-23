// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../../Assets/AssetsCore.h"
#include "../../Utility/MemoryUtils.h"
#include <memory>
#include <future>

namespace RenderCore { class IResourceView; class IDevice; class FrameBufferProperties; }
namespace RenderCore { namespace Techniques { class FragmentStitchingContext; class IComputeShaderOperator; class PipelineCollection; }}
namespace RenderCore { namespace BufferUploads { class ResourceLocator; } }
namespace RenderCore { class IThreadContext; }

namespace RenderCore { namespace LightingEngine
{
	class LightingTechniqueIterator;
	class RenderStepFragmentInterface;

	struct AmbientOcclusionOperatorDesc
	{
		unsigned _searchSteps = 32;
		float _maxWorldSpaceDistance = std::numeric_limits<float>::max();
		bool _sampleBothDirections = true;
		bool _lateTemporalFiltering = true;
		bool _enableFiltering = true;
		bool _enableHierarchicalStepping = true;
		float _thicknessHeuristicFactor = 0.15f;		// set to 1 to disable

		uint64_t GetHash(uint64_t seed = DefaultSeed64) const;
	};

	class SSAOOperator : public std::enable_shared_from_this<SSAOOperator>
	{
	public:
		void Execute(
			LightingTechniqueIterator& iterator,
			IResourceView& inputDepthsSRV,
			IResourceView& inputNormalsSRV,
			IResourceView& inputVelocitiesSRV,
			IResourceView& inputHistoryAccumulation,
			IResourceView& accumulation0UAV,
			IResourceView& accumulation1UAV,
			IResourceView& aoOutputUAV,
			IResourceView* hierarchicalDepths);

		RenderStepFragmentInterface CreateFragment(const FrameBufferProperties& fbProps);
		void PreregisterAttachments(Techniques::FragmentStitchingContext& stitchingContext);

		void ResetAccumulation();
		::Assets::DependencyValidation GetDependencyValidation() const;

		void CompleteInitialization(IThreadContext& threadContext);

		SSAOOperator(
			std::shared_ptr<Techniques::IComputeShaderOperator> perspectiveComputeOp,
			std::shared_ptr<Techniques::IComputeShaderOperator> orthogonalComputeOp,
			std::shared_ptr<Techniques::IComputeShaderOperator> upsampleOp,
			const AmbientOcclusionOperatorDesc& opDesc,
			bool hasHierarchicalDepths);
		~SSAOOperator();

		static void ConstructToPromise(
			std::promise<std::shared_ptr<SSAOOperator>>&& promise,
			std::shared_ptr<Techniques::PipelineCollection> pipelinePool,
			const AmbientOcclusionOperatorDesc& opDesc,
			bool hasHierarchicalDepths);
	private:
		std::shared_ptr<Techniques::IComputeShaderOperator> _perspectiveComputeOp;
		std::shared_ptr<Techniques::IComputeShaderOperator> _orthogonalComputeOp;
		std::shared_ptr<Techniques::IComputeShaderOperator> _upsampleOp;
		std::shared_ptr<IResourceView> _ditherTable;
		::Assets::DependencyValidation _depVal;
		unsigned _pingPongCounter = ~0u;
		AmbientOcclusionOperatorDesc _opDesc;
		bool _hasHierarchicalDepths = false;
		bool _pendingCompleteInit = true;
	};
}}
