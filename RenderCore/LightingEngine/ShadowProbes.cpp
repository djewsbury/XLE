// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "ShadowProbes.h"
#include "Core/Prefix.h"
#include "LightingEngineApparatus.h"
#include "Math/XLEMath.h"
#include "RenderCore/ResourceDesc.h"
#include "RenderCore/Vulkan/Metal/DeviceContext.h"
#include "SequenceIterator.h"
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
#include "../../Math/Transformations.h"
#include "../../Assets/Assets.h"
#include "../../Assets/Continuation.h"
#include "../../xleres/FileList.h"
#include <future>

using namespace Utility::Literals;

namespace RenderCore { namespace Techniques { class IDeformAcceleratorPool; }}

namespace RenderCore { namespace LightingEngine
{
	struct CB_ShadowProbeDesc
	{
		float _miniProjZ, _miniProjW;
	};

	static constexpr auto s_semanticProbePrepare = "probe-prepare"_h;

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
			_projectionCount = (unsigned)std::min(worldToProjections.size(), dimof(_multProbeProperties._worldToProjection));
			for (unsigned c=0; c<_projectionCount; ++c)
				_multProbeProperties._worldToProjection[c] = worldToProjections[c];
		}

		MultiViewUniformsDelegate()
		{
			BindImmediateData(0, "MultiViewProperties"_h);
		}
	};

	static ::Assets::MarkerPtr<Techniques::SequencerConfig> CreateProbePrepareCfg(
		std::shared_ptr<Techniques::IPipelineAcceleratorPool>, SharedTechniqueDelegateBox&, const ShadowProbes::Configuration&);

	class ShadowProbes::Pimpl
	{
	public:
		std::shared_ptr<Techniques::IPipelineAcceleratorPool> _pipelineAccelerators;
		std::shared_ptr<IResource> _staticTable, _probeUniforms;
		std::shared_ptr<IResourceView> _staticTableSRV, _probeUniformsUAV;
		std::vector<Probe> _probes;
		Configuration _config;
		std::shared_future<std::shared_ptr<Techniques::SequencerConfig>> _probePrepareCfg;
		std::shared_ptr<Assets::PredefinedDescriptorSetLayout> _sequencerDescSetLayout;
		std::string _sequencerDescSetLayoutName;
		std::shared_ptr<MultiViewUniformsDelegate> _multiViewUniformsDelegate;
		std::shared_ptr<Techniques::IDeformAcceleratorPool> _deformAccelerators;
		std::atomic<bool> _activeUpdate;
		bool _pendingClearOfProbeUniforms = true;
		bool _pendingStaticTableInit = true;

		struct StaticProbePrepareHelper
		{
			ShadowProbes::Pimpl* _pimpl;
			Techniques::TechniqueContext _techContext;
			std::unique_ptr<Techniques::ParsingContext> _parsingContext;
			
			StaticProbePrepareHelper(IThreadContext& threadContext, ShadowProbes::Pimpl& pimpl)
			: _pimpl(&pimpl)
			{
				// _techContext._attachmentPool = Techniques::CreateAttachmentPool(threadContext.GetDevice());		(can we get away without this?)
				_techContext._frameBufferPool = Techniques::CreateFrameBufferPool();
				_techContext._systemUniformsDelegate = std::make_shared<Techniques::SystemUniformsDelegate>(*threadContext.GetDevice());
				_techContext._graphicsSequencerDS = Techniques::CreateSemiConstantDescriptorSet(*_pimpl->_sequencerDescSetLayout, _pimpl->_sequencerDescSetLayoutName, PipelineType::Graphics, *threadContext.GetDevice());
				_techContext._computeSequencerDS = Techniques::CreateSemiConstantDescriptorSet(*_pimpl->_sequencerDescSetLayout, _pimpl->_sequencerDescSetLayoutName, PipelineType::Compute, *threadContext.GetDevice());
				_techContext._commonResources = Techniques::Services::GetCommonResources();
				_techContext._pipelineAccelerators = _pimpl->_pipelineAccelerators;

				_parsingContext = std::make_unique<Techniques::ParsingContext>(_techContext, threadContext);
				_parsingContext->SetPipelineAcceleratorsVisibility(_techContext._pipelineAccelerators->VisibilityBarrier());
				_parsingContext->GetUniformDelegateManager()->BindShaderResourceDelegate(_pimpl->_multiViewUniformsDelegate);
				_parsingContext->BindAttachment(s_semanticProbePrepare, _pimpl->_staticTable, false, ~0u);
			}

			Techniques::RenderPassInstance BeginRPI(unsigned firstSlice, unsigned sliceCount)
			{
				Techniques::FrameBufferDescFragment fragment;
				SubpassDesc sp;
				TextureViewDesc viewDesc;
				viewDesc._arrayLayerRange = {firstSlice, sliceCount};
				sp.SetDepthStencil(fragment.DefineAttachment(s_semanticProbePrepare).Clear().FinalState(BindFlag::ShaderResource), viewDesc);
				sp.SetName("static-shadow-prepare");
				fragment.AddSubpass(std::move(sp));

				Techniques::RenderPassBeginDesc beginInfo;
				return Techniques::RenderPassInstance{*_parsingContext, fragment, beginInfo};
			}
		};
	};

	void WriteProjectionDescs(
		std::vector<Techniques::ProjectionDesc>& dst,
		IteratorRange<const ShadowProbes::Probe*> probes)
	{
		// Should we consider fewer rendering directions for some probes? 
		for (auto& p:probes) {
			assert(p._nearRadius > 0 && p._farRadius > 0);
			if (p._dimensionality == TextureDesc::Dimensionality::CubeMap) {
				for (unsigned c=0; c<6; ++c)
					dst.emplace_back(Techniques::BuildCubemapProjectionDesc(c, ExtractTranslation(p._objectToWorld), p._nearRadius, p._farRadius));
			} else if (p._dimensionality == TextureDesc::Dimensionality::T2D) {
				assert(p._fov > 0);
				// note that we have to shift the matrix around to convert from object-to-world into camera-to-world style
				Techniques::CameraDesc cameraDesc;
				cameraDesc._cameraToWorld = Float4x4 {
					p._objectToWorld(0, 0), p._objectToWorld(0, 2), -p._objectToWorld(0, 1), p._objectToWorld(0, 3),
					p._objectToWorld(1, 0), p._objectToWorld(1, 2), -p._objectToWorld(1, 1), p._objectToWorld(1, 3),
					p._objectToWorld(2, 0), p._objectToWorld(2, 2), -p._objectToWorld(2, 1), p._objectToWorld(2, 3),
					0.f, 0.f, 0.f, 1.f };
				cameraDesc._nearClip = p._nearRadius; cameraDesc._farClip = p._farRadius;
				cameraDesc._projection = Techniques::CameraDesc::Projection::Perspective;
				cameraDesc._verticalFieldOfView = p._fov;
				dst.emplace_back(Techniques::BuildProjectionDesc(cameraDesc, 1.f));
			} else {
				assert(0);		// invalid dimensionality specified
			}	
		}
	}

	static void WriteStaticShadowProbeTable(IThreadContext& threadContext, IResource& dst, IteratorRange<const ShadowProbes::Probe*> probes)
	{
		VLA(CB_ShadowProbeDesc, probeUniforms, probes.size()*6);
		std::vector<Techniques::ProjectionDesc> projDescs; projDescs.reserve(probes.size()*6);		// subframe allocation candidate
		WriteProjectionDescs(projDescs, probes);
		for (unsigned c=0; c<probes.size()*6; ++c) {
			auto miniProj = ExtractMinimalProjection(projDescs[c]._cameraToProjection);
			probeUniforms[c] = CB_ShadowProbeDesc{miniProj[2], miniProj[3]};
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

		SequencePlayback::Step GetNextStep() override
		{
			if (_staticPrepareHelper) {
				if (!_pendingViews.empty()) {
					// Commit the objects that were prepared for rendering
					if (!_drawablePkt._drawables.empty()) {

						YieldForRequiredResources();
						assert(_pimpl->_probePrepareCfg.wait_for(std::chrono::seconds(0)) == std::future_status::ready);	// we wait for this in YieldForRequiredResources

						_pimpl->_multiViewUniformsDelegate->SetWorldToProjections(MakeIteratorRange(_pendingViews));
						_staticPrepareHelper->_parsingContext->GetUniformDelegateManager()->InvalidateUniforms();
						auto rpi = _staticPrepareHelper->BeginRPI(_probesToRender[_probeIterator].first*6, (unsigned)_pendingViews.size());
						TRY {
							Techniques::Draw(
								*_staticPrepareHelper->_parsingContext, *_pimpl->_pipelineAccelerators,
								*_pimpl->_probePrepareCfg.get(), _drawablePkt);
						} CATCH (...) {
						} CATCH_END
						_drawablePkt.Reset();

						#if defined(_DEBUG)
							auto staticTable = rpi.GetDepthStencilAttachmentResource();
							assert(_pimpl->_staticTable == staticTable);
						#endif
					}
					_probeIterator += (unsigned)_pendingViews.size()/6;
					_pendingViews.clear();
				}

				auto probeCount = _probesToRender.size();
				auto nextBatchCount = std::min(probeCount -_probeIterator, s_maxProbesPerBatch);
				if (!nextBatchCount) {
					// Completed all of the probes
					UpdateUniforms(_staticPrepareHelper->_parsingContext->GetThreadContext());
					return { LightingEngine::StepType::None };
				}
				SequencePlayback::Step result;
				result._type = LightingEngine::StepType::MultiViewParseScene;
				VLA_UNSAFE_FORCE(Probe, probesThisStep, nextBatchCount);
				for (unsigned p=0; p<nextBatchCount; ++p) probesThisStep[p] = _probesToRender[_probeIterator+p].second;
				WriteProjectionDescs(result._multiViewDesc, MakeIteratorRange(probesThisStep, probesThisStep+nextBatchCount));
				assert(result._multiViewDesc.size() == nextBatchCount*6);		// expecting everything to be cubemaps
				result._pkts.resize(Techniques::Services::GetInstance().BatchCodeCount());
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
			YieldToPool(_pimpl->_probePrepareCfg);
			auto cfg = _pimpl->_probePrepareCfg.get();

			// wait for resources (shaders, etc)
			std::promise<Techniques::PreparedResourcesVisibility> preparePromise;
			auto prepareFuture = preparePromise.get_future();
			Techniques::PrepareResources(std::move(preparePromise), *_pimpl->_pipelineAccelerators, *cfg, _drawablePkt);
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
		return (unsigned)_pimpl->_probes.size();
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

	IResourceView& ShadowProbes::GetStaticProbeTable() const
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
		if (_pimpl->_pendingStaticTableInit) {
			// Ensure that we initialize all subresources into depth buffer's ShaderResource state
			// individual subresources will be switched to this state when rendered to; but Vulkan validation layer still complains about the unwritten layers
			auto tableRes = _pimpl->_staticTable.get();
			Metal::BarrierHelper{*Metal::DeviceContext::Get(threadContext)}.Add(*tableRes, Metal::BarrierResourceUsage::NoState(), BindFlag::ShaderResource);
		}

		if (_pimpl->_pendingClearOfProbeUniforms) {
			auto probeUniformsSize = sizeof(CB_ShadowProbeDesc)*6*_pimpl->_config._maxProbes;
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
		_pimpl->_sequencerDescSetLayoutName = SEQUENCER_DS ":Sequencer";
		_pimpl->_probePrepareCfg = CreateProbePrepareCfg(_pimpl->_pipelineAccelerators, sharedTechniqueDelegate, _pimpl->_config).ShareFuture();

		_pimpl->_probes.resize(config._maxProbes, Probe{Identity<Float4x4>(), 1.f, 1024.f, 0.5f*gPI, TextureDesc::Dimensionality::Undefined});

		auto staticDatabaseDesc = TextureDesc::PlainCube(_pimpl->_config._faceDims, _pimpl->_config._faceDims, _pimpl->_config._format);
		staticDatabaseDesc._arrayCount = 6*_pimpl->_config._maxProbes;
		auto device = _pimpl->_pipelineAccelerators->GetDevice().get();
		_pimpl->_staticTable = device->CreateResource(CreateDesc(BindFlag::ShaderResource | BindFlag::DepthStencil | BindFlag::TransferDst, staticDatabaseDesc), "probe-prepare");
		_pimpl->_staticTableSRV = _pimpl->_staticTable->CreateTextureView(BindFlag::ShaderResource);
		_pimpl->_pendingStaticTableInit = true;

		_pimpl->_probeUniforms = device->CreateResource(
			CreateDesc(BindFlag::UnorderedAccess|BindFlag::TransferDst, LinearBufferDesc::Create(sizeof(CB_ShadowProbeDesc)*staticDatabaseDesc._arrayCount, sizeof(CB_ShadowProbeDesc))), "shadow-probe-list");
		_pimpl->_probeUniformsUAV = _pimpl->_probeUniforms->CreateBufferView(BindFlag::UnorderedAccess);
	}

	ShadowProbes::ShadowProbes(LightingEngineApparatus& apparatus, const Configuration& config)
	: ShadowProbes(apparatus._pipelineAccelerators, *apparatus._sharedDelegates, config)
	{}

	ShadowProbes::~ShadowProbes()
	{}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	class DynamicShadowProbes::Pimpl
	{
	public:
		std::shared_ptr<Techniques::IPipelineAcceleratorPool> _pipelineAccelerators;
		std::shared_ptr<IResource> _staticTable, _probeUniforms;
		std::shared_ptr<IResourceView> _staticTableSRV, _staticTableDS, _probeUniformsUAV;
		ShadowProbes::Configuration _config;
		std::shared_ptr<MultiViewUniformsDelegate> _multiViewUniformsDelegate;
		::Assets::MarkerPtr<Techniques::SequencerConfig> _futureProbePrepareCfg;
		bool _pendingClearOfProbeUniforms = true;
		bool _pendingStaticTableInit = true;
		DEBUG_ONLY(Techniques::ParsingContext* _boundParsingContext = nullptr);
	};

	void DynamicShadowProbes::Bind(Techniques::ParsingContext& parsingContext)
	{
		assert(_pimpl->_boundParsingContext == nullptr);
		DEBUG_ONLY(_pimpl->_boundParsingContext = &parsingContext);
		parsingContext.GetUniformDelegateManager()->BindShaderResourceDelegate(_pimpl->_multiViewUniformsDelegate);
		parsingContext.GetUniformDelegateManager()->InvalidateUniforms();
		parsingContext.GetAttachmentReservation().Bind(s_semanticProbePrepare, _pimpl->_staticTable, BindFlag::ShaderResource);
	}

	void DynamicShadowProbes::UnbindAndBarrier(Techniques::ParsingContext& parsingContext)
	{
		assert(_pimpl->_boundParsingContext == &parsingContext);
		DEBUG_ONLY(_pimpl->_boundParsingContext = nullptr);

		parsingContext.GetUniformDelegateManager()->UnbindShaderResourceDelegate(*_pimpl->_multiViewUniformsDelegate);
		parsingContext.GetUniformDelegateManager()->InvalidateUniforms();
		parsingContext.GetAttachmentReservation().Unbind(*_pimpl->_staticTable);

		Metal::BarrierHelper{*Metal::DeviceContext::Get(parsingContext.GetThreadContext())}.Add(*_pimpl->_staticTable, BindFlag::DepthStencil, BindFlag::ShaderResource);
	}

	Techniques::RenderPassInstance DynamicShadowProbes::Begin(
		Techniques::ParsingContext& parsingContext,
		IteratorRange<const Techniques::ProjectionDesc*> multiViewDesc,
		unsigned firstFaceIndex)
	{
		assert(_pimpl->_boundParsingContext == &parsingContext);
		VLA_UNSAFE_FORCE(Float4x4, worldToProjections, multiViewDesc.size());
		for (unsigned c=0; c<multiViewDesc.size(); ++c) worldToProjections[c] = multiViewDesc[c]._worldToProjection;

		_pimpl->_multiViewUniformsDelegate->SetWorldToProjections({worldToProjections, worldToProjections+multiViewDesc.size()});

		Techniques::FrameBufferDescFragment fragment;
		SubpassDesc sp;
		TextureViewDesc viewDesc;
		viewDesc._arrayLayerRange = {firstFaceIndex, (unsigned)multiViewDesc.size()};
		sp.SetDepthStencil(fragment.DefineAttachment(s_semanticProbePrepare).Clear().FinalState(BindFlag::ShaderResource), viewDesc);
		sp.SetName("dynamic-shadow-prepare");
		fragment.AddSubpass(std::move(sp));

		Techniques::RenderPassBeginDesc beginInfo;
		return Techniques::RenderPassInstance{parsingContext, fragment, beginInfo};
	}

	IResourceView& DynamicShadowProbes::GetDynamicProbeTable() const
	{
		assert(_pimpl->_staticTableSRV);
		return *_pimpl->_staticTableSRV;
	}

	IResourceView& DynamicShadowProbes::GetDynamicProbeUniforms() const
	{
		assert(_pimpl->_probeUniformsUAV);
		return *_pimpl->_probeUniformsUAV;
	}

	Techniques::SequencerConfig* DynamicShadowProbes::GetSequencerConfig() const
	{
		return _pimpl->_futureProbePrepareCfg.TryActualize2().get();
	}

	unsigned DynamicShadowProbes::GetFaceCount()
	{
		return (unsigned)_pimpl->_config._maxProbes*6;
	}

	void DynamicShadowProbes::CompleteInitialization(IThreadContext& threadContext)
	{
		if (_pimpl->_pendingStaticTableInit) {
			// Ensure that we initialize all subresources into depth buffer's ShaderResource state
			// individual subresources will be switched to this state when rendered to; but Vulkan validation layer still complains about the unwritten layers
			auto tableRes = _pimpl->_staticTable.get();
			Metal::BarrierHelper{*Metal::DeviceContext::Get(threadContext)}.Add(*tableRes, Metal::BarrierResourceUsage::NoState(), BindFlag::ShaderResource);
		}

		if (_pimpl->_pendingClearOfProbeUniforms) {
			auto probeUniformsSize = sizeof(CB_ShadowProbeDesc)*6*_pimpl->_config._maxProbes;
			VLA(uint8_t, blank, probeUniformsSize);
			std::memset(blank, 0, probeUniformsSize);
			Metal::DeviceContext::Get(threadContext)->BeginBlitEncoder().Write(*_pimpl->_probeUniforms, MakeIteratorRange(blank, blank+probeUniformsSize));
			_pimpl->_pendingClearOfProbeUniforms = false;
		}
	}

	DynamicShadowProbes::DynamicShadowProbes(
		std::shared_ptr<Techniques::IPipelineAcceleratorPool> pipelineAccelerators,
		SharedTechniqueDelegateBox& sharedTechniqueDelegate,
		const ShadowProbes::Configuration& config)
	{
		_pimpl = std::make_unique<Pimpl>();
		_pimpl->_config = config;
		_pimpl->_pipelineAccelerators = std::move(pipelineAccelerators);
		_pimpl->_multiViewUniformsDelegate = std::make_shared<MultiViewUniformsDelegate>();
		_pimpl->_futureProbePrepareCfg = CreateProbePrepareCfg(_pimpl->_pipelineAccelerators, sharedTechniqueDelegate, _pimpl->_config);

		auto staticDatabaseDesc = TextureDesc::PlainCube(_pimpl->_config._faceDims, _pimpl->_config._faceDims, _pimpl->_config._format);
		staticDatabaseDesc._arrayCount = 6*_pimpl->_config._maxProbes;
		auto device = _pimpl->_pipelineAccelerators->GetDevice().get();
		_pimpl->_staticTable = device->CreateResource(CreateDesc(BindFlag::ShaderResource | BindFlag::DepthStencil | BindFlag::TransferDst, staticDatabaseDesc), "dynamic-probe-prepare");
		_pimpl->_staticTableSRV = _pimpl->_staticTable->CreateTextureView(BindFlag::ShaderResource);
		_pimpl->_staticTableDS = _pimpl->_staticTable->CreateTextureView(BindFlag::DepthStencil);
		_pimpl->_pendingStaticTableInit = true;

		_pimpl->_probeUniforms = device->CreateResource(
			CreateDesc(BindFlag::UnorderedAccess|BindFlag::TransferDst, LinearBufferDesc::Create(sizeof(CB_ShadowProbeDesc)*staticDatabaseDesc._arrayCount, sizeof(CB_ShadowProbeDesc))), "dynamic-shadow-probe-list");
		_pimpl->_probeUniformsUAV = _pimpl->_probeUniforms->CreateBufferView(BindFlag::UnorderedAccess);

		// not the most ideal/thread safe pattern for updating the 
		// _pimpl->_onFrameBarrierSignalDelegate = Techniques::Services::GetSubFrameEvents()._onFrameBarrier.Bind([this]() {
		// 	if (this->_pimpl->_futureProbePrepareCfg.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
		// 		this->_pimpl->_probePrepareCfg = this->_pimpl->_futureProbePrepareCfg.get();
		// 		Techniques::Services::GetSubFrameEvents()._onFrameBarrier.Unbind(this->_pimpl->_onFrameBarrierSignalDelegate);
		// 		this->_pimpl->_onFrameBarrierSignalDelegate = ~0u;
		// 	}
		// });
	}

	DynamicShadowProbes::DynamicShadowProbes(
		LightingEngineApparatus& apparatus,
		const ShadowProbes::Configuration& config)
	: DynamicShadowProbes(apparatus._pipelineAccelerators, *apparatus._sharedDelegates, config)
	{}

	DynamicShadowProbes::~DynamicShadowProbes()
	{
		// if (_pimpl->_onFrameBarrierSignalDelegate != ~0u)
		// 	Techniques::Services::GetSubFrameEvents()._onFrameBarrier.Unbind(_pimpl->_onFrameBarrierSignalDelegate)
	}

	bool operator==(const ShadowProbes::Configuration& lhs, const ShadowProbes::Configuration& rhs)
	{
		return lhs._faceDims == rhs._faceDims
		 	&& lhs._maxProbes == rhs._maxProbes
			&& lhs._format == rhs._format
			&& lhs._singleSidedBias._slopeScaledBias == rhs._singleSidedBias._slopeScaledBias
			&& lhs._singleSidedBias._depthBiasClamp == rhs._singleSidedBias._depthBiasClamp
			&& lhs._singleSidedBias._depthBias == rhs._singleSidedBias._depthBias
			&& lhs._doubleSidedBias._slopeScaledBias == rhs._doubleSidedBias._slopeScaledBias
			&& lhs._doubleSidedBias._depthBiasClamp == rhs._doubleSidedBias._depthBiasClamp
			&& lhs._doubleSidedBias._depthBias == rhs._doubleSidedBias._depthBias
			;
	}

	static ::Assets::MarkerPtr<Techniques::SequencerConfig> CreateProbePrepareCfg(std::shared_ptr<Techniques::IPipelineAcceleratorPool> pa, SharedTechniqueDelegateBox& sharedTechniqueDelegate, const ShadowProbes::Configuration& config)
	{
		// Create the pipeline accelerator configuration
		AttachmentDesc attachmentDesc { config._format, 0, LoadStore::Clear, LoadStore::Retain, 0, BindFlag::ShaderResource };
		SubpassDesc spDesc;
		spDesc.SetDepthStencil(0);
		FrameBufferDesc fbDesc {
			std::vector<AttachmentDesc>{attachmentDesc},
			std::vector<SubpassDesc>{spDesc}};

		// Coordinate space for cubemap rendering is defined by the API to make shader lookups simple
		// However, if it's not the same as our typical conventions, we may need to flip the winding
		// direction
		bool flipCulling = Techniques::GetGeometricCoordinateSpaceForCubemaps() != GeometricCoordinateSpace::RightHanded;
		::Assets::MarkerPtr<Techniques::SequencerConfig> result;
		::Assets::WhenAll(
			sharedTechniqueDelegate.GetShadowGenTechniqueDelegate(
				Techniques::ShadowGenType::VertexIdViewInstancing,
				config._singleSidedBias,
				config._doubleSidedBias,
				CullMode::Back, flipCulling ? FaceWinding::CW : FaceWinding::CCW))
			.ThenConstructToPromise(
				result.AdoptPromise(),
				[pa=std::move(pa), fbDesc](auto techDel) {
					auto cfg = pa->CreateSequencerConfig("shadow-probe");
					pa->SetTechniqueDelegate(*cfg, techDel);
					pa->SetFrameBufferDesc(*cfg, fbDesc, 0);
					return cfg;
				});
		return result;
	}

	ISemiStaticShadowProbeScheduler::~ISemiStaticShadowProbeScheduler() {}

}}
