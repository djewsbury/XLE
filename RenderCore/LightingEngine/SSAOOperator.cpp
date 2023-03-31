// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "SSAOOperator.h"
#include "RenderStepFragments.h"
#include "LightingEngine.h"
#include "SequenceIterator.h"
#include "StandardLightOperators.h"
#include "../Techniques/PipelineOperators.h"
#include "../Techniques/CommonBindings.h"
#include "../Techniques/ParsingContext.h"
#include "../Techniques/Services.h"
#include "../BufferUploads/IBufferUploads.h"
#include "../Format.h"
#include "../UniformsStream.h"
#include "../../Assets/Continuation.h"
#include "../../Math/Vector.h"
#include "../../Utility/MemoryUtils.h"
#include "../../xleres/FileList.h"
#include <memory>

#include "../Metal/Resource.h"
#include "../Metal/DeviceContext.h"
#include "../Vulkan/Metal/IncludeVulkan.h"

using namespace Utility::Literals;

namespace RenderCore { namespace LightingEngine
{
	static const auto Hash_AOOutput = "ao-output"_h;
	static const auto Hash_AOAccumulation = "ao-accumulation"_h;
	static const auto Hash_AOAccumulationPrev = "ao-accumulation"_h+1;
    static const auto Hash_AOWorking = "ao-working"_h;

	static const auto s_aoFormat = Format::R8_UNORM;

    static const uint32_t s_ditherTable[96] = {24, 72, 0, 48, 60, 12, 84, 36, 90, 42, 66, 18, 6, 54, 30, 78, 7, 91, 61, 25, 55, 43, 13, 73, 31, 67, 85, 1, 79, 19, 37, 49, 80, 20, 38, 50, 32, 68, 86, 2, 56, 44, 14, 74, 8, 92, 62, 26, 9, 57, 33, 81, 93, 45, 69, 21, 63, 15, 87, 39, 27, 75, 3, 51, 52, 4, 76, 28, 40, 88, 16, 64, 22, 70, 46, 94, 82, 34, 58, 10, 29, 65, 95, 11, 77, 17, 47, 59, 5, 89, 71, 35, 53, 41, 23, 83};

    void SSAOOperator::Execute(
        SequenceIterator& iterator,
        IResourceView& inputDepthsSRV,
        IResourceView& inputNormalsSRV,
        IResourceView& inputVelocitiesSRV,
        IResourceView& workingUAV,
        IResourceView& accumulationUAV,
        IResourceView& accumulationPrevUAV,
        IResourceView& aoOutputUAV,
        IResourceView* historyAccumulationSRV,
        IResourceView* hierarchicalDepthsSRV,
        IResourceView* depthPrevSRV,
        IResourceView* gbufferNormalPrevSRV)
    {
        assert(_secondStageConstructionState == 2);
        CompleteInitialization(*iterator._threadContext);

        auto& metalContext = *Metal::DeviceContext::Get(*iterator._threadContext);

        UniformsStream us;
        IResourceView* srvs[] = {
            &inputDepthsSRV, &aoOutputUAV, &workingUAV, &accumulationUAV, &accumulationPrevUAV, &inputNormalsSRV, &inputVelocitiesSRV, historyAccumulationSRV, 
            hierarchicalDepthsSRV, depthPrevSRV, gbufferNormalPrevSRV,
            _ditherTable.get() };
        us._resourceViews = MakeIteratorRange(srvs);
        struct AOProps
        {
            unsigned _searchSteps;
            float _maxWorldSpaceDistanceSq;
            unsigned _frameIdx;
            unsigned _clearAccumulation;
            float _thicknessHeuristicFactor;
            float _filteringStrength;
            float _variationTolerance;
        } aoProps {
            _opDesc._searchSteps, _opDesc._maxWorldSpaceDistance * _opDesc._maxWorldSpaceDistance,
            _pingPongCounter, 
            _pingPongCounter == ~0u,
            _opDesc._thicknessHeuristicFactor,
            _opDesc._filteringStrength,
            _opDesc._variationTolerance
        };
        UniformsStream::ImmediateData immData[] = {
            MakeOpaqueIteratorRange(aoProps)
        };
        us._immediateData = MakeIteratorRange(immData);
        UInt2 outputDims { iterator._rpi.GetFrameBufferDesc().GetProperties()._width, iterator._rpi.GetFrameBufferDesc().GetProperties()._height };
        
        bool orthogonalMainSceneCamera = IsOrthogonalProjection(iterator._parsingContext->GetProjectionDesc()._cameraToProjection);
        if (orthogonalMainSceneCamera) {
            _orthogonalComputeOp->Dispatch(
                *iterator._parsingContext,
                (outputDims[0] + (2*8) - 1) / (2*8), (outputDims[1] + (2*8) - 1) / (2*8), 1,
                us);
        } else {
            _perspectiveComputeOp->Dispatch(
                *iterator._parsingContext,
                (outputDims[0] + (2*8) - 1) / (2*8), (outputDims[1] + (2*8) - 1) / (2*8), 1,
                us);
        }

        // barrier on "workingUAV" (written in first step, read in second)
        Metal::BarrierHelper{metalContext}
            .Add(*workingUAV.GetResource(), BindFlag::UnorderedAccess, BindFlag::ShaderResource);
        
        _upsampleOp->Dispatch(
            *iterator._parsingContext,
            (outputDims[0] + (2*8) - 1) / (2*8), (outputDims[1] + (2*8) - 1) / (2*8), 1,
            us);

        // leave the output texture in ShaderResource layout
        Metal::BarrierHelper{metalContext}
            .Add(*aoOutputUAV.GetResource(), {BindFlag::UnorderedAccess, ShaderStage::Compute}, BindFlag::ShaderResource);
        ++_pingPongCounter;
    }

