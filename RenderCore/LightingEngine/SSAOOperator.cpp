// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "SSAOOperator.h"
#include "RenderStepFragments.h"
#include "LightingEngine.h"
#include "LightingEngineIterator.h"
#include "../Techniques/PipelineOperators.h"
#include "../Techniques/CommonBindings.h"
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

    void SSAOOperator::Execute(
        LightingEngine::LightingTechniqueIterator& iterator,
        IResourceView& inputDepthsSRV,
        IResourceView& inputNormalsSRV,
        IResourceView& inputVelocitiesSRV,
        IResourceView& inputHistoryAccumulation,
        IResourceView& accumulation0UAV,
        IResourceView& accumulation1UAV,
        IResourceView& aoOutputUAV,
        IResourceView& hierarchicalDepths)
    {
        {
            // need to ensure the hierarchical depths compute step has finished
            auto& metalContext = *Metal::DeviceContext::Get(*iterator._threadContext);
            VkImageMemoryBarrier barrier[1];
            barrier[0] = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
            barrier[0].pNext = nullptr;
            barrier[0].oldLayout = VK_IMAGE_LAYOUT_GENERAL;
            barrier[0].newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            barrier[0].srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
            barrier[0].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            barrier[0].image = checked_cast<Metal_Vulkan::Resource*>(hierarchicalDepths.GetResource().get())->GetImage();
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
        IResourceView* srvs[] = { &inputDepthsSRV, &aoOutputUAV, accumulationUAV, accumulationLastUAV, &inputNormalsSRV, &inputVelocitiesSRV, &inputHistoryAccumulation, &hierarchicalDepths };
        us._resourceViews = MakeIteratorRange(srvs);
        UInt4 aoProps { _pingPongCounter, _pingPongCounter == ~0u, 0, 0 };
        UniformsStream::ImmediateData immData[] = {
            MakeOpaqueIteratorRange(aoProps)
        };
        us._immediateData = MakeIteratorRange(immData);
        UInt2 outputDims { iterator._rpi.GetFrameBufferDesc().GetProperties()._outputWidth, iterator._rpi.GetFrameBufferDesc().GetProperties()._outputHeight };
        
        _computeOp->Dispatch(
            *iterator._parsingContext,
            (outputDims[0] + (2*8) - 1) / (2*8), (outputDims[1] + (2*8) - 1) / (2*8), 1,
            us);
        _upsampleOp->Dispatch(
            *iterator._parsingContext,
            (outputDims[0] + (2*8) - 1) / (2*8), (outputDims[1] + (2*8) - 1) / (2*8), 1,
            us);

        ++_pingPongCounter;
    }

    LightingEngine::RenderStepFragmentInterface SSAOOperator::CreateFragment(const RenderCore::FrameBufferProperties& fbProps)
    {
        LightingEngine::RenderStepFragmentInterface result{PipelineType::Compute};

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
        spDesc.AppendNonFrameBufferAttachmentView(result.DefineAttachment(Techniques::AttachmentSemantics::HierarchicalDepths), BindFlag::ShaderResource);
        spDesc.SetName("ao-operator");

        result.AddSubpass(
            std::move(spDesc),
            [op=shared_from_this()](LightingEngine::LightingTechniqueIterator& iterator) {
                op->Execute(
                    iterator,
                    *iterator._rpi.GetNonFrameBufferAttachmentView(0),
                    *iterator._rpi.GetNonFrameBufferAttachmentView(1),
                    *iterator._rpi.GetNonFrameBufferAttachmentView(2),
                    *iterator._rpi.GetNonFrameBufferAttachmentView(3),
                    *iterator._rpi.GetNonFrameBufferAttachmentView(4),
                    *iterator._rpi.GetNonFrameBufferAttachmentView(5),
                    *iterator._rpi.GetNonFrameBufferAttachmentView(6),
                    *iterator._rpi.GetNonFrameBufferAttachmentView(7));
            });

        return result;
    }

    void SSAOOperator::PreregisterAttachments(RenderCore::Techniques::FragmentStitchingContext& stitchingContext)
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
        std::shared_ptr<RenderCore::Techniques::IComputeShaderOperator> computeOp,
        std::shared_ptr<RenderCore::Techniques::IComputeShaderOperator> upsampleOp)
    : _computeOp(std::move(computeOp)), _upsampleOp(std::move(upsampleOp))
    {
        _depVal = ::Assets::GetDepValSys().Make();
        _depVal.RegisterDependency(_computeOp->GetDependencyValidation());
        _depVal.RegisterDependency(_upsampleOp->GetDependencyValidation());
    }
    SSAOOperator::~SSAOOperator() {}

    void SSAOOperator::ConstructToPromise(
        std::promise<std::shared_ptr<SSAOOperator>>&& promise,
        std::shared_ptr<RenderCore::Techniques::PipelineCollection> pipelinePool)
    {
        UniformsStreamInterface usi;
        usi.BindResourceView(0, Hash64("InputTexture"));
        usi.BindResourceView(1, Hash64("OutputTexture"));
        usi.BindResourceView(2, Hash64("AccumulationAO"));
        usi.BindResourceView(3, Hash64("AccumulationAOLast"));
        usi.BindResourceView(4, Hash64("InputNormals"));
        usi.BindResourceView(5, Hash64("GBufferMotion"));
        usi.BindResourceView(6, Hash64("HistoryAcc"));
        usi.BindResourceView(7, Hash64("HierarchicalDepths"));
        usi.BindImmediateData(0, Hash64("AOProps"));

        ParameterBox selectors;
        auto computeOp = Techniques::CreateComputeOperator(
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

        ::Assets::WhenAll(computeOp, upsampleOp).ThenConstructToPromise(std::move(promise));
    }

}}
