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
#include "../Techniques/DrawableDelegates.h"
#include "../Techniques/Drawables.h"
#include "../Assets/PredefinedDescriptorSetLayout.h"
#include "../Assets/PredefinedPipelineLayout.h"
#include "../Metal/DeviceContext.h"
#include "../IDevice.h"
#include "../../Assets/Assets.h"
#include "../../xleres/FileList.h"

namespace RenderCore { namespace Techniques { class IDeformAcceleratorPool; }}

namespace RenderCore { namespace LightingEngine
{
	struct CB_StaticShadowProbeDesc
	{
		float _miniProjZ, _miniProjW;
	};

	constexpr size_t s_maxProbesPerBatch = 5;		// ie, 30 slices of the array texture per batch

	class MultiViewUniformsDelegate : public RenderCore::Techniques::IShaderResourceDelegate
	{
	public:
		struct MultiViewProperties
		{
			Float4x4 _worldToProjection[s_maxProbesPerBatch*6];
		};
		MultiViewProperties _multProbeProperties;
		unsigned _projectionCount = 0;

		virtual void WriteImmediateData(RenderCore::Techniques::ParsingContext& context, const void* objectContext, unsigned idx, IteratorRange<void*> dst) override
		{
			assert(idx == 0);
			assert(dst.size() >= sizeof(Float4x4) * _projectionCount);
			std::memcpy(dst.begin(), &_multProbeProperties, sizeof(Float4x4) * _projectionCount);
		}

		virtual size_t GetImmediateDataSize(RenderCore::Techniques::ParsingContext& context, const void* objectContext, unsigned idx) override
		{
			assert(idx == 0);
			return sizeof(Float4x4) * dimof(_multProbeProperties._worldToProjection);
		}

		void SetWorldToProjections(IteratorRange<const Float4x4*> worldToProjections)
		{
			assert(worldToProjections.size() > 0 && worldToProjections.size() <= dimof(_multProbeProperties._worldToProjection));
			_projectionCount = std::min(worldToProjections.size(), dimof(_multProbeProperties._worldToProjection));
			for (unsigned c=0; c<_projectionCount; ++c)
				_multProbeProperties._worldToProjection[c] = worldToProjections[c];
		}

		MultiViewUniformsDelegate()
		{
			BindImmediateData(0, Utility::Hash64("MultiViewProperties"));
		}
	};

	class ShadowProbes::Pimpl
	{
	public:
		std::shared_ptr<Techniques::IPipelineAcceleratorPool> _pipelineAccelerators;
		std::shared_ptr<IResource> _staticTable, _probeUniforms;
		std::shared_ptr<IResourceView> _staticTableSRV, _probeUniformsUAV;
		std::vector<Probe> _probes;
		Configuration _config;
		std::shared_ptr<Techniques::SequencerConfig> _probePrepareCfg;
		std::shared_ptr<Assets::PredefinedDescriptorSetLayout> _sequencerDescSetLayout;
		std::shared_ptr<MultiViewUniformsDelegate> _multiViewUniformsDelegate;
		std::shared_ptr<Techniques::IDeformAcceleratorPool> _deformAccelerators;
		std::atomic<bool> _activeUpdate;
		bool _pendingClearOfProbeUniforms = true;

		struct StaticProbePrepareHelper
		{
			ShadowProbes::Pimpl* _pimpl;
			Techniques::TechniqueContext _techContext;
			std::unique_ptr<Techniques::ParsingContext> _parsingContext;
			