    RenderStepFragmentInterface SSAOOperator::CreateFragment(const FrameBufferProperties& fbProps)
    {
        assert(_secondStageConstructionState == 0);
        RenderStepFragmentInterface result{PipelineType::Compute};

        auto working = result.DefineAttachment(Hash_AOWorking).InitialState(LoadStore::DontCare, BindFlag::UnorderedAccess).Discard();
        auto accumulation = result.DefineAttachment(Hash_AOAccumulation).InitialState(LoadStore::DontCare, BindFlag::UnorderedAccess).FinalState(BindFlag::ShaderResource);
        auto accumulationPrev = result.DefineAttachment(Hash_AOAccumulationPrev).InitialState(BindFlag::ShaderResource).Discard();
        auto aoOutput = result.DefineAttachment(Hash_AOOutput).NoInitialState().FinalState(BindFlag::ShaderResource);

        Techniques::FrameBufferDescFragment::SubpassDesc spDesc;
        spDesc.AppendNonFrameBufferAttachmentView(result.DefineAttachment(Techniques::AttachmentSemantics::MultisampleDepth), BindFlag::ShaderResource, TextureViewDesc { TextureViewDesc::Aspect::Depth });
        spDesc.AppendNonFrameBufferAttachmentView(result.DefineAttachment(Techniques::AttachmentSemantics::GBufferNormal));
        spDesc.AppendNonFrameBufferAttachmentView(result.DefineAttachment(Techniques::AttachmentSemantics::GBufferMotion));

        spDesc.AppendNonFrameBufferAttachmentView(working, BindFlag::UnorderedAccess);
        spDesc.AppendNonFrameBufferAttachmentView(accumulation, BindFlag::UnorderedAccess);
        spDesc.AppendNonFrameBufferAttachmentView(accumulationPrev, BindFlag::ShaderResource);
        spDesc.AppendNonFrameBufferAttachmentView(aoOutput, BindFlag::UnorderedAccess);
        if (_integrationParams._hasHierarchicalDepths)
            spDesc.AppendNonFrameBufferAttachmentView(result.DefineAttachment(Techniques::AttachmentSemantics::HierarchicalDepths), BindFlag::ShaderResource);
        if (_integrationParams._hasHistoryConfidence) {
            spDesc.AppendNonFrameBufferAttachmentView(result.DefineAttachment(Techniques::AttachmentSemantics::HistoryAcc));
        } else {
            spDesc.AppendNonFrameBufferAttachmentView(result.DefineAttachment(Techniques::AttachmentSemantics::MultisampleDepthPrev).InitialState(BindFlag::ShaderResource), BindFlag::ShaderResource, TextureViewDesc { TextureViewDesc::Aspect::Depth });
            spDesc.AppendNonFrameBufferAttachmentView(result.DefineAttachment(Techniques::AttachmentSemantics::GBufferNormalPrev).InitialState(BindFlag::ShaderResource), BindFlag::ShaderResource);
        }
        spDesc.SetName("ao-operator");

        result.AddSubpass(
            std::move(spDesc),
            [op=shared_from_this(), hd=_integrationParams._hasHierarchicalDepths, hc=_integrationParams._hasHistoryConfidence](SequenceIterator& iterator) {

                IResourceView* hierarchicalDepthsSRV = nullptr, *depthPrevSRV = nullptr, *gbufferNormalPrevSRV = nullptr;
                IResourceView* historyAccumulationSRV = nullptr;
                unsigned counter = 7;
                if (hd) {
                    // need to ensure the hierarchical depths compute step has finished
                    iterator._rpi.AutoNonFrameBufferBarrier({
                        {counter, BindFlag::ShaderResource, ShaderStage::Compute}
                    });
                    hierarchicalDepthsSRV = iterator._rpi.GetNonFrameBufferAttachmentView(counter).get();
                    ++counter;
                }
                if (hc) {
                    historyAccumulationSRV = iterator._rpi.GetNonFrameBufferAttachmentView(counter).get();
                    ++counter;
                } else {
                    depthPrevSRV = iterator._rpi.GetNonFrameBufferAttachmentView(counter).get();
                    gbufferNormalPrevSRV = iterator._rpi.GetNonFrameBufferAttachmentView(counter+1).get();
                    counter += 2;
                }

                op->Execute(
                    iterator,
                    *iterator._rpi.GetNonFrameBufferAttachmentView(0),
                    *iterator._rpi.GetNonFrameBufferAttachmentView(1),
                    *iterator._rpi.GetNonFrameBufferAttachmentView(2),
                    *iterator._rpi.GetNonFrameBufferAttachmentView(3),
                    *iterator._rpi.GetNonFrameBufferAttachmentView(4),
                    *iterator._rpi.GetNonFrameBufferAttachmentView(5),
                    *iterator._rpi.GetNonFrameBufferAttachmentView(6),
                    historyAccumulationSRV, hierarchicalDepthsSRV, depthPrevSRV, gbufferNormalPrevSRV);
            });

        return result;
    }

