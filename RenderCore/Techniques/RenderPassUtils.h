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
		ParsingContext& parsingContext,
		uint64_t semantic);

	IResource* GetAttachmentResourceAndBarrierToLayout(
		ParsingContext& parsingContext,
		uint64_t semantic,
		BindFlag::BitField newLayout);

	std::vector<PreregisteredAttachment> InitializeColorLDR(
		IteratorRange<const PreregisteredAttachment*>);

	std::vector<PreregisteredAttachment> ConfigureCommonOverlayAttachments(
		IteratorRange<const PreregisteredAttachment*> systemPreregs,
		const FrameBufferProperties& fbProps,
		IteratorRange<const Format*> systemAttachmentFormats);

	struct AttachmentLoadStore
	{
		LoadStore _loadStore;
		BindFlag::BitField _layout;

		static AttachmentLoadStore NoState() { return AttachmentLoadStore{LoadStore::DontCare, 0}; };
		static AttachmentLoadStore Discard() { return AttachmentLoadStore{LoadStore::DontCare, 0}; };
		static AttachmentLoadStore Clear() { return AttachmentLoadStore{LoadStore::Clear, 0}; };
		AttachmentLoadStore(BindFlag::BitField layout) : _loadStore(LoadStore::Retain), _layout(layout) {}
		AttachmentLoadStore(LoadStore loadStore, BindFlag::BitField layout) : _loadStore(loadStore), _layout(layout) {}
	};

	struct SelfContainedRenderPassHelper
	{
		Techniques::FrameBufferDescFragment _workingFragment;
		std::vector<IResource*> _attachments;

		SelfContainedRenderPassHelper&& AppendOutput(IResource& resource, AttachmentLoadStore initialState, AttachmentLoadStore finalState, const TextureViewDesc& = {});
		SelfContainedRenderPassHelper&& SetDepthStencil(IResource& resource, AttachmentLoadStore initialState, AttachmentLoadStore finalState, const TextureViewDesc& = {});
		SelfContainedRenderPassHelper&& AppendNonFrameBufferAttachmentView(IResource& resource, BindFlag::Enum usage = BindFlag::ShaderResource, const TextureViewDesc& = {});

		RenderPassInstance Complete(ParsingContext&, IFrameBufferPool&);
		SelfContainedRenderPassHelper(std::string subpassName = {});
	};


}}
