// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "HierarchicalDepths.h"
#include "LightingEngineIterator.h"
#include "RenderStepFragments.h"
#include "../Techniques/RenderPass.h"
#include "../Techniques/PipelineOperators.h"
#include "../Techniques/Drawables.h"		// for SequencerUniformsHelper
#include "../Techniques/CommonBindings.h"
#include "../Metal/DeviceContext.h"
#include "../Metal/Resource.h"
#include "../IAnnotator.h"
#include "../../Assets/Continuation.h"
#include "../../Utility/BitUtils.h"
#include "../../xleres/FileList.h"

namespace RenderCore { namespace LightingEngine
{
	void HierarchicalDepthsOperator::Execute(RenderCore::LightingEngine::LightingTechniqueIterator& iterator)
	{
		GPUProfilerBlock profileBlock(*iterator._threadContext, "HierarchicalDepthsOperator");

		Metal::BarrierHelper{*iterator._threadContext}.Add(*iterator._rpi.GetNonFrameBufferAttachmentView(1)->GetResource(), Metal::BarrierResourceUsage::NoState(), BindFlag::UnorderedAccess);

		auto& metalContext = *Metal::DeviceContext::Get(*iterator._threadContext);
		vkCmdFillBuffer(
			metalContext.GetActiveCommandList().GetUnderlying().get(),
			checked_cast<Metal::Resource*>(_atomicCounterBufferView->GetResource().get())->GetBuffer(), 
			0, VK_WHOLE_SIZE, 0);

		IResourceView* srvs[2+13];
		srvs[0] = _atomicCounterBufferView.get();
		srvs[1] = iterator._rpi.GetNonFrameBufferAttachmentView(0).get();
		unsigned mipCount = iterator._rpi.GetNonFrameBufferAttachmentView(1)->GetResource()->GetDesc()._textureDesc._mipCount;
		auto& fbProps = iterator._rpi.GetFrameBufferDesc().GetProperties();
		unsigned expectedMipCount = IntegerLog2(std::max(fbProps._width, fbProps._height))+1;
		assert(mipCount == expectedMipCount);
		for (unsigned c=0; c<13; ++c) {		// 13 slots in the shader input interface
			// duplicate the lowest resource view over any extra bindings
			srvs[2+c] = iterator._rpi.GetNonFrameBufferAttachmentView(1+std::min(c, mipCount-1)).get();
		}

		UniformsStream us;
		us._resourceViews = MakeIteratorRange(srvs);

		UInt2 outputDims { iterator._rpi.GetFrameBufferDesc().GetProperties()._width, iterator._rpi.GetFrameBufferDesc().GetProperties()._height };
		_resolveOp->Dispatch(
			*iterator._parsingContext,
			(outputDims[0]+63) / 64, (outputDims[1]+63) / 64, 1,
			us);

		// because we're using a compute shader fragment, we must manually add a barrier to update the resource layout
		Metal::BarrierHelper{*iterator._threadContext}.Add(*iterator._rpi.GetNonFrameBufferAttachmentView(1)->GetResource(), BindFlag::UnorderedAccess, BindFlag::ShaderResource);
	}

	RenderCore::LightingEngine::RenderStepFragmentInterface HierarchicalDepthsOperator::CreateFragment(const FrameBufferProperties& fbProps)
	{
		LightingEngine::RenderStepFragmentInterface result{PipelineType::Compute};

		Techniques::FrameBufferDescFragment::SubpassDesc spDesc;
		spDesc.AppendNonFrameBufferAttachmentView(result.DefineAttachment(Techniques::AttachmentSemantics::MultisampleDepth), BindFlag::ShaderResource, TextureViewDesc { TextureViewDesc::Aspect::Depth });
		auto hierarchicalDepthsAttachment = result.DefineAttachment(Techniques::AttachmentSemantics::HierarchicalDepths).InitialState(LoadStore::DontCare, BindFlag::UnorderedAccess).FinalState(BindFlag::ShaderResource);
		unsigned depthsMipCount = IntegerLog2(std::max(fbProps._width, fbProps._height))+1;
		for (unsigned c=0; c<depthsMipCount; ++c) {
			TextureViewDesc view;
			view._format._explicitFormat = Format::R32_FLOAT;
			view._mipRange._min = c;
			view._mipRange._count = 1;
			spDesc.AppendNonFrameBufferAttachmentView(hierarchicalDepthsAttachment, BindFlag::UnorderedAccess, view);
		}
		spDesc.SetName("depth-downsample-operator");

		result.AddSubpass(
			std::move(spDesc),
			[op=shared_from_this()](LightingEngine::LightingTechniqueIterator& iterator) {
				op->Execute(iterator);
			});

		return result;
	}

