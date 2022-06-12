// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "SSAOOperator.h"
#include "RenderStepFragments.h"
#include "LightingEngine.h"
#include "LightingEngineIterator.h"
#include "StandardLightOperators.h"
#include "../Techniques/PipelineOperators.h"
#include "../Techniques/CommonBindings.h"
#include "../Techniques/ParsingContext.h"
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

namespace RenderCore { namespace LightingEngine
{
	static const auto Hash_AOOutput = ConstHash64<'ao-o', 'utpu', 't'>::Value;
	static const auto Hash_AOAccumulation0 = ConstHash64<'ao-a', 'ccum', 'ulat', 'ion0'>::Value;
	static const auto Hash_AOAccumulation1 = ConstHash64<'ao-a', 'ccum', 'ulat', 'ion1'>::Value;

	static const auto s_downresDepthsFormat = Format::R16_UNORM;
	static const auto s_aoFormat = Format::R8_UNORM;

    static const uint32_t s_ditherTable[96] = {24, 72, 0, 48, 60, 12, 84, 36, 90, 42, 66, 18, 6, 54, 30, 78, 7, 91, 61, 25, 55, 43, 13, 73, 31, 67, 85, 1, 79, 19, 37, 49, 80, 20, 38, 50, 32, 68, 86, 2, 56, 44, 14, 74, 8, 92, 62, 26, 9, 57, 33, 81, 93, 45, 69, 21, 63, 15, 87, 39, 27, 75, 3, 51, 52, 4, 76, 28, 40, 88, 16, 64, 22, 70, 46, 94, 82, 34, 58, 10, 29, 65, 95, 11, 77, 17, 47, 59, 5, 89, 71, 35, 53, 41, 23, 83};

    void SSAOOperator::Execute(
        LightingTechniqueIterator& iterator,
        IResourceView& inputDepthsSRV,
        IResourceView& inputNormalsSRV,
        IResourceView& inputVelocitiesSRV,
        IResourceView& inputHistoryAccumulation,
        IResourceView& accumulation0UAV,
        IResourceView& accumulation1UAV,
        IResourceView& aoOutputUAV,
        IResourceView* hierarchicalDepths)
    {
        auto& metalContext = *Metal::DeviceContext::Get(*iterator._threadContext);
        if (hierarchicalDepths) {
            // need to ensure the hierarchical depths compute step has finished
            VkImageMemoryBarrier barrier[1];
            barrier[0] = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
            barrier[0].pNext = nullptr;
            barrier[0].oldLayout = VK_IMAGE_LAYOUT_GENERAL;
            barrier[0].newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            barrier[0].srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
            barrier[0].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            barrier[0].image = checked_cast<Metal_Vulkan::Resource*>(hierarchicalDepths->GetResource().get())->GetImage();
            barrier[0].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            barrier[0].subresourceRange.baseMipLevel = 0;
            barrier[0].subresourceRange.baseArrayLayer = 0;
            barrier[0].subresourceRange.levelCount = VK_REMAINING_MIP_LEVELS;
            barrier[0].subresourceRange.layerCount = VK_REMAINING_ARRAY_LAYERS;
            vkCmdPipelineBarrier(
				metalContext.GetActiveCommandList().GetUnderlying().get(),
				VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
				VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
				0,
				0, nullptr,
				0, nullptr,
				dimof(barrier), barrier);
		}

        IResourceView* accumulationUAV = (_pingPongCounter&1) ? &accumulation0UAV : &accumulation1UAV;
        IResourceView* accumulationLastUAV = (_pingPongCounter&1) ? &accumulation1UAV : &accumulation0UAV;
        
        UniformsStream us;
        IResourceView* srvs[] = { &inputDepthsSRV, &aoOutputUAV, accumulationUAV, accumulationLastUAV, &inputNormalsSRV, &inputVelocitiesSRV, &inputHistoryAccumulation, hierarchicalDepths, _ditherTable.get() };
        us._resourceViews = MakeIteratorRange(srvs);
        struct AOProps
        {
            unsigned _searchSteps;
            unsigned _occupancyThreshold;
            unsigned _frameIdx;
            unsigned _clearAccumulation;
        } aoProps {
            _opDesc._searchSteps, _opDesc._occupancyThreshold,
            _pingPongCounter, 
            _pingPongCounter == ~0u,
        };
        UniformsStream::ImmediateData immData[] = {
            MakeOpaqueIteratorRange(aoProps)
        };
        us._immediateData = MakeIteratorRange(immData);
        UInt2 outputDims { iterator._rpi.GetFrameBufferDesc().GetProperties()._outputWidth, iterator._rpi.GetFrameBufferDesc().GetProperties()._outputHeight };
        
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

        {
            // barrier on "accumulationUAV" (written in first step, read in second)
            VkImageMemoryBarrier barrier[1];
            barrier[0] = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
            barrier[0].pNext = nullptr;
            barrier[0].oldLayout = VK_IMAGE_LAYOUT_GENERAL;
            barrier[0].newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            barrier[0].srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
            barrier[0].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            barrier[0].image = checked_cast<Metal_Vulkan::Resource*>(accumulationUAV->GetResource().get())->GetImage();
            barrier[0].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            barrier[0].subresourceRange.baseMipLevel = 0;
            barrier[0].subresourceRange.baseArrayLayer = 0;
            barrier[0].subresourceRange.levelCount = VK_REMAINING_MIP_LEVELS;
            barrier[0].subresourceRange.layerCount = VK_REMAINING_ARRAY_LAYERS;
            vkCmdPipelineBarrier(
				metalContext.GetActiveCommandList().GetUnderlying().get(),
				VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
				VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
				0,
				0, nullptr,
				0, nullptr,
				dimof(barrier), barrier);
        }
        
        _upsampleOp->Dispatch(
            *iterator._parsingContext,
            (outputDims[0] + (2*8) - 1) / (2*8), (outputDims[1] + (2*8) - 1) / (2*8), 1,
            us);

        ++_pingPongCounter;
    }

