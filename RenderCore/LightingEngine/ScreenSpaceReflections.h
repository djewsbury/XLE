// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include <memory>
#include "../../Assets/AssetsCore.h"

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
	class PipelinePool;
	class DeferredShaderResource;
}}
namespace BufferUploads { using CommandListID = uint32_t; }

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

		void CompleteInitialization(IThreadContext& threadContext);

		ScreenSpaceReflectionsOperator(
			std::shared_ptr<Techniques::IComputeShaderOperator> classifyTiles,
			std::shared_ptr<Techniques::IComputeShaderOperator> prepareIndirectArgs,
			std::shared_ptr<Techniques::IComputeShaderOperator> intersect,
			std::shared_ptr<Techniques::IComputeShaderOperator> resolveSpatial,
			std::shared_ptr<Techniques::IComputeShaderOperator> resolveTemporal,
			std::shared_ptr<Techniques::IComputeShaderOperator> reflectionsBlur,
			std::shared_ptr<IDevice> device);
		~ScreenSpaceReflectionsOperator();

		static void ConstructToFuture(
			::Assets::FuturePtr<ScreenSpaceReflectionsOperator>& future,
			std::shared_ptr<Techniques::PipelinePool> pipelinePool);
	private:
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

		class ResolutionDependentResources;
		std::unique_ptr<ResolutionDependentResources> _res;
		std::unique_ptr<BlueNoiseGeneratorTables> _blueNoiseRes;
		
		std::shared_ptr<IResourceView> _dummyCube;

		std::shared_ptr<IDevice> _device;
		::Assets::DependencyValidation _depVal;
		unsigned _pingPongCounter = ~0u;
	};

}}

