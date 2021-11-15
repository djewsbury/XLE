// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "SSAOOperator.h"
#include "RenderStepFragments.h"
#include "LightingEngine.h"
#include "LightingEngineInternal.h"
#include "../Techniques/PipelineOperators.h"
#include "../Techniques/CommonBindings.h"
#include "../Format.h"
#include "../UniformsStream.h"
#include "../../Assets/Continuation.h"
#include "../../Math/Vector.h"
#include "../../Utility/MemoryUtils.h"
#include "../../xleres/FileList.h"
#include <memory>

/*#include "../Metal/Resource.h"
#include "../Metal/DeviceContext.h"
#include "../Vulkan/Metal/IncludeVulkan.h"*/

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
        IResourceView& accumulation0UAV,
        IResourceView& accumulation1UAV,
        IResourceView& aoOutputUAV,
        IResourceView& hierarchicalDepths)
    {
        IResourceView* accumulationUAV = (_pingPongCounter&1) ? &accumulation0UAV : &accumulation1UAV;
        IResourceView* accumulationLastUAV = (_pingPongCounter&1) ? &accumulation1UAV : &accumulation0UAV;
        
        UniformsStream us;
        IResourceView* srvs[] = { &inputDepthsSRV, &aoOutputUAV, accumulationUAV, accumulationLastUAV, &inputNormalsSRV, &inputVelocitiesSRV, &hierarchicalDepths };
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
        auto aoOutput = result.DefineAttachment(Hash_AOOutput);

        Techniques::FrameBufferDescFragment::SubpassDesc spDesc;
        spDesc.AppendNonFrameBufferAttachmentView(result.DefineAttachment(Techniques::AttachmentSemantics::MultisampleDepth), BindFlag::ShaderResource, TextureViewDesc { TextureViewDesc::Aspect::Depth });
        spDesc.AppendNonFrameBufferAttachmentView(result.DefineAttachment(Techniques::AttachmentSemantics::GBufferNormal));
        spDesc.AppendNonFrameBufferAttachmentView(result.DefineAttachment(Techniques::AttachmentSemantics::GBufferMotion));

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
                    *iterator._rpi.GetNonFrameBufferAttachmentView(6));
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
        usi.BindResourceView(6, Hash64("HierarchicalDepths"));
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
