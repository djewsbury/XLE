// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "RenderPassUtils.h"
#include "RenderPass.h"
#include "ParsingContext.h"
#include "CommonBindings.h"
#include "Techniques.h"
#include "../IDevice.h"
#include "../IThreadContext.h"
#include "../Format.h"
#include "../../Utility/IteratorUtils.h"

namespace RenderCore { namespace Techniques
{
	RenderPassInstance RenderPassToPresentationTarget(
		IThreadContext& context,
        ParsingContext& parserContext,
		LoadStore loadOperation)
	{
		FrameBufferDescFragment frag;
		SubpassDesc subpass;
		subpass.AppendOutput(frag.DefineAttachment(AttachmentSemantics::ColorLDR).InitialState(loadOperation, 0));
		frag.AddSubpass(std::move(subpass));
        return RenderPassInstance{context, parserContext, frag};
	}

	RenderPassInstance RenderPassToPresentationTarget(
		IThreadContext& context,
		const RenderCore::IResourcePtr& presentationTarget,
        ParsingContext& parserContext,
		LoadStore loadOperation)
	{
		parserContext.GetTechniqueContext()._attachmentPool->Bind(AttachmentSemantics::ColorLDR, presentationTarget);
		return RenderPassToPresentationTarget(context, parserContext, loadOperation);
	}

	RenderPassInstance RenderPassToPresentationTargetWithDepthStencil(
		IThreadContext& context,
        ParsingContext& parserContext,
		LoadStore loadOperation)
	{
		auto boundDepth = parserContext.GetTechniqueContext()._attachmentPool->GetBoundResource(AttachmentSemantics::MultisampleDepth);
		if (!boundDepth && loadOperation != LoadStore::Clear)
			return RenderPassToPresentationTarget(context, parserContext, loadOperation);

		FrameBufferDescFragment frag;
		SubpassDesc subpass;
		subpass.AppendOutput(frag.DefineAttachment(AttachmentSemantics::ColorLDR).InitialState(loadOperation, 0));
		subpass.SetDepthStencil(frag.DefineAttachment(AttachmentSemantics::MultisampleDepth).InitialState(loadOperation, 0));
		frag.AddSubpass(std::move(subpass));

        return RenderPassInstance{ context, parserContext, frag };
	}

	RenderPassInstance RenderPassToPresentationTargetWithDepthStencil(
		IThreadContext& context,
		const std::shared_ptr<RenderCore::IResource>& presentationTarget,
        ParsingContext& parserContext,
		LoadStore loadOperation)
	{
        parserContext.GetTechniqueContext()._attachmentPool->Bind(AttachmentSemantics::ColorLDR, presentationTarget);
		return RenderPassToPresentationTargetWithDepthStencil(context, parserContext, loadOperation);
	}
}}
