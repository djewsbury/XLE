// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../../Assets/AssetsCore.h"
#include "../../Utility/MemoryUtils.h"
#include <memory>

namespace RenderCore { class IResourceView; class IDevice; class FrameBufferProperties; }
namespace RenderCore { namespace Techniques { class FragmentStitchingContext; class IComputeShaderOperator; class PipelineCollection; struct FrameBufferTarget; }}
namespace RenderCore { namespace BufferUploads { class ResourceLocator; } }
namespace RenderCore { class IThreadContext; }
namespace std { template<typename T> class promise; }

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

	class ISSAmbientOcclusion
	{
	public:
		virtual ~ISSAmbientOcclusion();
	};

	class SSAOOperator : public ISSAmbientOcclusion, public std::enable_shared_from_this<SSAOOperator>
	{
	public:
		void Execute(
			LightingTechniqueIterator& iterator,
			IResourceView& inputDepthsSRV,
			IResourceView& inputNormalsSRV,
			IResourceView& inputVelocitiesSRV,
			IResourceView& workingUAV,
			IResourceView& accumulation0UAV,
			IResourceView& accumulation1UAV,
			IResourceView& aoOutputUAV,
			IResourceView* historyAccumulationSRV,
			IResourceView* hierarchicalDepthsSRV,
			IResourceView* depthPrevSRV,
        	IResourceView* gbufferNormalPrevSRV);

		RenderStepFragmentInterface CreateFragment(const FrameBufferProperties& fbProps);
		void PreregisterAttachments(Techniques::FragmentStitchingContext& stitchingContext);

		void ResetAccumulation();
		::Assets::DependencyValidation GetDependencyValidation() const;

		void SecondStageConstruction(
			std::promise<std::shared_ptr<SSAOOperator>>&& promise,
			const Techniques::FrameBufferTarget& fbTarget);
		void CompleteInitialization(IThreadContext& threadContext);

		struct IntegrationParams
		{
			bool _hasHierarchicalDepths = false;
			bool _hasHistoryConfidence = false;		// has precomputed history confidence texture
		};

		SSAOOperator(
			std::shared_ptr<Techniques::PipelineCollection> pipelinePool,
			const AmbientOcclusionOperatorDesc& opDesc,
			const IntegrationParams& integrationParams);
		~SSAOOperator();
	private:
		std::shared_ptr<Techniques::IComputeShaderOperator> _perspectiveComputeOp;
		std::shared_ptr<Techniques::IComputeShaderOperator> _orthogonalComputeOp;
		std::shared_ptr<Techniques::IComputeShaderOperator> _upsampleOp;
		std::shared_ptr<IResourceView> _ditherTable;

		unsigned _pingPongCounter = ~0u;
		AmbientOcclusionOperatorDesc _opDesc;
		IntegrationParams _integrationParams;

		std::shared_ptr<Techniques::PipelineCollection> _pipelinePool;
		::Assets::DependencyValidation _depVal;
		bool _pendingCompleteInit = true;
		unsigned _secondStageConstructionState = 0;		// debug usage only
	};
}}