			static const auto semanticProbePrepare = ConstHash64<'prob', 'epre'>::Value;
			StaticProbePrepareHelper(IThreadContext& threadContext, ShadowProbes::Pimpl& pimpl)
			: _pimpl(&pimpl)
			{
				auto staticDatabaseDesc = TextureDesc::PlainCube(_pimpl->_config._staticFaceDims, _pimpl->_config._staticFaceDims, Format::D16_UNORM);
				staticDatabaseDesc._arrayCount = 6*_pimpl->_probes.size();
				Techniques::PreregisteredAttachment preregisteredAttachments[] {
					semanticProbePrepare,
					CreateDesc(BindFlag::ShaderResource | BindFlag::DepthStencil, staticDatabaseDesc, "probe-prepare")
				};

				_techContext._attachmentPool = std::make_shared<Techniques::AttachmentPool>(threadContext.GetDevice());
				_techContext._frameBufferPool = Techniques::CreateFrameBufferPool();
				auto uniformDelegateMan = Techniques::CreateUniformDelegateManager();
				uniformDelegateMan->AddShaderResourceDelegate(std::make_shared<Techniques::SystemUniformsDelegate>(*threadContext.GetDevice()));
				uniformDelegateMan->AddShaderResourceDelegate(_pimpl->_multiViewUniformsDelegate);
				uniformDelegateMan->AddSemiConstantDescriptorSet(Hash64("Sequencer"), *_pimpl->_sequencerDescSetLayout, *threadContext.GetDevice());
				_techContext._uniformDelegateManager = uniformDelegateMan;
				_techContext._commonResources = Techniques::Services::GetCommonResources();
				_techContext._pipelineAccelerators = _pimpl->_pipelineAccelerators;
				_parsingContext = std::make_unique<Techniques::ParsingContext>(_techContext, threadContext);
				_parsingContext->SetPipelineAcceleratorsVisibility(_techContext._pipelineAccelerators->VisibilityBarrier());
				for (const auto&a:preregisteredAttachments) _parsingContext->GetFragmentStitchingContext().DefineAttachment(a);
			}

			Techniques::RenderPassInstance BeginRPI(unsigned firstSlice, unsigned sliceCount)
			{
				Techniques::FrameBufferDescFragment fragment;
				SubpassDesc sp;
				TextureViewDesc viewDesc;
				viewDesc._arrayLayerRange = {firstSlice, sliceCount};
				sp.SetDepthStencil(fragment.DefineAttachment(semanticProbePrepare).Clear().FinalState(BindFlag::ShaderResource), viewDesc);
				sp.SetName("static-shadow-prepare");
				fragment.AddSubpass(std::move(sp));

				_techContext._attachmentPool->Bind(semanticProbePrepare, _pimpl->_staticTable);

				Techniques::RenderPassBeginDesc beginInfo;
				return Techniques::RenderPassInstance{*_parsingContext, fragment, beginInfo};
			}
		};
	};

	static void WriteProjectionDescs(
		Techniques::ProjectionDesc* dst,
		IteratorRange<const ShadowProbes::Probe*> probes)
	{
		// Should we consider fewer rendering directions for some probes? 
		auto count = probes.size()*6;
		for (unsigned c=0; c<count; ++c) {
			const auto& p = probes[c/6];
			float near_ = p._nearRadius;
			float far_ = p._farRadius;
			assert(near_ > 0 && far_ > 0);
			dst[c] = Techniques::BuildCubemapProjectionDesc(c%6, p._position, near_, far_);
		}
	}

	static void WriteStaticShadowProbeTable(IThreadContext& threadContext, IResource& dst, IteratorRange<const ShadowProbes::Probe*> probes)
	{
		VLA(CB_StaticShadowProbeDesc, probeUniforms, probes.size()*6);
		VLA_UNSAFE_FORCE(Techniques::ProjectionDesc, projDescs, probes.size()*6);
		WriteProjectionDescs(projDescs, probes);
		for (unsigned c=0; c<probes.size()*6; ++c) {
			auto miniProj = ExtractMinimalProjection(projDescs[c]._cameraToProjection);
			probeUniforms[c] = CB_StaticShadowProbeDesc{miniProj[2], miniProj[3]};
		}
		Metal::DeviceContext::Get(threadContext)->BeginBlitEncoder().Write(dst, MakeIteratorRange(probeUniforms, probeUniforms+(probes.size()*6)));
	}

