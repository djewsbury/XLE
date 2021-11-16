// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "RayVsModel.h"
#include "../RenderCore/Metal/DeviceContext.h"
#include "../RenderCore/Metal/ObjectFactory.h"
#include "../RenderCore/Metal/QueryPool.h"
#include "../RenderCore/Format.h"
#include "../RenderCore/Types.h"
#include "../RenderCore/IDevice.h"
#include "../RenderCore/BufferView.h"
#include "../RenderCore/Techniques/CommonResources.h"
#include "../RenderCore/Techniques/TechniqueUtils.h"
#include "../RenderCore/Techniques/Techniques.h"
#include "../RenderCore/Techniques/ParsingContext.h"
#include "../RenderCore/Techniques/DrawableDelegates.h"
#include "../RenderCore/Techniques/BasicDelegates.h"
#include "../RenderCore/Techniques/RenderPass.h"
#include "../RenderCore/Techniques/TechniqueDelegates.h"
#include "../RenderCore/Techniques/PipelineAccelerator.h"
#include "../RenderCore/Techniques/Drawables.h"
#include "../RenderCore/Techniques/DrawablesInternal.h"
#include "../RenderCore/Techniques/Services.h"
#include "../RenderCore/Techniques/CommonUtils.h"
#include "../RenderCore/Techniques/PipelineOperators.h"
#include "../Assets/Assets.h"
#include "../Assets/DepVal.h"
#include "../Math/Transformations.h"
#include "../ConsoleRig/ResourceBox.h"
#include "../xleres/FileList.h"

namespace SceneEngine
{
    using namespace RenderCore;

	static std::shared_ptr<RenderCore::Techniques::ITechniqueDelegate> CreateTechniqueDelegate(
		const ::Assets::PtrToMarkerPtr<RenderCore::Techniques::TechniqueSetFile>& techniqueSet,
		unsigned testTypeParameter);

	class RayDefinitionUniformDelegate : public Techniques::IShaderResourceDelegate
	{
	public:
		struct Buffer
        {
            Float3 _rayStart = Zero<Float3>();
            float _rayLength = 0.f;
            Float3 _rayDirection = Zero<Float3>();
            unsigned _dummy = 0;
			Float4x4 _frustum = Identity<Float4x4>();
        };
		Buffer _data = {};

		virtual void WriteImmediateData(Techniques::ParsingContext& context, const void* objectContext, unsigned idx, IteratorRange<void*> dst) override
		{ 
			assert(idx==0);
			assert(dst.size() == sizeof(_data));
			std::memcpy(dst.begin(), &_data, sizeof(_data));
		}
		virtual size_t GetImmediateDataSize(Techniques::ParsingContext&, const void*, unsigned idx) override { assert(idx==0); return sizeof(_data); };

		RayDefinitionUniformDelegate()
		{
			BindImmediateData(0, s_binding);
		}
		static const uint64_t s_binding;
	};
	const uint64_t RayDefinitionUniformDelegate::s_binding = Hash64("RayDefinition");

    class ModelIntersectionStateContext::Pimpl
    {
    public:
        IThreadContext* _threadContext;
        ModelIntersectionResources* _res;
		bool _pendingUnbind = false;

		Techniques::RenderPassInstance _rpi;
		Metal::GraphicsEncoder_Optimized _encoder;
		unsigned _queryId = ~0u;

		TestType _testType;

		std::shared_ptr<Techniques::SequencerConfig> _sequencerConfig;
		Techniques::IPipelineAcceleratorPool* _pipelineAccelerators = nullptr;
    };

    class ModelIntersectionResources
    {
    public:
        RenderCore::IResourcePtr _streamOutputBuffer;
        RenderCore::IResourcePtr _cpuAccessBuffer;

		std::unique_ptr<RenderCore::Metal::QueryPool> _streamOutputQueryPool;

		std::shared_ptr<RayDefinitionUniformDelegate> _rayDefinition;
		Techniques::AttachmentPool _dummyAttachmentPool;
		std::shared_ptr<Techniques::FrameBufferPool> _frameBufferPool;

        ModelIntersectionResources(unsigned elementSize, unsigned elementCount);
    };