    void SSAOOperator::PreregisterAttachments(Techniques::FragmentStitchingContext& stitchingContext, const FrameBufferProperties& fbProps)
    {
        UInt2 fbSize{fbProps._width, fbProps._height};
        Techniques::PreregisteredAttachment preGeneratedAttachments[] {
            Techniques::PreregisteredAttachment {
                Hash_AOAccumulation,
                CreateDesc(
                    BindFlag::UnorderedAccess | BindFlag::ShaderResource,
                    TextureDesc::Plain2D(fbSize[0]/2, fbSize[1]/2, s_aoFormat)),
                "ao-accumulation"
            },

            Techniques::PreregisteredAttachment {
                Hash_AOWorking,
                CreateDesc(
                    BindFlag::UnorderedAccess | BindFlag::ShaderResource,
                    TextureDesc::Plain2D(fbSize[0]/2, fbSize[1]/2, s_aoFormat)),
                "ao-working"
            },

            Techniques::PreregisteredAttachment {
                Hash_AOOutput,
                CreateDesc(
                    BindFlag::UnorderedAccess | BindFlag::ShaderResource,
                    TextureDesc::Plain2D(fbSize[0], fbSize[1], s_aoFormat)),
                "ao-output",
                Techniques::PreregisteredAttachment::State::Uninitialized
            }
        };
        for (auto a:preGeneratedAttachments)
            stitchingContext.DefineAttachment(a);
        stitchingContext.DefineDoubleBufferAttachment(Hash_AOAccumulation, MakeClearValue(1.f, 1.f, 1.f, 1.f), BindFlag::ShaderResource);

        if (!_integrationParams._hasHistoryConfidence) {
            stitchingContext.DefineDoubleBufferAttachment(Techniques::AttachmentSemantics::MultisampleDepth, MakeClearValue(0.f, 0.f, 0.f, 0.f), BindFlag::ShaderResource);
            stitchingContext.DefineDoubleBufferAttachment(Techniques::AttachmentSemantics::GBufferNormal, MakeClearValue(0.f, 0.f, 0.f, 0.f), BindFlag::ShaderResource);
        }
    }