	class ShadowProbes::ProbeRenderingInstance : public IProbeRenderingInstance
	{
	public:
		unsigned _probeIterator = 0;
		std::vector<Float4x4> _pendingViews;
		std::unique_ptr<ShadowProbes::Pimpl::StaticProbePrepareHelper> _staticPrepareHelper;
		ShadowProbes::Pimpl* _pimpl = nullptr;
		Techniques::DrawablesPacket _drawablePkt;
		std::vector<std::pair<unsigned, Probe>> _probesToRender;

		LightingTechniqueInstance::Step GetNextStep() override
		{
			if (_staticPrepareHelper) {
				if (!_pendingViews.empty()) {
					// Commit the objects that were prepared for rendering
					if (!_drawablePkt._drawables.empty()) {

						YieldForRequiredResources();

						_pimpl->_multiViewUniformsDelegate->SetWorldToProjections(MakeIteratorRange(_pendingViews));
						_staticPrepareHelper->_parsingContext->GetUniformDelegateManager()->InvalidateUniforms();
						auto rpi = _staticPrepareHelper->BeginRPI(_probesToRender[_probeIterator].first*6, _pendingViews.size());
						TRY {
							Techniques::Draw(
								*_staticPrepareHelper->_parsingContext, *_pimpl->_pipelineAccelerators,
								*_pimpl->_probePrepareCfg, _drawablePkt);
						} CATCH (...) {
						} CATCH_END
						_drawablePkt.Reset();

						#if defined(_DEBUG)
							auto staticTable = rpi.GetDepthStencilAttachmentResource();
							assert(_pimpl->_staticTable == staticTable);
						#endif
					}
					_probeIterator += _pendingViews.size()/6;
					_pendingViews.clear();
				}

				auto probeCount = _probesToRender.size();
				auto nextBatchCount = std::min(probeCount -_probeIterator, s_maxProbesPerBatch);
				if (!nextBatchCount) {
					// Completed all of the probes
					UpdateUniforms(_staticPrepareHelper->_parsingContext->GetThreadContext());
					return { LightingEngine::StepType::None };
				}
				LightingTechniqueInstance::Step result;
				result._type = LightingEngine::StepType::MultiViewParseScene;
				VLA_UNSAFE_FORCE(Probe, probesThisStep, nextBatchCount);
				for (unsigned p=0; p<nextBatchCount; ++p) probesThisStep[p] = _probesToRender[_probeIterator+p].second;
				result._multiViewDesc.resize(nextBatchCount*6);
				WriteProjectionDescs(result._multiViewDesc.data(), MakeIteratorRange(probesThisStep, probesThisStep+nextBatchCount));
				result._pkts.resize((unsigned)Techniques::Batch::Max);
				result._pkts[(unsigned)Techniques::Batch::Opaque] = &_drawablePkt;
				_pendingViews.reserve(result._multiViewDesc.size());
				for (const auto&v:result._multiViewDesc) _pendingViews.push_back(v._worldToProjection);
				return result;
			} else {
				return { LightingEngine::StepType::None };
			}
		}

		virtual BufferUploads::CommandListID GetRequiredBufferUploadsCommandList() override
		{
			return _staticPrepareHelper->_parsingContext->_requiredBufferUploadsCommandList;
		}

		void UpdateUniforms(IThreadContext& threadContext)
		{
			for (const auto&p:_probesToRender)
				_pimpl->_probes[p.first] = p.second;
			WriteStaticShadowProbeTable(threadContext, *_pimpl->_probeUniforms, _pimpl->_probes);
		}

