// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "ShadowProbes.h"
#include "LightingEngineApparatus.h"
#include "../Techniques/RenderPass.h"
#include "../Techniques/ParsingContext.h"
#include "../Techniques/Techniques.h"
#include "../Techniques/PipelineAccelerator.h"
#include "../Techniques/DrawableDelegates.h"
#include "../Techniques/Services.h"
#include "../Techniques/SystemUniformsDelegate.h"
#include "../Assets/PredefinedDescriptorSetLayout.h"
#include "../Assets/PredefinedPipelineLayout.h"
#include "../IDevice.h"
#include "../IThreadContext.h"
#include "../../Assets/Assets.h"
#include "../../xleres/FileList.h"

namespace RenderCore { namespace LightingEngine
{
	class ShadowProbes::Pimpl
	{
	public:
		std::shared_ptr<Techniques::IPipelineAcceleratorPool> _pipelineAccelerators;
		std::shared_ptr<IResource> _staticTable;
		std::shared_ptr<IResource> _dynamicTable;
		std::shared_ptr<IResource> _lookupTable;
		std::vector<Probe> _probes;
		Configuration _config;
		std::shared_ptr<Techniques::SequencerConfig> _probePrepareCfg;
		std::shared_ptr<Assets::PredefinedDescriptorSetLayout> _sequencerDescSetLayout;

		struct StaticProbePrepareHelper
		{
			ShadowProbes::Pimpl* _pimpl;
			Techniques::TechniqueContext _techContext;
			std::unique_ptr<Techniques::ParsingContext> _parsingContext;
			
			static const auto semanticProbePrepare = ConstHash64<'prob', 'epre'>::Value;
			StaticProbePrepareHelper(std::shared_ptr<IDevice> device, ShadowProbes::Pimpl& pimpl)
			: _pimpl(&pimpl)
			{
				auto staticDatabaseDesc = TextureDesc::PlainCube(_pimpl->_config._staticFaceDims, _pimpl->_config._staticFaceDims, Format::D16_UNORM);
				staticDatabaseDesc._arrayCount = 6*_pimpl->_probes.size();
				Techniques::PreregisteredAttachment preregisteredAttachments[] {
					semanticProbePrepare,
					CreateDesc(BindFlag::ShaderResource | BindFlag::DepthStencil, 0, 0, staticDatabaseDesc, "probe-prepare")
				};

				_techContext._attachmentPool = std::make_shared<Techniques::AttachmentPool>(device);
				_techContext._frameBufferPool = Techniques::CreateFrameBufferPool();
				_techContext._sequencerDescSetLayout = _pimpl->_sequencerDescSetLayout;
				_techContext._systemUniformsDelegate = std::make_shared<Techniques::SystemUniformsDelegate>(*device);
				_techContext._commonResources = Techniques::Services::GetCommonResources();
				_parsingContext = std::make_unique<Techniques::ParsingContext>(_techContext);
				for (const auto&a:preregisteredAttachments) _parsingContext->GetFragmentStitchingContext().DefineAttachment(a);
			}

			Techniques::RenderPassInstance BeginRPI(IThreadContext& threadContext, unsigned firstSlice, unsigned sliceCount)
			{
				Techniques::FrameBufferDescFragment fragment;
				SubpassDesc sp;
				TextureViewDesc viewDesc;
				viewDesc._arrayLayerRange = {firstSlice, sliceCount};
				sp.SetDepthStencil(fragment.DefineAttachment(semanticProbePrepare, LoadStore::Clear, LoadStore::Retain, 0, BindFlag::ShaderResource), viewDesc);
				sp.SetName("static-shadow-prepare");
				fragment.AddSubpass(std::move(sp));

				return Techniques::RenderPassInstance{threadContext, *_parsingContext, fragment};
			}

			std::shared_ptr<IResource> GetStaticTable()
			{
				return _techContext._attachmentPool->GetBoundResource(semanticProbePrepare);
			}
		};
	};