    RenderStepFragmentInterface SSAOOperator::CreateFragment(const FrameBufferProperties& fbProps)
    {
        RenderStepFragmentInterface result{PipelineType::Compute};

        auto accumulation0 = result.DefineAttachment(Hash_AOAccumulation0).InitialState(BindFlag::UnorderedAccess).FinalState(BindFlag::UnorderedAccess);
        auto accumulation1 = result.DefineAttachment(Hash_AOAccumulation1).InitialState(BindFlag::UnorderedAccess).FinalState(BindFlag::UnorderedAccess);
        auto aoOutput = result.DefineAttachment(Hash_AOOutput).NoInitialState().FinalState(BindFlag::UnorderedAccess);

        Techniques::FrameBufferDescFragment::SubpassDesc spDesc;
        spDesc.AppendNonFrameBufferAttachmentView(result.DefineAttachment(Techniques::AttachmentSemantics::MultisampleDepth), BindFlag::ShaderResource, TextureViewDesc { TextureViewDesc::Aspect::Depth });
        spDesc.AppendNonFrameBufferAttachmentView(result.DefineAttachment(Techniques::AttachmentSemantics::GBufferNormal));
        spDesc.AppendNonFrameBufferAttachmentView(result.DefineAttachment(Techniques::AttachmentSemantics::GBufferMotion));
        spDesc.AppendNonFrameBufferAttachmentView(result.DefineAttachment(Techniques::AttachmentSemantics::HistoryAcc));

        spDesc.AppendNonFrameBufferAttachmentView(accumulation0, BindFlag::UnorderedAccess);
        spDesc.AppendNonFrameBufferAttachmentView(accumulation1, BindFlag::UnorderedAccess);
        spDesc.AppendNonFrameBufferAttachmentView(aoOutput, BindFlag::UnorderedAccess);
        if (_hasHierarchicalDepths)
            spDesc.AppendNonFrameBufferAttachmentView(result.DefineAttachment(Techniques::AttachmentSemantics::HierarchicalDepths), BindFlag::ShaderResource);
        spDesc.SetName("ao-operator");

        result.AddSubpass(
            std::move(spDesc),
            [op=shared_from_this(), hd=_hasHierarchicalDepths](LightingTechniqueIterator& iterator) {
                op->Execute(
                    iterator,
                    *iterator._rpi.GetNonFrameBufferAttachmentView(0),
                    *iterator._rpi.GetNonFrameBufferAttachmentView(1),
                    *iterator._rpi.GetNonFrameBufferAttachmentView(2),
                    *iterator._rpi.GetNonFrameBufferAttachmentView(3),
                    *iterator._rpi.GetNonFrameBufferAttachmentView(4),
                    *iterator._rpi.GetNonFrameBufferAttachmentView(5),
                    *iterator._rpi.GetNonFrameBufferAttachmentView(6),
                    hd ? iterator._rpi.GetNonFrameBufferAttachmentView(7).get() : nullptr);
            });

        return result;
    }