    ModelIntersectionResources::ModelIntersectionResources(unsigned elementSize, unsigned elementCount)
	: _dummyAttachmentPool(RenderCore::Techniques::Services::GetDevicePtr())
    {
        auto& device = RenderCore::Techniques::Services::GetDevice();

        LinearBufferDesc lbDesc;
        lbDesc._structureByteSize = elementSize;
        lbDesc._sizeInBytes = elementSize * elementCount;

        auto bufferDesc = CreateDesc(
            BindFlag::StreamOutput | BindFlag::TransferSrc, 0, GPUAccess::Read | GPUAccess::Write,
            lbDesc, "ModelIntersectionBuffer");
        
        _streamOutputBuffer = device.CreateResource(bufferDesc);

        _cpuAccessBuffer = device.CreateResource(
            CreateDesc(BindFlag::TransferDst, CPUAccess::Read, 0, lbDesc, "ModelIntersectionCopyBuffer"));

		_streamOutputQueryPool = std::make_unique<RenderCore::Metal::QueryPool>(
			Metal::GetObjectFactory(device), 
			Metal::QueryPool::QueryType::StreamOutput_Stream0, 4);

		_rayDefinition = std::make_shared<RayDefinitionUniformDelegate>();

		_frameBufferPool = RenderCore::Techniques::CreateFrameBufferPool();
    }
	
#if GFXAPI_TARGET == GFXAPI_VULKAN
	void BufferBarrier0(Metal::DeviceContext& context, Metal::Resource& buffer)
	{
		VkBufferMemoryBarrier globalBarrier =
			{
				VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
				nullptr,
				VK_ACCESS_TRANSFORM_FEEDBACK_WRITE_BIT_EXT,
				VK_ACCESS_TRANSFER_READ_BIT,
				VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED,
				buffer.GetBuffer(),
				0, VK_WHOLE_SIZE
			};
		context.GetActiveCommandList().PipelineBarrier(
			VK_PIPELINE_STAGE_TRANSFORM_FEEDBACK_BIT_EXT,
			VK_PIPELINE_STAGE_TRANSFER_BIT,
			0,
			0, nullptr,
			1, &globalBarrier,
			0, nullptr);
	}

	void BufferBarrier1(Metal::DeviceContext& context, Metal::Resource& buffer)
	{
		VkBufferMemoryBarrier globalBarrier =
			{
				VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
				nullptr,
				VK_ACCESS_TRANSFER_WRITE_BIT,
				VK_ACCESS_TRANSFORM_FEEDBACK_WRITE_BIT_EXT,
				VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED,
				buffer.GetBuffer(),
				0, VK_WHOLE_SIZE
			};
		context.GetActiveCommandList().PipelineBarrier(
			VK_PIPELINE_STAGE_TRANSFER_BIT,
			VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
			0,
			0, nullptr,
			1, &globalBarrier,
			0, nullptr);
	}
#endif

    auto ModelIntersectionStateContext::GetResults() -> std::vector<ResultEntry>
    {
        std::vector<ResultEntry> result;

        auto& metalContext = *Metal::DeviceContext::Get(*_pimpl->_threadContext);

            // We must lock the stream output buffer, and look for results within it
            // It seems that this kind of thing wasn't part of the original intentions
            // for stream output. So the results can appear anywhere within the buffer.
            // We have to search for non-zero entries. Results that haven't been written
            // to will appear zeroed out.
		_pimpl->_encoder = {};
		_pimpl->_rpi = {};
		if (_pimpl->_queryId != ~0u)
			_pimpl->_res->_streamOutputQueryPool->End(metalContext, _pimpl->_queryId);
		_pimpl->_pendingUnbind = false;
		#if GFXAPI_TARGET == GFXAPI_VULKAN
			BufferBarrier0(metalContext, *checked_cast<Metal::Resource*>(_pimpl->_res->_streamOutputBuffer.get()));
		#endif
		_pimpl->_threadContext->CommitCommands(CommitCommandsFlags::WaitForCompletion);		// unfortunately we need a synchronize here

		unsigned hitEventsWritten = 0;
		if (_pimpl->_queryId != ~0u) {
			Metal::QueryPool::QueryResult_StreamOutput out;
			_pimpl->_res->_streamOutputQueryPool->GetResults_Stall(metalContext, _pimpl->_queryId, MakeOpaqueIteratorRange(out));
			_pimpl->_queryId = ~0u;
			hitEventsWritten = out._primitivesWritten;
		}

		if (hitEventsWritten!=0) {
			// note -- we may not have to readback the entire buffer here, if we use the hitEventsWritten value
			auto readback = _pimpl->_res->_streamOutputBuffer->ReadBackSynchronized(*_pimpl->_threadContext);
			if (!readback.empty()) {
				const auto* mappedData = (const ResultEntry*)readback.data();
				result.reserve(std::min(std::min(hitEventsWritten, unsigned(readback.size() / sizeof(ResultEntry))), s_maxResultCount));
				for (unsigned c=0; c<std::min(hitEventsWritten, s_maxResultCount); ++c)
					result.push_back(mappedData[c]);
			}

			std::sort(result.begin(), result.end(), &ResultEntry::CompareDepth);
		}

        return std::move(result);
    }

