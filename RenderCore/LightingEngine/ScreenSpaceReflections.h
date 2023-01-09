// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../Assets/PredefinedCBLayout.h"
#include "../../Assets/AssetsCore.h"
#include "../../Utility/MemoryUtils.h"
#include <memory>

namespace RenderCore
{
	class FrameBufferProperties;
	class IDevice;
	class IResource;
	class IResourceView;
	class IThreadContext; 
}

namespace RenderCore { namespace Techniques
{
	class FragmentStitchingContext;
	class IComputeShaderOperator;
	class PipelineCollection;
	class DeferredShaderResource;
	struct FrameBufferTarget;
}}
namespace RenderCore { namespace Assets { class PredefinedCBLayout; } }
namespace RenderCore { namespace BufferUploads { class ResourceLocator; } }
namespace std { template<typename T> class promise; }

namespace RenderCore { namespace LightingEngine
{
	class LightingTechniqueIterator;
	class RenderStepFragmentInterface;
	class BlueNoiseGeneratorTables;

	struct ScreenSpaceReflectionsOperatorDesc
	{
		bool _enableFinalBlur = false;
		bool _splitConfidence = true;

		uint64_t GetHash(uint64_t seed = DefaultSeed64) const;
	};

	class IScreenSpaceReflections
	{
	public:
		struct QualityParameters
		{
			unsigned _mostDetailedMip = 1;
			unsigned _minTraversalOccupancy = 4;
			unsigned _maxTraversalIntersections = 128;

			float _depthBufferThickness = 0.015f;

			bool _temporalVarianceGuidanceEnabled = true;
			unsigned _samplesPerQuad = 4;

			float _temporalStabilityFactor = 0.96f;
			float _temporalVarianceThreshold = 0.002f;

			float _depthSigma = 0.02f;
			float _roughnessSigmaxMin = 0.001f;
			float _roughnessSigmaMax = 0.01f;
		};
		virtual void SetQualityParameters(const QualityParameters&) = 0;
		virtual QualityParameters GetQualityParameters() const = 0;
		virtual ~IScreenSpaceReflections();
	};

	class ScreenSpaceReflectionsOperator : public IScreenSpaceReflections, public std::enable_shared_from_this<ScreenSpaceReflectionsOperator>
	{
	public:
		void Execute(LightingEngine::LightingTechniqueIterator& iterator);

		void SetSpecularIBL(std::shared_ptr<IResourceView>);

		LightingEngine::RenderStepFragmentInterface CreateFragment(const FrameBufferProperties& fbProps);
		void PreregisterAttachments(Techniques::FragmentStitchingContext& stitchingContext);

		void ResetAccumulation();
		::Assets::DependencyValidation GetDependencyValidation() const { assert(_secondStageConstructionState == 2); return _depVal; }

		void SecondStageConstruction(
			std::promise<std::shared_ptr<ScreenSpaceReflectionsOperator>>&& promise,
			const Techniques::FrameBufferTarget& fbTarget);
		void CompleteInitialization(IThreadContext& threadContext);		// must be called after CompleteInitialization()

		void SetQualityParameters(const QualityParameters&) override;
		QualityParameters GetQualityParameters() const override;

		struct IntegrationParams
		{
			bool _specularIBLEnabled = false;
		};

		ScreenSpaceReflectionsOperator(
			std::shared_ptr<Techniques::PipelineCollection> pipelinePool,
			const ScreenSpaceReflectionsOperatorDesc& desc,
			const IntegrationParams& integrationParams);
		~ScreenSpaceReflectionsOperator();
	private:
		ScreenSpaceReflectionsOperatorDesc _desc;
		std::shared_ptr<Techniques::IComputeShaderOperator> _classifyTiles;
		std::shared_ptr<Techniques::IComputeShaderOperator> _prepareIndirectArgs;
		std::shared_ptr<Techniques::IComputeShaderOperator> _intersect;
		std::shared_ptr<Techniques::IComputeShaderOperator> _resolveSpatial;
		std::shared_ptr<Techniques::IComputeShaderOperator> _resolveTemporal;
		std::shared_ptr<Techniques::IComputeShaderOperator> _reflectionsBlur;

		std::shared_ptr<IResourceView> _rayCounterBufferUAV;
		std::shared_ptr<IResourceView> _rayCounterBufferSRV;
		std::shared_ptr<IResourceView> _indirectArgsBufferUAV;
		std::shared_ptr<IResource> _indirectArgsBuffer;
		std::shared_ptr<IResourceView> _skyCubeSRV;

		std::shared_ptr<IResourceView> _paramsBuffer[3];
		unsigned _paramsBufferCounter = 0;
		unsigned _paramsBufferCopyCountdown = 0;
		std::vector<uint8_t> _paramsBufferData;
		QualityParameters _qualityParameters;
		RenderCore::Assets::PredefinedCBLayout _paramsCBLayout;
		IntegrationParams _integrationParams;

		struct ResolutionDependentResources;
		std::unique_ptr<ResolutionDependentResources> _res;
		std::unique_ptr<BlueNoiseGeneratorTables> _blueNoiseRes;
		
		std::shared_ptr<IDevice> _device;
		std::shared_ptr<Techniques::PipelineCollection> _pipelinePool;
		::Assets::DependencyValidation _depVal;
		unsigned _pingPongCounter = ~0u;
		bool _pendingCompleteInit = true;
		unsigned _secondStageConstructionState = 0;		// debug usage only
	};

}}

