// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "HierarchicalDepths.h"
#include "LightingEngineInternal.h"
#include "../Techniques/RenderPass.h"
#include "../Techniques/PipelineOperators.h"
#include "../Techniques/Drawables.h"		// for SequencerUniformsHelper
#include "../Techniques/CommonBindings.h"
#include "../Metal/DeviceContext.h"
#include "../Metal/Resource.h"
#include "../IAnnotator.h"
#include "../../Assets/AssetFutureContinuation.h"
#include "../../Utility/BitUtils.h"
#include "../../xleres/FileList.h"

namespace RenderCore { namespace LightingEngine
{
	void HierarchicalDepthsOperator::Execute(RenderCore::LightingEngine::LightingTechniqueIterator& iterator)
	{
		GPUProfilerBlock profileBlock(*iterator._threadContext, "HierarchicalDepthsOperator");

		auto& metalContext = *Metal::DeviceContext::Get(*iterator._threadContext);
		vkCmdFillBuffer(
			metalContext.GetActiveCommandList().GetUnderlying().get(),
			checked_cast<Metal::Resource*>(_atomicCounterBufferView->GetResource().get())->GetBuffer(), 
			0, VK_WHOLE_SIZE, 0);

		IResourceView* srvs[2+13];
		srvs[0] = _atomicCounterBufferView.get();
		srvs[1] = iterator._rpi.GetNonFrameBufferAttachmentView(0).get();
		unsigned mipCount = iterator._rpi.GetNonFrameBufferAttachmentView(1)->GetResource()->GetDesc()._textureDesc._mipCount;
		for (unsigned c=0; c<13; ++c) {
			// duplicate the lowest resource view over any extra bindings
			srvs[2+c] = iterator._rpi.GetNonFrameBufferAttachmentView(1+std::min(c, mipCount-1)).get();
		}

		UniformsStream us;
		us._resourceViews = MakeIteratorRange(srvs);

		Techniques::SequencerUniformsHelper uniformsHelper{*iterator._parsingContext};
		UInt2 outputDims { iterator._rpi.GetFrameBufferDesc().GetProperties()._outputWidth, iterator._rpi.GetFrameBufferDesc().GetProperties()._outputHeight };
		_resolveOp->Dispatch(
			*iterator._threadContext, *iterator._parsingContext, uniformsHelper,
			(outputDims[0]+63) / 64, (outputDims[1]+63) / 64, 1,
			us);
	}

	RenderCore::LightingEngine::RenderStepFragmentInterface HierarchicalDepthsOperator::CreateFragment(const FrameBufferProperties& fbProps)
	{
		LightingEngine::RenderStepFragmentInterface result{PipelineType::Compute};

		Techniques::FrameBufferDescFragment::SubpassDesc spDesc;
		spDesc.AppendNonFrameBufferAttachmentView(result.DefineAttachment(Techniques::AttachmentSemantics::MultisampleDepth), BindFlag::UnorderedAccess, TextureViewDesc { TextureViewDesc::Aspect::Depth });
		auto hierarchicalDepthsAttachment = result.DefineAttachment(Hash64("HierarchicalDepths"), LoadStore::DontCare);
		unsigned depthsMipCount = IntegerLog2(std::max(fbProps._outputWidth, fbProps._outputHeight))+1;
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

	void HierarchicalDepthsOperator::PregisterAttachments(RenderCore::Techniques::FragmentStitchingContext& stitchingContext) 
	{
		UInt2 fbSize{stitchingContext._workingProps._outputWidth, stitchingContext._workingProps._outputHeight};
		unsigned depthsMipCount = IntegerLog2(std::max(fbSize[0], fbSize[1]))+1;
		Techniques::PreregisteredAttachment attachments[] {
			Techniques::PreregisteredAttachment {
				Hash64("HierarchicalDepths"),
				CreateDesc(
					BindFlag::UnorderedAccess | BindFlag::ShaderResource | BindFlag::TransferSrc, 0, 0, 
					TextureDesc::Plain2D(fbSize[0], fbSize[1], Format::R32_FLOAT, depthsMipCount),
					"hierarchical-depths")
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
				0, 0, LinearBufferDesc::Create(4*4),
				"depth-downsample-atomic-counter"
			));
		_atomicCounterBufferView = atomicBuffer->CreateTextureView(BindFlag::UnorderedAccess, TextureViewDesc{TextureViewDesc::FormatFilter{Format::R32_UINT}});
	}

	HierarchicalDepthsOperator::~HierarchicalDepthsOperator() {}

	void HierarchicalDepthsOperator::ConstructToFuture(
		::Assets::FuturePtr<HierarchicalDepthsOperator>& future,
		std::shared_ptr<RenderCore::Techniques::PipelinePool> pipelinePool)
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

		::Assets::WhenAll(resolveOp).ThenConstructToFuture(
			future, 
			[dev=pipelinePool->GetDevice()](auto resolveOp) { return std::make_shared<HierarchicalDepthsOperator>(std::move(resolveOp), std::move(dev)); });
	}


}}