	void HierarchicalDepthsOperator::PreregisterAttachments(RenderCore::Techniques::FragmentStitchingContext& stitchingContext) 
	{
		UInt2 fbSize{stitchingContext._workingProps._width, stitchingContext._workingProps._height};
		unsigned depthsMipCount = IntegerLog2(std::max(fbSize[0], fbSize[1]))+1;
		Techniques::PreregisteredAttachment attachments[] {
			Techniques::PreregisteredAttachment {
				Techniques::AttachmentSemantics::HierarchicalDepths,
				CreateDesc(
					BindFlag::UnorderedAccess | BindFlag::ShaderResource | BindFlag::TransferSrc,
					TextureDesc::Plain2D(fbSize[0], fbSize[1], Format::R32_FLOAT, depthsMipCount)),
				"hierarchical-depths"
			}
		};
		for (const auto& a:attachments)
			stitchingContext.DefineAttachment(a);
	}

	HierarchicalDepthsOperator::HierarchicalDepthsOperator(
		std::shared_ptr<RenderCore::Techniques::IComputeShaderOperator> resolveOp,
		std::shared_ptr<RenderCore::IDevice> device)
	: _resolveOp(std::move(resolveOp))
	{
		_depVal = ::Assets::GetDepValSys().Make();
		_depVal.RegisterDependency(_resolveOp->GetDependencyValidation());
		_completionCommandList = 0;

		auto atomicBuffer = device->CreateResource(
			CreateDesc(
				BindFlag::TransferDst | BindFlag::UnorderedAccess | BindFlag::TexelBuffer,
				LinearBufferDesc::Create(4*4)),
			"depth-downsample-atomic-counter");
		_atomicCounterBufferView = atomicBuffer->CreateTextureView(BindFlag::UnorderedAccess, TextureViewDesc{TextureViewDesc::FormatFilter{Format::R32_UINT}});
	}

	HierarchicalDepthsOperator::~HierarchicalDepthsOperator() {}

	void HierarchicalDepthsOperator::ConstructToPromise(
		std::promise<std::shared_ptr<HierarchicalDepthsOperator>>&& promise,
		std::shared_ptr<RenderCore::Techniques::PipelineCollection> pipelinePool)
	{
		UniformsStreamInterface usi;
		usi.BindResourceView(0, Hash64("AtomicBuffer"));
		usi.BindResourceView(1, Hash64("InputDepths"));
		auto downSampleDepthsBinding = Hash64("DownsampleDepths");
		for (unsigned c=0; c<13; ++c)
			usi.BindResourceView(2+c, downSampleDepthsBinding+c);

		ParameterBox selectors;
		auto resolveOp = Techniques::CreateComputeOperator(
			pipelinePool,
			HIERARCHICAL_DEPTHS_HLSL ":GenerateDownsampleDepths",
			selectors, 
			SSR_PIPELINE ":DownsampleDepths",
			usi);

		::Assets::WhenAll(resolveOp).ThenConstructToPromise(
			std::move(promise), 
			[dev=pipelinePool->GetDevice()](auto resolveOp) { return std::make_shared<HierarchicalDepthsOperator>(std::move(resolveOp), std::move(dev)); });
	}


}}



