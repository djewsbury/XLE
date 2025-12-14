// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "LightingEngine.h"
#include "../Techniques/TechniqueUtils.h"
#include "../Techniques/RenderPass.h"
#include "../Format.h"
#include "../../Math/Vector.h"
#include "../../Utility/IteratorUtils.h"
#include <memory>

namespace RenderCore { namespace Techniques { class ProjectionDesc; class IPipelineAcceleratorPool; }}
namespace RenderCore { class IResourceView; class IThreadContext; }
namespace RenderCore { namespace BufferUploads { using CommandListID = uint32_t; }}
namespace RenderCore { namespace LightingEngine
{
	class IProbeRenderingInstance
	{
	public:
		virtual SequencePlayback::Step GetNextStep() = 0;
		virtual BufferUploads::CommandListID GetRequiredBufferUploadsCommandList() = 0;
		virtual ~IProbeRenderingInstance() = default;
	};

	class ISemiStaticShadowProbeScheduler
	{
	public:
		enum OnFrameBarrierResult { NoChange, QueuedRenders, BackgroundOperationOngoing };
		virtual OnFrameBarrierResult OnFrameBarrier(const Float3& newViewPosition, float drawDistance) = 0;

		virtual std::shared_ptr<IProbeRenderingInstance> BeginPrepare(
			IThreadContext& threadContext, unsigned maxProbeCount) = 0;
		virtual void EndPrepare(IThreadContext& threadContext) = 0;

		virtual void SetNearRadius(float) = 0;
		virtual float GetNearRadius(float) = 0;
		virtual void SetFadeTransition(unsigned) = 0;		// count of frames to transition between active and inactive

		virtual ~ISemiStaticShadowProbeScheduler();
	};

	class LightingEngineApparatus;
	class SharedTechniqueDelegateBox;

	class ShadowProbes
	{
	public:
		struct Probe
		{
			Float3 _position;
			float _nearRadius, _farRadius;
		};

		struct Configuration
		{
			unsigned _faceDims = 256;
			unsigned _maxProbes = 32;
			Format _format = Format::D16_UNORM;

			Techniques::RSDepthBias _singleSidedBias;
			Techniques::RSDepthBias _doubleSidedBias;

			friend bool operator==(const Configuration& lhs, const Configuration& rhs);
		};

		using AABB = std::pair<Float3, Float3>;

		std::shared_ptr<IProbeRenderingInstance> PrepareStaticProbes(
			IThreadContext& threadContext,
			IteratorRange<const std::pair<unsigned, Probe>*> probesAndIndices);

		IResourceView& GetStaticProbesTable() const;
		IResourceView& GetShadowProbeUniforms() const;
		bool IsReady() const;
		unsigned GetReservedProbeCount();

		void CompleteInitialization(IThreadContext& threadContext);

		ShadowProbes(
			std::shared_ptr<Techniques::IPipelineAcceleratorPool> pipelineAccelerators,
			SharedTechniqueDelegateBox& sharedTechniqueDelegate,
			const Configuration& config);

		ShadowProbes(
			LightingEngineApparatus& apparatus,
			const Configuration& config);

		~ShadowProbes();
	private:
		class Pimpl;
		std::unique_ptr<Pimpl> _pimpl;
		class ProbeRenderingInstance;
	};

	class DynamicShadowProbes
	{
	public:
		Techniques::RenderPassInstance Begin(
			Techniques::ParsingContext& parsingContext,
			const ShadowProbes::Probe& probe,
			unsigned probeIndex);		// probeIndex is the index into our table where we're going to write to

		IResourceView& GetDynamicProbesTable() const;
		IResourceView& GetDynamicProbeUniforms() const;
		unsigned GetReservedProbeCount();

		void CompleteInitialization(IThreadContext& threadContext);

		DynamicShadowProbes(
			std::shared_ptr<Techniques::IPipelineAcceleratorPool> pipelineAccelerators,
			SharedTechniqueDelegateBox& sharedTechniqueDelegate,
			const ShadowProbes::Configuration& config);

		DynamicShadowProbes(
			LightingEngineApparatus& apparatus,
			const ShadowProbes::Configuration& config);

	private:
		class Pimpl;
		std::unique_ptr<Pimpl> _pimpl;
	};

}}