    void SSAOOperator::ResetAccumulation() { _pingPongCounter = ~0u; }
    ::Assets::DependencyValidation SSAOOperator::GetDependencyValidation() const { assert(_secondStageConstructionState == 2); return _depVal; }

    void SSAOOperator::CompleteInitialization(IThreadContext& threadContext)
	{
        if (_pendingCompleteInit) {
            auto ditherTable = threadContext.GetDevice()->CreateResource(
                CreateDesc(
                    BindFlag::ShaderResource | BindFlag::TexelBuffer | BindFlag::TransferDst,
                    LinearBufferDesc::Create(sizeof(s_ditherTable))),
                "ao-dither-table");
            _ditherTable = ditherTable->CreateTextureView(BindFlag::ShaderResource, {Format::R32_UINT});

            Metal::DeviceContext::Get(threadContext)->BeginBlitEncoder().Write(*ditherTable, MakeIteratorRange(s_ditherTable));
            _pendingCompleteInit = false;
        }
	}

    SSAOOperator::SSAOOperator(
        std::shared_ptr<Techniques::PipelineCollection> pipelinePool,
        const AmbientOcclusionOperatorDesc& opDesc,
        const IntegrationParams& integrationParams)
    : _opDesc(opDesc)
    , _pipelinePool(std::move(pipelinePool))
    , _integrationParams(integrationParams)
    {
        assert(opDesc._searchSteps > 1 && opDesc._searchSteps < 1024);  // rationality check
        assert(opDesc._maxWorldSpaceDistance > 0);
    }
    SSAOOperator::~SSAOOperator() {}

