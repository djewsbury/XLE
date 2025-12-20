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
#include "RenderCore/Techniques/DrawableDelegates.h"
#include <memory>

namespace RenderCore { namespace Techniques { class ProjectionDesc; class IPipelineAcceleratorPool; class SequencerConfig; }}
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
			Float3x4 _objectToWorld;			// for cubemap probes, only the translation is used
			float _nearRadius, _farRadius;
			float _fov;							// only when writing single projection probes (eg, cone lights)		
			TextureDesc::Dimensionality _dimensionality;		// should be cube or 2d
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

		IResourceView& GetStaticProbeTable() const;
		IResourceView& GetShadowProbeUniforms() const;
		bool IsReady() const;
		unsigned GetReservedProbeCount() const;
		const Configuration& GetConfiguration() const;

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
			IteratorRange<const Techniques::ProjectionDesc*> multiViewDesc,
			unsigned firstFaceIndex);		// firstFaceIndex is the index into our table where we're going to write to

		void Bind(Techniques::ParsingContext& parsingContext);
		void Unbind(Techniques::ParsingContext& parsingContext, IteratorRange<const ShadowProbes::Probe*> updatedUniformState);
		void BarrierToReadingLayout(Techniques::ParsingContext&);

		IResourceView& GetDynamicProbeTable() const;
		IResourceView& GetDynamicProbeUniforms() const;
		unsigned GetReservedFaceCount() const;
		const ShadowProbes::Configuration& GetConfiguration() const;

		Techniques::SequencerConfig* GetSequencerConfig() const;

		void CompleteInitialization(IThreadContext& threadContext);

		DynamicShadowProbes(
			std::shared_ptr<Techniques::IPipelineAcceleratorPool>,
			SharedTechniqueDelegateBox& sharedTechniqueDelegate,
			const ShadowProbes::Configuration& config);

		DynamicShadowProbes(
			LightingEngineApparatus& apparatus,
			const ShadowProbes::Configuration& config);

		~DynamicShadowProbes();

	private:
		class Pimpl;
		std::unique_ptr<Pimpl> _pimpl;
	};

	void WriteProjectionDescs(
		std::vector<Techniques::ProjectionDesc>& dst,
		IteratorRange<const ShadowProbes::Probe*> probes);

}}

