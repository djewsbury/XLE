// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "RenderPassUtils.h"
#include "RenderPass.h"
#include "ParsingContext.h"
#include "CommonBindings.h"
#include "Techniques.h"
#include "../IDevice.h"
#include "../Format.h"
#include "../../Utility/IteratorUtils.h"

namespace RenderCore { namespace Techniques
{
	static ClearValue AsClearValueColor(unsigned clearColor) 
	{
		ClearValue clear;
		clear._float[0] = ((clearColor>>16)&0xff)/float(0xff);
		clear._float[1] = ((clearColor>> 8)&0xff)/float(0xff);
		clear._float[2] = ((clearColor>> 0)&0xff)/float(0xff);
		clear._float[3] = ((clearColor>>24)&0xff)/float(0xff);
		return clear;
	}

	RenderPassInstance RenderPassToPresentationTarget(
        ParsingContext& parserContext,
		LoadStore loadOperation,
		unsigned clearColor)
	{
		FrameBufferDescFragment frag;
		SubpassDesc subpass;
		subpass.AppendOutput(frag.DefineAttachment(AttachmentSemantics::ColorLDR).InitialState(loadOperation, 0));
		frag.AddSubpass(std::move(subpass));
		auto clear = AsClearValueColor(clearColor);
        return RenderPassInstance{parserContext, frag, RenderPassBeginDesc{MakeIteratorRange(&clear, &clear+1)}};
	}

	RenderPassInstance RenderPassToPresentationTarget(
		const RenderCore::IResourcePtr& presentationTarget,
        ParsingContext& parserContext,
		LoadStore loadOperation,
		unsigned clearColor)
	{
		parserContext.GetTechniqueContext()._attachmentPool->Bind(AttachmentSemantics::ColorLDR, presentationTarget);
		return RenderPassToPresentationTarget(parserContext, loadOperation, clearColor);
	}

	RenderPassInstance RenderPassToPresentationTargetWithDepthStencil(
        ParsingContext& parserContext,
		LoadStore loadOperation,
		unsigned clearColor)
	{
		auto boundDepth = parserContext.GetTechniqueContext()._attachmentPool->GetBoundResource(AttachmentSemantics::MultisampleDepth);
		if (!boundDepth && loadOperation != LoadStore::Clear)
			return RenderPassToPresentationTarget(parserContext, loadOperation, clearColor);

		FrameBufferDescFragment frag;
		SubpassDesc subpass;
		subpass.AppendOutput(frag.DefineAttachment(AttachmentSemantics::ColorLDR).InitialState(loadOperation, 0));
		subpass.SetDepthStencil(frag.DefineAttachment(AttachmentSemantics::MultisampleDepth).InitialState(loadOperation, 0));
		frag.AddSubpass(std::move(subpass));

        auto clear = AsClearValueColor(clearColor);
		return RenderPassInstance{ parserContext, frag, RenderPassBeginDesc{MakeIteratorRange(&clear, &clear+1)}};
	}

	RenderPassInstance RenderPassToPresentationTargetWithDepthStencil(
		const std::shared_ptr<RenderCore::IResource>& presentationTarget,
        ParsingContext& parserContext,
		LoadStore loadOperation,
		unsigned clearColor)
	{
        parserContext.GetTechniqueContext()._attachmentPool->Bind(AttachmentSemantics::ColorLDR, presentationTarget);
		return RenderPassToPresentationTargetWithDepthStencil(parserContext, loadOperation, clearColor);
	}

	RenderPassInstance RenderPassToDepthStencil(
        ParsingContext& parserContext,
		LoadStore loadOperation,
		ClearValue clearValue)
	{
		FrameBufferDescFragment frag;
		SubpassDesc subpass;
		subpass.AppendOutput(frag.DefineAttachment(AttachmentSemantics::MultisampleDepth).InitialState(loadOperation, 0));
		frag.AddSubpass(std::move(subpass));
        return RenderPassInstance{parserContext, frag, RenderPassBeginDesc{MakeIteratorRange(&clearValue, &clearValue+1)}};	
	}
}}
