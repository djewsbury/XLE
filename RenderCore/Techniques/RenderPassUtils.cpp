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
		subpass.AppendOutput(frag.DefineAttachment(AttachmentSemantics::ColorLDR).InitialState(loadOperation));
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
		parserContext.GetAttachmentReservation().Bind(AttachmentSemantics::ColorLDR, presentationTarget, BindFlag::Enum(0));
		return RenderPassToPresentationTarget(parserContext, loadOperation, clearColor);
	}

	RenderPassInstance RenderPassToPresentationTargetWithDepthStencil(
        ParsingContext& parserContext,
		LoadStore loadOperation,
		unsigned clearColor)
	{
		auto boundDepth = parserContext.GetAttachmentReservation().GetSemanticResource(AttachmentSemantics::MultisampleDepth).get();
		if (!boundDepth && loadOperation != LoadStore::Clear)
			return RenderPassToPresentationTarget(parserContext, loadOperation, clearColor);

		FrameBufferDescFragment frag;
		SubpassDesc subpass;
		subpass.AppendOutput(frag.DefineAttachment(AttachmentSemantics::ColorLDR).InitialState(loadOperation));
		subpass.SetDepthStencil(frag.DefineAttachment(AttachmentSemantics::MultisampleDepth).InitialState(loadOperation));
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
        parserContext.GetAttachmentReservation().Bind(AttachmentSemantics::ColorLDR, presentationTarget, BindFlag::Enum(0));
		return RenderPassToPresentationTargetWithDepthStencil(parserContext, loadOperation, clearColor);
	}

	RenderPassInstance RenderPassToDepthStencil(
        ParsingContext& parserContext,
		LoadStore loadOperation,
		ClearValue clearValue)
	{
		FrameBufferDescFragment frag;
		SubpassDesc subpass;
		subpass.AppendOutput(frag.DefineAttachment(AttachmentSemantics::MultisampleDepth).InitialState(loadOperation));
		frag.AddSubpass(std::move(subpass));
        return RenderPassInstance{parserContext, frag, RenderPassBeginDesc{MakeIteratorRange(&clearValue, &clearValue+1)}};	
	}

	IResource* GetAttachmentResource(ParsingContext& parsingContext, uint64_t semantic)
	{
		// Get the attachment bound to the given semantic from the AttachmentReservation in the parsing context
		// This will create the attachment resource if it hasn't been created yet
		// Need to jump through some hoops to do this; because most of the interfaces were built for interacting
		// with render passes
		auto preregs = parsingContext.GetFragmentStitchingContext().GetPreregisteredAttachments();
		auto i = std::find_if(preregs.begin(), preregs.end(), [semantic](const auto& c) { return c._semantic == semantic; });
		if (i == preregs.end())
			return nullptr;
		auto newReservation = parsingContext.GetTechniqueContext()._attachmentPool->Reserve(
			MakeIteratorRange(i, i+1),
			&parsingContext.GetAttachmentReservation());
		assert(newReservation.GetResourceCount() == 1);
		newReservation.CompleteInitialization(parsingContext.GetThreadContext());

		AttachmentTransform transform;
		transform._type = AttachmentTransform::LoadedAndStored;
		transform._initialLayout = i->_layout;
		transform._finalLayout = i->_layout;
		parsingContext.GetAttachmentReservation().UpdateAttachments(newReservation, MakeIteratorRange(&transform, &transform+1));

		return newReservation.GetResource(0).get();

	}
}}