    void SSAOOperator::PreregisterAttachments(Techniques::FragmentStitchingContext& stitchingContext)
    {
        UInt2 fbSize{stitchingContext._workingProps._outputWidth, stitchingContext._workingProps._outputHeight};
        Techniques::PreregisteredAttachment preGeneratedAttachments[] {
            Techniques::PreregisteredAttachment {
                Hash_AOAccumulation0,
                CreateDesc(
                    BindFlag::UnorderedAccess | BindFlag::ShaderResource, 0, 0, 
                    TextureDesc::Plain2D(fbSize[0]/2, fbSize[1]/2, s_aoFormat),
                    "ao-accumulation-0"),
                Techniques::PreregisteredAttachment::State::Initialized
            },

            Techniques::PreregisteredAttachment {
                Hash_AOAccumulation1,
                CreateDesc(
                    BindFlag::UnorderedAccess | BindFlag::ShaderResource, 0, 0, 
                    TextureDesc::Plain2D(fbSize[0]/2, fbSize[1]/2, s_aoFormat),
                    "ao-accumulation-1"),
                Techniques::PreregisteredAttachment::State::Initialized
            },

            Techniques::PreregisteredAttachment {
                Hash_AOOutput,
                CreateDesc(
                    BindFlag::UnorderedAccess | BindFlag::ShaderResource, 0, 0, 
                    TextureDesc::Plain2D(fbSize[0], fbSize[1], s_aoFormat),
                    "ao-output"),
                Techniques::PreregisteredAttachment::State::Uninitialized
            }
        };
        for (auto a:preGeneratedAttachments)
            stitchingContext.DefineAttachment(a);
    }

    void SSAOOperator::ResetAccumulation() { _pingPongCounter = ~0u; }
    ::Assets::DependencyValidation SSAOOperator::GetDependencyValidation() const { return _depVal; }

    SSAOOperator::SSAOOperator(
        std::shared_ptr<Techniques::IComputeShaderOperator> perspectiveComputeOp,
        std::shared_ptr<Techniques::IComputeShaderOperator> orthogonalComputeOp,
        std::shared_ptr<Techniques::IComputeShaderOperator> upsampleOp,
        std::shared_ptr<IResourceView> ditherTable,
        const AmbientOcclusionOperatorDesc& opDesc,
        bool hasHierarchicalDepths)
    : _perspectiveComputeOp(std::move(perspectiveComputeOp))
    , _orthogonalComputeOp(std::move(orthogonalComputeOp)), _upsampleOp(std::move(upsampleOp))
    , _ditherTable(std::move(ditherTable))
    , _opDesc(opDesc)
    , _hasHierarchicalDepths(hasHierarchicalDepths)
    {
        ::Assets::DependencyValidationMarker depVals[] {
            _perspectiveComputeOp->GetDependencyValidation(),
            _orthogonalComputeOp->GetDependencyValidation(),
            _upsampleOp->GetDependencyValidation()
        };
        _depVal = ::Assets::GetDepValSys().MakeOrReuse(MakeIteratorRange(depVals));
    }
    SSAOOperator::~SSAOOperator() {}

