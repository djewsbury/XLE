// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "LightingEngine.h"
#include "../../Math/Vector.h"
#include "../../Utility/IteratorUtils.h"
#include <memory>

namespace RenderCore { namespace Techniques { class ProjectionDesc; }}
namespace RenderCore { class IResourceView; class IThreadContext; }
namespace RenderCore { namespace LightingEngine
{
	class IProbeRenderingInstance
	{
	public:
		virtual LightingTechniqueInstance::Step GetNextStep() = 0;
		virtual ~IProbeRenderingInstance() = default;
	};

	class LightingEngineApparatus;

	class ShadowProbes
	{
	public:
		struct Probe
		{
			Float3 _position;
			float radius;
		};

		struct Configuration
		{
			unsigned _staticFaceDims = 256;
			unsigned _dynamicFaceDims = 128;
			unsigned _maxDynamicProbes = 32;
			Format _staticFormat = Format::D16_UNORM;
		};

		using AABB = std::pair<Float3, Float3>;

		std::shared_ptr<IProbeRenderingInstance> PrepareDynamicProbes(
			IThreadContext& threadContext,
			const Techniques::ProjectionDesc& projDesc,
			IteratorRange<const AABB*> dynamicObjects);
		std::shared_ptr<IProbeRenderingInstance> PrepareStaticProbes(
			IThreadContext& threadContext);
		const IResourceView& GetStaticProbesTable();
		const IResourceView& GetDynamicProbesTable();
		const IResourceView& GetLookupTable();

		ShadowProbes(
			std::shared_ptr<Techniques::IPipelineAcceleratorPool> pipelineAccelerators,
			SharedTechniqueDelegateBox& sharedTechniqueDelegate,
			IteratorRange<const Probe*> probeLocations, const Configuration& config);

		ShadowProbes(
			LightingEngineApparatus& apparatus,
			IteratorRange<const Probe*> probeLocations, const Configuration& config);

		~ShadowProbes();
	// private:
		class Pimpl;
		std::unique_ptr<Pimpl> _pimpl;
	};

}}