    void ModelIntersectionStateContext::SetRay(const std::pair<Float3, Float3> worldSpaceRay)
    {
        float rayLength = Magnitude(worldSpaceRay.second - worldSpaceRay.first);
		_pimpl->_res->_rayDefinition->_data._rayStart = worldSpaceRay.first;
		_pimpl->_res->_rayDefinition->_data._rayLength = rayLength;
		_pimpl->_res->_rayDefinition->_data._rayDirection = (worldSpaceRay.second - worldSpaceRay.first) / rayLength;
    }

    void ModelIntersectionStateContext::SetFrustum(const Float4x4& frustum)
    {
		_pimpl->_res->_rayDefinition->_data._frustum = frustum;
    }

	class ModelIntersectionTechniqueBox
	{
	public:
		FrameBufferDesc _fbDesc;
		std::shared_ptr<RenderCore::Techniques::ITechniqueDelegate> _rayTestTechniqueDelegate;
		std::shared_ptr<RenderCore::Techniques::ITechniqueDelegate> _frustumTechniqueDelegate;
		std::shared_ptr<RenderCore::Techniques::SequencerConfig> _rayTestSequencerCfg;
		std::shared_ptr<RenderCore::Techniques::SequencerConfig> _frustumTestSequencerCfg;
		::Assets::DependencyValidation _depVal;

		const ::Assets::DependencyValidation& GetDependencyValidation() const { return _depVal; }

		ModelIntersectionTechniqueBox(Techniques::IPipelineAcceleratorPool& pipelineAcceleratorPool)
		{
			auto device = RenderCore::Techniques::Services::GetDevicePtr();
			auto techniqueSetFile = ::Assets::MakeAssetPtr<RenderCore::Techniques::TechniqueSetFile>(ILLUM_TECH);
			_rayTestTechniqueDelegate = CreateTechniqueDelegate(techniqueSetFile, 0);
			_frustumTechniqueDelegate = CreateTechniqueDelegate(techniqueSetFile, 1);

			std::vector<SubpassDesc> subpasses;
			subpasses.emplace_back(SubpassDesc{});
			_fbDesc = FrameBufferDesc { {}, std::move(subpasses) };
			_depVal = techniqueSetFile->GetDependencyValidation();

			_frustumTestSequencerCfg = pipelineAcceleratorPool.CreateSequencerConfig(
				"frustum-test",
				_frustumTechniqueDelegate,
				{}, _fbDesc);

			_rayTestSequencerCfg = pipelineAcceleratorPool.CreateSequencerConfig(
				"ray-vs-model",
				_rayTestTechniqueDelegate,
				{}, _fbDesc);
		}
	};

    ModelIntersectionStateContext::ModelIntersectionStateContext(
        TestType testType,
        RenderCore::IThreadContext& threadContext,
		Techniques::IPipelineAcceleratorPool& pipelineAcceleratorPool)
    {
        _pimpl = std::make_unique<Pimpl>();
        _pimpl->_threadContext = &threadContext;

        auto& metalContext = *Metal::DeviceContext::Get(threadContext);
		_pimpl->_pendingUnbind = true;
        _pimpl->_res = &ConsoleRig::FindCachedBox<ModelIntersectionResources>(
            sizeof(ResultEntry), s_maxResultCount);

		    // We're doing the intersection test in the geometry shader. This means
            // we have to setup a projection transform to avoid removing any potential
            // intersection results during screen-edge clipping.
            // Also, if we want to know the triangle pts and barycentric coordinates,
            // we need to make sure that no clipping occurs.
            // The easiest way to prevent clipping would be use a projection matrix that
            // would transform all points into a single point in the center of the view
            // frustum.
        // Viewport newViewport(0.f, 0.f, float(255.f), float(255.f), 0.f, 1.f);
        // metalContext.Bind(MakeIteratorRange(&newViewport, &newViewport+1), {});

		auto& box = ConsoleRig::FindCachedBox<ModelIntersectionTechniqueBox>(std::ref(pipelineAcceleratorPool));
		_pimpl->_queryId = _pimpl->_res->_streamOutputQueryPool->Begin(metalContext);
		_pimpl->_rpi = Techniques::RenderPassInstance {
			threadContext,
			box._fbDesc, {},
			*_pimpl->_res->_frameBufferPool, _pimpl->_res->_dummyAttachmentPool,
			{} };

		VertexBufferView sov { _pimpl->_res->_streamOutputBuffer.get() };
		_pimpl->_sequencerConfig = (testType == TestType::FrustumTest) ? box._frustumTestSequencerCfg : box._rayTestSequencerCfg;

		auto pipelineLayout = pipelineAcceleratorPool.GetCompiledPipelineLayoutMarker(*_pimpl->_sequencerConfig);
		pipelineLayout->StallWhilePending();

		_pimpl->_encoder = metalContext.BeginStreamOutputEncoder(pipelineLayout->Actualize()->GetPipelineLayout(), MakeIteratorRange(&sov, &sov+1));
		_pimpl->_testType = testType;
		_pimpl->_pipelineAccelerators = &pipelineAcceleratorPool;
    }