		void YieldForRequiredResources()
		{
			// wait for resources (shaders, etc)
			std::promise<Techniques::PreparedResourcesVisibility> preparePromise;
			auto prepareFuture = preparePromise.get_future();
			Techniques::PrepareResources(std::move(preparePromise), *_pimpl->_pipelineAccelerators, *_pimpl->_probePrepareCfg, _drawablePkt);
			YieldToPool(prepareFuture);
			auto requiredVisibility = prepareFuture.get();

			// update parsing context with required visibility
			auto currentVisibilityBarrier = _pimpl->_pipelineAccelerators->VisibilityBarrier(requiredVisibility._pipelineAcceleratorsVisibility);
			_staticPrepareHelper->_parsingContext->SetPipelineAcceleratorsVisibility(currentVisibilityBarrier);
			_staticPrepareHelper->_parsingContext->RequireCommandList(requiredVisibility._bufferUploadsVisibility);
		}

		ProbeRenderingInstance(ProbeRenderingInstance&&) = delete;
		ProbeRenderingInstance&operator=(ProbeRenderingInstance&&) = delete;
		ProbeRenderingInstance() = default;
		~ProbeRenderingInstance()
		{
			if (_pimpl) {
				auto prevActiveUpdate = _pimpl->_activeUpdate.exchange(false);
				assert(prevActiveUpdate);
			}
		}
	};

	unsigned ShadowProbes::GetReservedProbeCount()
	{
		return _pimpl->_probes.size();
	}

	std::shared_ptr<IProbeRenderingInstance> ShadowProbes::PrepareStaticProbes(IThreadContext& threadContext, IteratorRange<const std::pair<unsigned, Probe>*> probesAndIndices)
	{
		if (probesAndIndices.empty())
			return nullptr;

		for (const auto& p:probesAndIndices) assert(p.first < _pimpl->_probes.size());

		auto result = std::make_shared<ProbeRenderingInstance>();
		result->_pimpl = _pimpl.get();
		result->_staticPrepareHelper = std::make_unique<ShadowProbes::Pimpl::StaticProbePrepareHelper>(threadContext, *_pimpl);
		result->_probesToRender.insert(result->_probesToRender.end(), probesAndIndices.begin(), probesAndIndices.end());
		auto prevActiveUpdate = _pimpl->_activeUpdate.exchange(true);
		assert(!prevActiveUpdate);
		return result;
	}

	IResourceView& ShadowProbes::GetStaticProbesTable() const
	{
		assert(_pimpl->_staticTableSRV);
		return *_pimpl->_staticTableSRV;
	}

	IResourceView& ShadowProbes::GetShadowProbeUniforms() const
	{
		assert(_pimpl->_probeUniformsUAV);
		return *_pimpl->_probeUniformsUAV;
	}

	bool ShadowProbes::IsReady() const
	{
		return true;
	}

	void ShadowProbes::CompleteInitialization(IThreadContext& threadContext)
	{
		auto tableRes = _pimpl->_staticTable.get();
		Metal::CompleteInitialization(*Metal::DeviceContext::Get(threadContext), MakeIteratorRange(&tableRes, &tableRes+1));

		if (_pimpl->_pendingClearOfProbeUniforms) {
			auto probeUniformsSize = sizeof(CB_StaticShadowProbeDesc)*6*_pimpl->_config._maxStaticProbes;
			VLA(uint8_t, blank, probeUniformsSize);
			std::memset(blank, 0, probeUniformsSize);
			Metal::DeviceContext::Get(threadContext)->BeginBlitEncoder().Write(*_pimpl->_probeUniforms, MakeIteratorRange(blank, blank+probeUniformsSize));
			_pimpl->_pendingClearOfProbeUniforms = false;
		}
	}