    void SSAOOperator::SecondStageConstruction(
        std::promise<std::shared_ptr<SSAOOperator>>&& promise,
        const Techniques::FrameBufferTarget& fbTarget)
    {
        assert(_secondStageConstructionState == 0);
        _secondStageConstructionState = 1;

        UniformsStreamInterface usi;
        usi.BindResourceView(0, "FullResolutionDepths"_h);
        usi.BindResourceView(1, "OutputTexture"_h);
        usi.BindResourceView(2, "Working"_h);
        usi.BindResourceView(3, "AccumulationAO"_h);
        usi.BindResourceView(4, "AccumulationAOLast"_h);
        usi.BindResourceView(5, "InputNormals"_h);
        usi.BindResourceView(6, "GBufferMotion"_h);
        usi.BindResourceView(7, "HistoryAcc"_h);
        usi.BindResourceView(8, "HierarchicalDepths"_h);
        usi.BindResourceView(9, "DepthPrev"_h);
        usi.BindResourceView(10, "GBufferNormalPrev"_h);
        usi.BindResourceView(11, "DitherTable"_h);
        usi.BindImmediateData(0, "AOProps"_h);

        ParameterBox selectors;
        if (_opDesc._sampleBothDirections) selectors.SetParameter("BOTH_WAYS", 1);
        if (_opDesc._lateTemporalFiltering) selectors.SetParameter("DO_LATE_TEMPORAL_FILTERING", 1);
        if (_integrationParams._hasHierarchicalDepths) selectors.SetParameter("HAS_HIERARCHICAL_DEPTHS", 1);
        if (_integrationParams._hasHistoryConfidence) selectors.SetParameter("HAS_HISTORY_CONFIDENCE_TEXTURE", 1);
        if (_opDesc._enableHierarchicalStepping) selectors.SetParameter("ENABLE_HIERARCHICAL_STEPPING", 1);
        if (_opDesc._enableFiltering) selectors.SetParameter("ENABLE_FILTERING", 1);
        if (_opDesc._thicknessHeuristicFactor < 1) selectors.SetParameter("ENABLE_THICKNESS_HEURISTIC", 1);
        auto perspectiveComputeOp = Techniques::CreateComputeOperator(
            _pipelinePool,
            AO_COMPUTE_HLSL ":main",
            selectors, 
            GENERAL_OPERATOR_PIPELINE ":ComputeMain",
            usi);
        selectors.SetParameter("ORTHO_CAMERA", 1);
        auto orthogonalComputeOp = Techniques::CreateComputeOperator(
            _pipelinePool,
            AO_COMPUTE_HLSL ":main",
            selectors, 
            GENERAL_OPERATOR_PIPELINE ":ComputeMain",
            usi);

        auto upsampleOp = Techniques::CreateComputeOperator(
            _pipelinePool,
            AO_COMPUTE_HLSL ":UpsampleOp",
            selectors, 
            GENERAL_OPERATOR_PIPELINE ":ComputeMain",
            usi);

        ::Assets::WhenAll(perspectiveComputeOp, orthogonalComputeOp, upsampleOp).ThenConstructToPromise(
            std::move(promise),
            [strongThis=shared_from_this()](auto perspectiveComputeOpActual, auto orthogonalComputeOpActual, auto upsampleOpActual) mutable
            {
                assert(strongThis->_secondStageConstructionState == 1);

                ::Assets::DependencyValidationMarker depVals[] {
                    perspectiveComputeOpActual->GetDependencyValidation(),
                    orthogonalComputeOpActual->GetDependencyValidation(),
                    upsampleOpActual->GetDependencyValidation()
                };
                strongThis->_depVal = ::Assets::GetDepValSys().MakeOrReuse(MakeIteratorRange(depVals));

                strongThis->_perspectiveComputeOp = std::move(perspectiveComputeOpActual);
                strongThis->_orthogonalComputeOp = std::move(orthogonalComputeOpActual);
                strongThis->_upsampleOp = std::move(upsampleOpActual);

                strongThis->_secondStageConstructionState = 2;
                return strongThis;
            });
    }

    template<typename Type>
        static uint64_t CompressToBits(Type value, unsigned bitCount)
    {
        assert(bitCount < 64);
        assert((uint64_t(value) & ((1ull<<uint64_t(bitCount))-1ull)) == uint64_t(value));
        return uint64_t(value) & ((1ull<<uint64_t(bitCount))-1ull);
    }

    uint64_t AmbientOcclusionOperatorDesc::GetHash(uint64_t seed) const
    {
        uint64_t value0 = 
            CompressToBits(_searchSteps, 8)
            | (CompressToBits(_sampleBothDirections, 1) << 8ull)
            | (CompressToBits(_lateTemporalFiltering, 1) << 9ull)
            | (CompressToBits(_enableFiltering, 1) << 10ull)
            | (CompressToBits(_enableHierarchicalStepping, 1) << 11ull)
            ;

        uint64_t value1 =
            CompressToBits(*reinterpret_cast<const unsigned*>(&_maxWorldSpaceDistance), 32)
            | (CompressToBits(*reinterpret_cast<const unsigned*>(&_thicknessHeuristicFactor), 32) << 32ull)
            ;

        uint64_t value2 =
            CompressToBits(*reinterpret_cast<const unsigned*>(&_filteringStrength), 32)
            | (CompressToBits(*reinterpret_cast<const unsigned*>(&_variationTolerance), 32) << 32ull)
            ;

        return HashCombine(HashCombine(HashCombine(value0, value1), value2), seed);
    }

    ISSAmbientOcclusion::~ISSAmbientOcclusion() {}

}}