    ModelIntersectionStateContext::~ModelIntersectionStateContext()
    {
		auto& metalContext = *Metal::DeviceContext::Get(*_pimpl->_threadContext);

		if (_pimpl->_pendingUnbind) {
			_pimpl->_encoder = {};
			_pimpl->_rpi = {};
			if (_pimpl->_queryId != ~0u)
				_pimpl->_res->_streamOutputQueryPool->End(metalContext, _pimpl->_queryId);
		}

		if (_pimpl->_queryId != ~0u) {
			_pimpl->_res->_streamOutputQueryPool->AbandonResults(_pimpl->_queryId);
			_pimpl->_queryId = ~0u;
		}
    }

	void ModelIntersectionStateContext::ExecuteDrawables(
		Techniques::ParsingContext& parsingContext, 
		RenderCore::Techniques::DrawablesPacket& drawablePkt,
		const Techniques::CameraDesc* cameraForLOD)
	{
		assert(_pimpl->_pendingUnbind);		// we must not have queried the results yet
		auto& context = *_pimpl->_threadContext;

            // The camera settings can affect the LOD that objects a rendered with.
            // So, in some cases we need to initialise the camera to the same state
            // used in rendering. This will ensure that we get the right LOD behaviour.
        Techniques::CameraDesc camera;
        if (cameraForLOD) { camera = *cameraForLOD; }

		auto projDesc = BuildProjectionDesc(camera, UInt2(256, 256));
		projDesc._cameraToProjection = MakeFloat4x4(
            0.f, 0.f, 0.f, 0.5f,
            0.f, 0.f, 0.f, 0.5f,
            0.f, 0.f, 0.f, 0.5f,
            0.f, 0.f, 0.f, 1.f);
		projDesc._worldToProjection = Combine(InvertOrthonormalTransform(projDesc._cameraToWorld), projDesc._cameraToProjection);
		parsingContext.GetProjectionDesc() = projDesc;

		auto& metalContext = *Metal::DeviceContext::Get(context);
		parsingContext.GetUniformDelegateManager()->AddShaderResourceDelegate(_pimpl->_res->_rayDefinition);

		_pimpl->_pipelineAccelerators->LockForReading();
		TRY {
			RenderCore::Techniques::Draw(metalContext, _pimpl->_encoder, parsingContext, *_pimpl->_pipelineAccelerators, *_pimpl->_sequencerConfig, drawablePkt);
		} CATCH(...) {
			_pimpl->_pipelineAccelerators->UnlockForReading();
			throw;
		} CATCH_END
		_pimpl->_pipelineAccelerators->UnlockForReading();
		parsingContext.GetUniformDelegateManager()->RemoveShaderResourceDelegate(*_pimpl->_res->_rayDefinition);
	}

	static const InputElementDesc s_soEles[] = {
        InputElementDesc("POINT",               0, Format::R32G32B32A32_FLOAT),
        InputElementDesc("POINT",               1, Format::R32G32B32A32_FLOAT),
        InputElementDesc("POINT",               2, Format::R32G32B32A32_FLOAT),
		InputElementDesc("PROPERTIES",			0, Format::R32G32B32A32_UINT)
    };

    static const unsigned s_soStrides[] = { sizeof(ModelIntersectionStateContext::ResultEntry) };

	static std::shared_ptr<RenderCore::Techniques::ITechniqueDelegate> CreateTechniqueDelegate(
		const ::Assets::PtrToMarkerPtr<RenderCore::Techniques::TechniqueSetFile>& techniqueSet,
		unsigned testTypeParameter)
	{
		return RenderCore::Techniques::CreateTechniqueDelegate_RayTest(
			techniqueSet, testTypeParameter, 
			StreamOutputInitializers {
				MakeIteratorRange(s_soEles),
				MakeIteratorRange(s_soStrides)});
	}
}

