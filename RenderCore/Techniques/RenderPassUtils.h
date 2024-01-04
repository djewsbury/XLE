// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "RenderPass.h"
#include "../FrameBufferDesc.h"		// (for LoadStore)
#include <memory>

namespace RenderCore { class IThreadContext; class IResource; }
namespace RenderCore { namespace BindFlag { using BitField = unsigned; }}

namespace RenderCore { namespace Techniques
{
	class RenderPassInstance;
	class ParsingContext;

	RenderPassInstance RenderPassToPresentationTarget(
		ParsingContext& parserContext,
		LoadStore loadOperation = LoadStore::Retain,
		unsigned clearColor = 0xff000000);

	RenderPassInstance RenderPassToPresentationTargetWithOptionalInitialize(ParsingContext& parserContext);

	RenderPassInstance RenderPassToPresentationTarget(
		const std::shared_ptr<IResource>& presentationTarget,
		ParsingContext& parserContext,
		LoadStore loadOperation = LoadStore::Retain,
		unsigned clearColor = 0xff000000);

	RenderPassInstance RenderPassToPresentationTargetWithDepthStencil(
		ParsingContext& parserContext,
		LoadStore loadOperation = LoadStore::Retain,
		unsigned clearColor = 0xff000000);
	
	RenderPassInstance RenderPassToPresentationTargetWithDepthStencil(
		const std::shared_ptr<IResource>& presentationTarget,
		ParsingContext& parserContext,
		LoadStore loadOperation = LoadStore::Retain,
		unsigned clearColor = 0xff000000);

	RenderPassInstance RenderPassToDepthStencil(
		ParsingContext& parserContext,
		LoadStore loadOperation = LoadStore::Retain,
		ClearValue clearValue = MakeClearValue(0.f, 0));

	IResource* GetAttachmentResource(
		Techniques::ParsingContext& parsingContext,
		uint64_t semantic);

	IResource* GetAttachmentResourceAndBarrierToLayout(
		Techniques::ParsingContext& parsingContext,
		uint64_t semantic,
		BindFlag::BitField newLayout);

	std::vector<Techniques::PreregisteredAttachment> InitializeColorLDR(
		IteratorRange<const Techniques::PreregisteredAttachment*>);

	std::vector<Techniques::PreregisteredAttachment> ConfigureCommonOverlayAttachments(
		IteratorRange<const Techniques::PreregisteredAttachment*> systemPreregs,
		const FrameBufferProperties& fbProps,
		IteratorRange<const Format*> systemAttachmentFormats);


}}