	static std::vector<Techniques::ProjectionDesc> CreateProjectionDescs(
		IteratorRange<const ShadowProbes::Probe*> probes)
	{
		// Should we consider fewer rendering directions for some probes? 
		std::vector<Techniques::ProjectionDesc> result;
		auto count = probes.size()*6;
		result.reserve(count);
		for (unsigned c=0; c<count; ++c) {
			const auto& p = probes[c/6];
			result.push_back(
				Techniques::BuildCubemapProjectionDesc(
					c%6, p._position, p.radius / 16384.f, p.radius));
		}
		return result;
	}

	class MultiViewUniformsDelegate : public RenderCore::Techniques::IShaderResourceDelegate
	{
	public:
		struct MultiViewProperties
		{
			Float4x4 _worldToProjection[64];
		};
		MultiViewProperties _multProbeProperties;
		unsigned _projectionCount = 0;

		virtual void WriteImmediateData(RenderCore::Techniques::ParsingContext& context, const void* objectContext, unsigned idx, IteratorRange<void*> dst) override
		{
			assert(idx == 0);
			assert(dst.size() == sizeof(Float4x4) * _projectionCount);
			std::memcpy(dst.begin(), &_multProbeProperties, sizeof(Float4x4) * _projectionCount);
		}

		virtual size_t GetImmediateDataSize(RenderCore::Techniques::ParsingContext& context, const void* objectContext, unsigned idx) override
		{
			assert(idx == 0);
			return sizeof(Float4x4) * _projectionCount;
		}

		MultiViewUniformsDelegate(IteratorRange<const Float4x4*> worldToProjections)
		{
			assert(worldToProjections.size() > 0 && worldToProjections.size() <= dimof(_multProbeProperties._worldToProjection));
			_projectionCount = std::min(worldToProjections.size(), dimof(_multProbeProperties._worldToProjection));
			for (unsigned c=0; c<_projectionCount; ++c)
				_multProbeProperties._worldToProjection[c] = worldToProjections[c];
			BindImmediateData(0, Utility::Hash64("MultiViewProperties"));
		}
	};

	class ProbeRenderingInstance : public IProbeRenderingInstance
	{
	public:
		IThreadContext* _threadContext = nullptr;
		unsigned _probeIterator = 0;
		std::vector<Float4x4> _pendingViews;	// candidate for subframe heap
		std::unique_ptr<ShadowProbes::Pimpl::StaticProbePrepareHelper> _static;
		ShadowProbes::Pimpl* _pimpl = nullptr;
		Techniques::DrawablesPacket _drawablePkt;

		LightingTechniqueInstance::Step GetNextStep() override
		{
			if (_static) {
				if (!_pendingViews.empty()) {
					// Commit the objects that were prepared for rendering
					if (!_drawablePkt._drawables.empty()) {
						auto srDel = std::make_shared<MultiViewUniformsDelegate>(MakeIteratorRange(_pendingViews));
						_static->_parsingContext->AddShaderResourceDelegate(srDel);
						auto rpi = _static->BeginRPI(*_threadContext, _probeIterator*6, _pendingViews.size());
						Techniques::Draw(
							*_threadContext, *_static->_parsingContext, *_pimpl->_pipelineAccelerators, 
							*_pimpl->_probePrepareCfg, _drawablePkt);
						_static->_parsingContext->RemoveShaderResourceDelegate(*srDel);
						_drawablePkt.Reset();
					}
					_probeIterator += _pendingViews.size()/6;
					_pendingViews.clear();
				}

				auto probeCount = _pimpl->_probes.size();
				const size_t maxProbesPerBatch = 5;		// ie, 30 slices of the array texture per batch
				auto nextBatchCount = std::min(probeCount -_probeIterator, maxProbesPerBatch);
				if (!nextBatchCount) {
					// Completed all of the probes
					_pimpl->_staticTable = _static->GetStaticTable();
					return { LightingEngine::StepType::None };
				}
				LightingTechniqueInstance::Step result;
				result._type = LightingEngine::StepType::MultiViewParseScene;
				result._multiViewDesc = CreateProjectionDescs(
					MakeIteratorRange(_pimpl->_probes.begin()+_probeIterator, _pimpl->_probes.begin()+_probeIterator+nextBatchCount));
				result._pkt = &_drawablePkt;
				_pendingViews.reserve(result._multiViewDesc.size());
				for (const auto&v:result._multiViewDesc) _pendingViews.push_back(v._worldToProjection);
				return result;
			} else {
				return { LightingEngine::StepType::None };
			}
		}
	};