    void SSAOOperator::ConstructToPromise(
        std::promise<std::shared_ptr<SSAOOperator>>&& promise,
        std::shared_ptr<Techniques::PipelineCollection> pipelinePool,
        const AmbientOcclusionOperatorDesc& opDesc,
		bool hasHierarchicalDepths)
    {
        assert(opDesc._searchSteps > 1 && opDesc._searchSteps < 1024);  // rationality check
        assert(opDesc._occupancyThreshold > 1);

        UniformsStreamInterface usi;
        usi.BindResourceView(0, Hash64("InputTexture"));
        usi.BindResourceView(1, Hash64("OutputTexture"));
        usi.BindResourceView(2, Hash64("AccumulationAO"));
        usi.BindResourceView(3, Hash64("AccumulationAOLast"));
        usi.BindResourceView(4, Hash64("InputNormals"));
        usi.BindResourceView(5, Hash64("GBufferMotion"));
        usi.BindResourceView(6, Hash64("HistoryAcc"));
        usi.BindResourceView(7, Hash64("HierarchicalDepths"));
        usi.BindResourceView(8, Hash64("DitherTable"));
        usi.BindImmediateData(0, Hash64("AOProps"));

        ParameterBox selectors;
        if (opDesc._sampleBothDirections) selectors.SetParameter("BOTH_WAYS", 1);
        if (opDesc._lateTemporalFiltering) selectors.SetParameter("DO_LATE_TEMPORAL_FILTERING", 1);
        if (hasHierarchicalDepths) selectors.SetParameter("HAS_HIERARCHICAL_DEPTHS", 1);
        if (opDesc._enableHierarchicalStepping) selectors.SetParameter("ENABLE_HIERARCHICAL_STEPPING", 1);
        if (opDesc._enableFiltering) selectors.SetParameter("ENABLE_FILTERING", 1);
        auto perspectiveComputeOp = Techniques::CreateComputeOperator(
            pipelinePool,
            AO_COMPUTE_HLSL ":main",
            selectors, 
            GENERAL_OPERATOR_PIPELINE ":ComputeMain",
            usi);
        selectors.SetParameter("ORTHO_CAMERA", 1);
        auto orthogonalComputeOp = Techniques::CreateComputeOperator(
            pipelinePool,
            AO_COMPUTE_HLSL ":main",
            selectors, 
            GENERAL_OPERATOR_PIPELINE ":ComputeMain",
            usi);

        auto upsampleOp = Techniques::CreateComputeOperator(
            pipelinePool,
            AO_COMPUTE_HLSL ":UpsampleOp",
            selectors, 
            GENERAL_OPERATOR_PIPELINE ":ComputeMain",
            usi);

        auto ditherTable = pipelinePool->GetDevice()->CreateResource(
            CreateDesc(
                BindFlag::ShaderResource | BindFlag::TexelBuffer,
                0, 0, LinearBufferDesc::Create(sizeof(s_ditherTable)),
                "ao-dither-table"),
            SubResourceInitData{MakeIteratorRange(s_ditherTable)});
        auto ditherTableView = ditherTable->CreateTextureView(BindFlag::ShaderResource, {Format::R32_UINT});

        ::Assets::WhenAll(perspectiveComputeOp, orthogonalComputeOp, upsampleOp).ThenConstructToPromise(
            std::move(promise),
            [ditherTableView, od=opDesc, hd=hasHierarchicalDepths](auto perspectiveComputeOpActual, auto orthogonalComputeOpActual, auto upsampleOpActual) mutable
            { return std::make_shared<SSAOOperator>(std::move(perspectiveComputeOpActual), std::move(orthogonalComputeOpActual), std::move(upsampleOpActual), std::move(ditherTableView), od, hd); });
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
        uint64_t value = 
            CompressToBits(_searchSteps, 8)
            | (CompressToBits(_occupancyThreshold, 5) << 8ull)
            | (CompressToBits(_sampleBothDirections, 1) << 13ull)
            | (CompressToBits(_lateTemporalFiltering, 1) << 14ull)
            | (CompressToBits(_enableFiltering, 1) << 15ull)
            | (CompressToBits(_enableHierarchicalStepping, 1) << 16ull)
            ;
        return HashCombine(value, seed);
    }

}}
