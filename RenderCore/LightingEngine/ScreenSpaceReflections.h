// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "StandardLightOperators.h"
#include "../../Assets/AssetsCore.h"
#include <memory>
#include <future>

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
}}
namespace RenderCore { namespace Assets { class PredefinedCBLayout; } }
namespace RenderCore { namespace BufferUploads { class ResourceLocator; } }

namespace RenderCore { namespace LightingEngine
{
	class LightingTechniqueIterator;
	class RenderStepFragmentInterface;
	class BlueNoiseGeneratorTables;

	class ScreenSpaceReflectionsOperator : public std::enable_shared_from_this<ScreenSpaceReflectionsOperator>
	{
	public:
		void Execute(LightingEngine::LightingTechniqueIterator& iterator);

		void SetSpecularIBL(std::shared_ptr<IResourceView>);

		LightingEngine::RenderStepFragmentInterface CreateFragment(const FrameBufferProperties& fbProps);
		void PreregisterAttachments(Techniques::FragmentStitchingContext& stitchingContext);

		void ResetAccumulation();
		::Assets::DependencyValidation GetDependencyValidation() const { return _depVal; }

		void CompleteInitialization(IThreadContext& threadContext);		// must be called after CompleteInitialization()

		ScreenSpaceReflectionsOperator(
			const ScreenSpaceReflectionsOperatorDesc& desc,
			std::shared_ptr<Techniques::IComputeShaderOperator> classifyTiles,
			std::shared_ptr<Techniques::IComputeShaderOperator> prepareIndirectArgs,
			std::shared_ptr<Techniques::IComputeShaderOperator> intersect,
			std::shared_ptr<Techniques::IComputeShaderOperator> resolveSpatial,
			std::shared_ptr<Techniques::IComputeShaderOperator> resolveTemporal,
			std::shared_ptr<Techniques::IComputeShaderOperator> reflectionsBlur,
			const RenderCore::Assets::PredefinedCBLayout& configCBLayout,
			std::shared_ptr<IDevice> device);
		~ScreenSpaceReflectionsOperator();

		static void ConstructToPromise(
			std::promise<std::shared_ptr<ScreenSpaceReflectionsOperator>>&& promise,
			std::shared_ptr<Techniques::PipelineCollection> pipelinePool,
			const ScreenSpaceReflectionsOperatorDesc& desc);
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

		std::shared_ptr<IResourceView> _configCB;
		std::vector<uint8_t> _configCBData;

		struct ResolutionDependentResources;
		std::unique_ptr<ResolutionDependentResources> _res;
		std::unique_ptr<BlueNoiseGeneratorTables> _blueNoiseRes;
		
		std::shared_ptr<IDevice> _device;
		::Assets::DependencyValidation _depVal;
		unsigned _pingPongCounter = ~0u;
		bool _pendingCompleteInit = true;
	};

}}