	std::shared_ptr<IProbeRenderingInstance> ShadowProbes::PrepareDynamicProbes(
		IThreadContext& threadContext,
		const Techniques::ProjectionDesc& projDesc,
		IteratorRange<const AABB*> dynamicObjects)
	{
		return nullptr;
	}

	std::shared_ptr<IProbeRenderingInstance> ShadowProbes::PrepareStaticProbes(IThreadContext& threadContext)
	{
		if (_pimpl->_probes.empty())
			return nullptr;

		auto result = std::make_shared<ProbeRenderingInstance>();
		result->_threadContext = &threadContext;
		result->_pimpl = _pimpl.get();
		result->_static = std::make_unique<ShadowProbes::Pimpl::StaticProbePrepareHelper>(threadContext.GetDevice(), *_pimpl);
		return result;
	}

	ShadowProbes::ShadowProbes(
		std::shared_ptr<Techniques::IPipelineAcceleratorPool> pipelineAccelerators,
		SharedTechniqueDelegateBox& sharedTechniqueDelegate,
		IteratorRange<const Probe*> probeLocations, const Configuration& config)
	{
		_pimpl = std::make_unique<Pimpl>();
		_pimpl->_config = config;
		_pimpl->_probes.insert(_pimpl->_probes.end(), probeLocations.begin(), probeLocations.end());
		_pimpl->_pipelineAccelerators = std::move(pipelineAccelerators);

		auto descSetLayoutFuture = ::Assets::MakeAsset<RenderCore::Assets::PredefinedPipelineLayoutFile>(SEQUENCER_DS);
		descSetLayoutFuture->StallWhilePending();
		auto descSetLayoutContainer = descSetLayoutFuture->Actualize();
		auto i = descSetLayoutContainer->_descriptorSets.find("Sequencer");
		if (i == descSetLayoutContainer->_descriptorSets.end())
			Throw(std::runtime_error("Missing 'Sequencer' descriptor set entry in sequencer pipeline file"));
		_pimpl->_sequencerDescSetLayout = i->second;

		{
			// Create the pipeline accelerator configuration
			AttachmentDesc attachmentDesc { _pimpl->_config._staticFormat, 0, LoadStore::Clear, LoadStore::Retain, 0, BindFlag::ShaderResource };
			SubpassDesc spDesc;
			spDesc.SetDepthStencil(0);
			FrameBufferDesc fbDesc {
				std::vector<AttachmentDesc>{attachmentDesc},
				std::vector<SubpassDesc>{spDesc}};
			_pimpl->_probePrepareCfg = _pimpl->_pipelineAccelerators->CreateSequencerConfig(
				sharedTechniqueDelegate.GetShadowGenTechniqueDelegate(Techniques::ShadowGenType::VertexIdViewInstancing),
				{}, fbDesc, 0);
		}
	}

	ShadowProbes::ShadowProbes(
		LightingEngineApparatus& apparatus,
		IteratorRange<const Probe*> probeLocations, const Configuration& config)
	: ShadowProbes(apparatus._pipelineAccelerators, *apparatus._sharedDelegates, probeLocations, config)
	{}

	ShadowProbes::~ShadowProbes()
	{}

}}