	ShadowProbes::ShadowProbes(
		std::shared_ptr<Techniques::IPipelineAcceleratorPool> pipelineAccelerators,
		SharedTechniqueDelegateBox& sharedTechniqueDelegate,
		const Configuration& config)
	{
		_pimpl = std::make_unique<Pimpl>();
		_pimpl->_config = config;
		_pimpl->_pipelineAccelerators = std::move(pipelineAccelerators);
		_pimpl->_multiViewUniformsDelegate = std::make_shared<MultiViewUniformsDelegate>();

		auto descSetLayoutContainer = ::Assets::ActualizeAssetPtr<RenderCore::Assets::PredefinedPipelineLayoutFile>(SEQUENCER_DS);
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

			// Coordinate space for cubemap rendering is defined by the API to make shader lookups simple
			// However, if it's not the same as our typical conventions, we may need to flip the winding
			// direction
			bool flipCulling = Techniques::GetGeometricCoordinateSpaceForCubemaps() != GeometricCoordinateSpace::RightHanded;
			_pimpl->_probePrepareCfg = _pimpl->_pipelineAccelerators->CreateSequencerConfig(
				"shadow-probe",
				sharedTechniqueDelegate.GetShadowGenTechniqueDelegate(
					Techniques::ShadowGenType::VertexIdViewInstancing, 
					_pimpl->_config._singleSidedBias, 
					_pimpl->_config._doubleSidedBias, 
					CullMode::Back, flipCulling ? FaceWinding::CW : FaceWinding::CCW),
				{}, fbDesc, 0);
		}

		_pimpl->_probes.resize(config._maxStaticProbes, Probe{Zero<Float3>(), 1.f, 1024.f});

		auto staticDatabaseDesc = TextureDesc::PlainCube(_pimpl->_config._staticFaceDims, _pimpl->_config._staticFaceDims, Format::D16_UNORM);
		staticDatabaseDesc._arrayCount = 6*_pimpl->_config._maxStaticProbes;
		auto device = _pimpl->_pipelineAccelerators->GetDevice().get();
		_pimpl->_staticTable = device->CreateResource(CreateDesc(BindFlag::ShaderResource | BindFlag::DepthStencil | BindFlag::TransferDst, staticDatabaseDesc, "probe-prepare"));
		_pimpl->_staticTableSRV = _pimpl->_staticTable->CreateTextureView(BindFlag::ShaderResource);

		_pimpl->_probeUniforms = device->CreateResource(
			CreateDesc(BindFlag::UnorderedAccess|BindFlag::TransferDst, LinearBufferDesc::Create(sizeof(CB_StaticShadowProbeDesc)*staticDatabaseDesc._arrayCount, sizeof(CB_StaticShadowProbeDesc)), "shadow-probe-list"));
		_pimpl->_probeUniformsUAV = _pimpl->_probeUniforms->CreateBufferView(BindFlag::UnorderedAccess);
	}

	ShadowProbes::ShadowProbes(LightingEngineApparatus& apparatus, const Configuration& config)
	: ShadowProbes(apparatus._pipelineAccelerators, *apparatus._sharedDelegates, config)
	{}

	ShadowProbes::~ShadowProbes()
	{}

	bool operator==(const ShadowProbes::Configuration& lhs, const ShadowProbes::Configuration& rhs)
	{
		return lhs._staticFaceDims == rhs._staticFaceDims
		 	&& lhs._dynamicFaceDims == rhs._dynamicFaceDims
			&& lhs._maxDynamicProbes == rhs._maxDynamicProbes
			&& lhs._staticFormat == rhs._staticFormat
			&& lhs._singleSidedBias._slopeScaledBias == rhs._singleSidedBias._slopeScaledBias
			&& lhs._singleSidedBias._depthBiasClamp == rhs._singleSidedBias._depthBiasClamp
			&& lhs._singleSidedBias._depthBias == rhs._singleSidedBias._depthBias
			&& lhs._doubleSidedBias._slopeScaledBias == rhs._doubleSidedBias._slopeScaledBias
			&& lhs._doubleSidedBias._depthBiasClamp == rhs._doubleSidedBias._depthBiasClamp
			&& lhs._doubleSidedBias._depthBias == rhs._doubleSidedBias._depthBias
			;
	}

	ISemiStaticShadowProbeScheduler::~ISemiStaticShadowProbeScheduler() {}

}}
