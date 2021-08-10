// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "FrameBuffer.h"
#include "Format.h"
#include "Resource.h"
#include "TextureView.h"
#include "DeviceContext.h"
#include "ObjectFactory.h"
#include "Pools.h"
#include "../../ResourceUtils.h"
#include "../../Format.h"
#include "../../../OSServices/Log.h"
#include "../../../Utility/MemoryUtils.h"
#include "../../../Utility/Streams/SerializationUtils.h"
#include <cmath>

std::ostream& SerializationOperator(std::ostream& str, const VkRenderPassCreateInfo2& rp_info);

namespace RenderCore { namespace Metal_Vulkan
{
    static VkAttachmentLoadOp AsLoadOp(LoadStore loadStore)
    {
        switch (loadStore)
        {
        default:
        case LoadStore::DontCare: 
        case LoadStore::DontCare_StencilRetain: 
        case LoadStore::DontCare_StencilClear: 
            return VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        case LoadStore::Retain: 
        case LoadStore::Retain_StencilDontCare: 
        case LoadStore::Retain_StencilClear: 
            return VK_ATTACHMENT_LOAD_OP_LOAD;
        case LoadStore::Clear: 
        case LoadStore::Clear_StencilDontCare: 
        case LoadStore::Clear_StencilRetain: 
            return VK_ATTACHMENT_LOAD_OP_CLEAR;
        }
    }

    static VkAttachmentStoreOp AsStoreOp(LoadStore loadStore)
    {
        switch (loadStore)
        {
        default:
            return VK_ATTACHMENT_STORE_OP_DONT_CARE;
        case LoadStore::Retain: 
        case LoadStore::Retain_StencilDontCare: 
        case LoadStore::Retain_StencilClear: 
            return VK_ATTACHMENT_STORE_OP_STORE;
        }
    }

    static VkAttachmentLoadOp AsLoadOpStencil(LoadStore loadStore)
    {
        switch (loadStore)
        {
        default:
        case LoadStore::DontCare: 
        case LoadStore::Retain_StencilDontCare: 
        case LoadStore::Clear_StencilDontCare: 
            return VK_ATTACHMENT_LOAD_OP_DONT_CARE;

        case LoadStore::Clear: 
        case LoadStore::DontCare_StencilClear: 
        case LoadStore::Retain_StencilClear: 
            return VK_ATTACHMENT_LOAD_OP_CLEAR;

        case LoadStore::Retain: 
        case LoadStore::DontCare_StencilRetain: 
        case LoadStore::Clear_StencilRetain: 
            return VK_ATTACHMENT_LOAD_OP_LOAD;
        }
    }

    static VkAttachmentStoreOp AsStoreOpStencil(LoadStore loadStore)
    {
        switch (loadStore)
        {
        default:
            return VK_ATTACHMENT_STORE_OP_DONT_CARE;

        case LoadStore::Retain: 
        case LoadStore::DontCare_StencilRetain: 
        case LoadStore::Clear_StencilRetain: 
            return VK_ATTACHMENT_STORE_OP_STORE;
        }
    }

	namespace Internal
	{
		namespace AttachmentResourceUsageType
		{
			enum Flags
			{
				Input = 1<<0, Output = 1<<1, DepthStencil = 1<<2,
				HintGeneral = 1<<3		// if "general" is explicitly requested in the input FrameBufferDesc
			};
			using BitField = unsigned;
		}
	}

	static bool HasRetain(LoadStore loadStore)
	{
        return  loadStore == LoadStore::Retain
            ||  loadStore == LoadStore::DontCare_StencilRetain
            ||  loadStore == LoadStore::Clear_StencilRetain
            ||  loadStore == LoadStore::Retain_StencilDontCare
            ||  loadStore == LoadStore::Retain_StencilClear
            ;
	}

	static void MergeFormatFilter(TextureViewDesc::FormatFilter& dst, TextureViewDesc::FormatFilter src)
	{
		if (dst._aspect == TextureViewDesc::Aspect::Depth) {
			if (src._aspect == TextureViewDesc::Aspect::DepthStencil || src._aspect == TextureViewDesc::Aspect::Stencil) {
				dst = TextureViewDesc::Aspect::DepthStencil;
				return;
			}
		} else if (dst._aspect == TextureViewDesc::Aspect::Stencil) {
			if (src._aspect == TextureViewDesc::Aspect::Depth || src._aspect == TextureViewDesc::Aspect::DepthStencil) {
				dst = TextureViewDesc::Aspect::DepthStencil;
				return;
			}
		} else if (dst._aspect == TextureViewDesc::Aspect::DepthStencil) {
			if (src._aspect == TextureViewDesc::Aspect::Depth || src._aspect == TextureViewDesc::Aspect::Stencil || src._aspect == TextureViewDesc::Aspect::DepthStencil) {
				dst = TextureViewDesc::Aspect::DepthStencil;
				return;
			}
		}

		assert(dst._aspect == src._aspect
			|| dst._aspect == TextureViewDesc::Aspect::UndefinedAspect
			|| src._aspect == TextureViewDesc::Aspect::UndefinedAspect);
		if (src._aspect != TextureViewDesc::Aspect::UndefinedAspect)
			dst._aspect = src._aspect;
	}

	static VkImageLayout LayoutFromBindFlagsAndUsage(BindFlag::BitField bindFlags, Internal::AttachmentResourceUsageType::BitField usage)
	{
		if (bindFlags == BindFlag::ShaderResource) {
			return VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		} else if (bindFlags == BindFlag::InputAttachment) {
			return VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		} else if (bindFlags == BindFlag::TransferSrc) {
			return VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
		} else if (bindFlags == BindFlag::TransferDst) {
			return VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		} else if (bindFlags == BindFlag::PresentationSrc) {
			return VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
		} else if (bindFlags == BindFlag::RenderTarget) {
			return VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		} else if (bindFlags == BindFlag::DepthStencil) {
			//
			// VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,
			// VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_STENCIL_ATTACHMENT_OPTIMAL
			// VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_STENCIL_READ_ONLY_OPTIMAL,
			// VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL
			// VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL
			// VK_IMAGE_LAYOUT_STENCIL_ATTACHMENT_OPTIMAL
			// VK_IMAGE_LAYOUT_STENCIL_READ_ONLY_OPTIMAL
			// are not accessible here -- but would it be useful?
			//
			return VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
		} else if (bindFlags != 0) {
			return VK_IMAGE_LAYOUT_GENERAL;
		} else {
			bool isDepthStencil = !!(usage & unsigned(Internal::AttachmentResourceUsageType::DepthStencil));
			bool isColorOutput = !!(usage & unsigned(Internal::AttachmentResourceUsageType::Output));
			bool isAttachmentInput = !!(usage & unsigned(Internal::AttachmentResourceUsageType::Input));
			bool hintGeneral = !!(usage & unsigned(Internal::AttachmentResourceUsageType::HintGeneral));
			if (hintGeneral) {
				return VK_IMAGE_LAYOUT_GENERAL;
			} else if (isDepthStencil) {
				assert(!isColorOutput);
				if (isAttachmentInput)
					return VK_IMAGE_LAYOUT_GENERAL;
				return VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
			} else if (isColorOutput) {
				if (isAttachmentInput)
					return VK_IMAGE_LAYOUT_GENERAL;
				return VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
			} else if (isAttachmentInput) {
				return VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			} else {
				// (sometimes we use this function just to convert from BindFlag to a VkImageLayout -- in which case we can get here)
				return VK_IMAGE_LAYOUT_UNDEFINED;
			}
		}
	}

	VkImageAspectFlags GetAspectForTextureView(const TextureViewDesc& window);

	static VkAttachmentReference2 MakeAttachmentReference(uint32_t attachmentName, Internal::AttachmentResourceUsageType::BitField usage, const TextureViewDesc& window)
	{
		return VkAttachmentReference2 {
			VK_STRUCTURE_TYPE_ATTACHMENT_REFERENCE_2,
			nullptr,
			attachmentName,
			LayoutFromBindFlagsAndUsage(0, usage),
			GetAspectForTextureView(window)};
	}

	class RenderPassHelper 
	{
	public:
		// Attachment resources & views
		// In FrameBufferDesc, each "attachment" is a different resource, which we assume never aliases/overlaps with any other resource
		// & we can have "views" for that attachment in the subpasses
		//
		// In Vulkan renderpasses & frame buffers, each "attachment" has only one view. So if we have a single resource that's viewed
		// in multiple different ways, we must declare it as a completely different attachment
		//
		// We need to distinguish between separate resources and multiple views on the same resource for things like image layouts
		// (which are per-resource, not per-view)
		//
		// So let's just have a way to convert from one system to another & find a compressed set of attachment resources & views

		struct WorkingAttachmentResource
		{
			AttachmentDesc _desc;
			Internal::AttachmentResourceUsageType::BitField _attachmentUsage = 0;
		};
		std::vector<std::pair<AttachmentName, WorkingAttachmentResource>> workingAttachments;

		struct WorkingViewedAttachment
		{
			unsigned _mappedAttachmentIdx;
			TextureViewDesc _view;
			uint64_t _viewHash;
		};
		std::vector<WorkingViewedAttachment> workingViewedAttachments;

		struct SubpassDependency
		{
			AttachmentName _resource;

			unsigned _subpassFirst = ~0u;
			Internal::AttachmentResourceUsageType::BitField _usageFirst = 0;

			unsigned _subpassSecond = ~0u;
			Internal::AttachmentResourceUsageType::BitField _usageSecond = 0;
		};
		std::vector<SubpassDependency> dependencies;

		VkAttachmentDescription2 CreateAttachmentDescription(AttachmentName resName, const WorkingAttachmentResource& res, const TextureViewDesc& view)
		{
			const auto& attachmentDesc = res._desc;
			
			// We need to look through all of the places we use this attachment to finalize the
			// format filter
            TextureViewDesc::FormatFilter formatFilter { Format::Unknown };
			for (const auto& spDesc:_layout->GetSubpasses()) {
				for (const auto& r:spDesc.GetOutputs())
					if (r._resourceName == resName)
						MergeFormatFilter(formatFilter, r._window._format);
				if (spDesc.GetDepthStencil()._resourceName == resName)
					MergeFormatFilter(formatFilter, spDesc.GetDepthStencil()._window._format);
				for (const auto& r:spDesc.GetInputs())
					if (r._resourceName == resName)
						MergeFormatFilter(formatFilter, r._window._format);
			}

            BindFlag::Enum formatUsage = BindFlag::ShaderResource;
            if (res._attachmentUsage & Internal::AttachmentResourceUsageType::Output) formatUsage = BindFlag::RenderTarget;
            if (res._attachmentUsage & Internal::AttachmentResourceUsageType::DepthStencil) formatUsage = BindFlag::DepthStencil;
            auto resolvedFormat = ResolveFormat(attachmentDesc._format, formatFilter, formatUsage);

			LoadStore originalLoad = attachmentDesc._loadFromPreviousPhase;
			LoadStore finalStore = attachmentDesc._storeToNextPhase;

            VkAttachmentDescription2 desc;
			desc.sType = VK_STRUCTURE_TYPE_ATTACHMENT_DESCRIPTION_2;
			desc.pNext = nullptr;
            desc.flags = 0;
            desc.format = (VkFormat)AsVkFormat(resolvedFormat);
            desc.samples = VK_SAMPLE_COUNT_1_BIT;
            desc.loadOp = AsLoadOp(originalLoad);
            desc.stencilLoadOp = AsLoadOpStencil(originalLoad);
			desc.storeOp = AsStoreOp(finalStore);
            desc.stencilStoreOp = AsStoreOpStencil(finalStore);
			assert(desc.format != VK_FORMAT_UNDEFINED);

			desc.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			desc.finalLayout = VK_IMAGE_LAYOUT_UNDEFINED;

			// If we're loading or storing the data, we should set the initial and final layouts
			// If the attachment desc has initial and/or final layout flags, those take precidence
			// Otherwise 
			if (HasRetain(attachmentDesc._loadFromPreviousPhase))
				desc.initialLayout = LayoutFromBindFlagsAndUsage(attachmentDesc._initialLayout, res._attachmentUsage);

			// Even if we don't have a "retain" on the store operation, we're still supposed to give the attachment
			// a final layout. Using "undefined" here results in a validation warning
			desc.finalLayout = LayoutFromBindFlagsAndUsage(attachmentDesc._finalLayout, res._attachmentUsage);

            if (attachmentDesc._flags & AttachmentDesc::Flags::Multisampled)
                desc.samples = (VkSampleCountFlagBits)AsSampleCountFlagBits(_layout->GetProperties()._samples);
			return desc;
		}

		static bool ViewsOverlap(const TextureViewDesc& lhs, const TextureViewDesc& rhs) 
		{
			// check array layers & mip levels to ensure they overlap
			auto mipLevelOverlap = true, arrayLayerOverlap = true;
			{
				unsigned lhsMipEnd = (lhs._mipRange._count == TextureViewDesc::All._count) ? ~0u : (lhs._mipRange._min + lhs._mipRange._count);
				unsigned rhsMipEnd = (rhs._mipRange._count == TextureViewDesc::All._count) ? ~0u : (rhs._mipRange._min + rhs._mipRange._count);
				if ((rhsMipEnd <= lhs._mipRange._min) || (rhs._mipRange._min >= lhsMipEnd))
					mipLevelOverlap = false;
			}

			{
				unsigned lhsLayerEnd = (lhs._arrayLayerRange._count == TextureViewDesc::All._count) ? ~0u : (lhs._arrayLayerRange._min + lhs._arrayLayerRange._count);
				unsigned rhsLayerEnd = (rhs._arrayLayerRange._count == TextureViewDesc::All._count) ? ~0u : (rhs._arrayLayerRange._min + rhs._arrayLayerRange._count);
				if ((rhsLayerEnd <= lhs._arrayLayerRange._min) || (rhs._arrayLayerRange._min >= lhsLayerEnd))
					arrayLayerOverlap = false;
			}

			return mipLevelOverlap && arrayLayerOverlap;
		}

		VkAttachmentReference2 CreateAttachmentReference(AttachmentName resourceName, const TextureViewDesc& view, Internal::AttachmentResourceUsageType::BitField subpassUsage, unsigned subpassIdx)
		{
			auto i = FindIf(workingAttachments, [resourceName](auto& i) { return i.first == resourceName; });
			if (i == workingAttachments.end()) {
				i = workingAttachments.insert(workingAttachments.end(), {resourceName, WorkingAttachmentResource{}});
				assert(resourceName < _layout->GetAttachments().size());
				i->second._desc = _layout->GetAttachments()[resourceName];

				// If we're loading from general or storing to general, then we should encourage use of general
				// within the render pass, also
				if (HasRetain(i->second._desc._loadFromPreviousPhase) && LayoutFromBindFlagsAndUsage(i->second._desc._initialLayout, 0) == VK_IMAGE_LAYOUT_GENERAL)
					i->second._attachmentUsage |= Internal::AttachmentResourceUsageType::HintGeneral;
				if (HasRetain(i->second._desc._storeToNextPhase) && LayoutFromBindFlagsAndUsage(i->second._desc._finalLayout, 0) == VK_IMAGE_LAYOUT_GENERAL)
					i->second._attachmentUsage |= Internal::AttachmentResourceUsageType::HintGeneral;
			}

			i->second._attachmentUsage |= subpassUsage;
			auto mappedAttachmentIdx = (unsigned)std::distance(workingAttachments.begin(), i);
			uint64_t viewHash = view.GetHash();
			auto i2 = FindIf(workingViewedAttachments, [viewHash, mappedAttachmentIdx](auto& i) { 
				return i._mappedAttachmentIdx == mappedAttachmentIdx && i._viewHash == viewHash; });
			if (i2 == workingViewedAttachments.end())
				i2 = workingViewedAttachments.insert(workingViewedAttachments.end(), WorkingViewedAttachment{mappedAttachmentIdx, view, viewHash});

			// check dependencies now. If there's a previous subpass that uses this resource and the view overlap, and the usages contain output somewhere, then we need a dependency
			// We need to do this sometimes even when it seems like it shouldn't be required -- such as 2 subpasses that use the same depth/stencil buffer (otherwise we get validation errors)
			for (int spCheckIdx=subpassIdx-1; spCheckIdx>=0; spCheckIdx--) {
				const auto& spCheck = _layout->GetSubpasses()[spCheckIdx];
				Internal::AttachmentResourceUsageType::BitField flags = 0;
				for (const auto& a:spCheck.GetOutputs()) if (a._resourceName == resourceName && ViewsOverlap(a._window, view)) flags |= Internal::AttachmentResourceUsageType::Output;
				for (const auto& a:spCheck.GetResolveOutputs()) if (a._resourceName == resourceName && ViewsOverlap(a._window, view)) flags |= Internal::AttachmentResourceUsageType::Output;		// resolve usage type?
				if (spCheck.GetDepthStencil()._resourceName == resourceName && ViewsOverlap(spCheck.GetDepthStencil()._window, view)) flags |= Internal::AttachmentResourceUsageType::DepthStencil;
				if (spCheck.GetResolveDepthStencil()._resourceName == resourceName && ViewsOverlap(spCheck.GetResolveDepthStencil()._window, view)) flags |= Internal::AttachmentResourceUsageType::Output;
				for (const auto& a:spCheck.GetInputs()) if (a._resourceName == resourceName && ViewsOverlap(a._window, view)) flags |= Internal::AttachmentResourceUsageType::Input;
				// todo -- for attachments registered as non-framebuffer-views, we can't check if a dependency is required
				if (flags) {
					dependencies.push_back({resourceName, (unsigned)spCheckIdx, flags, subpassIdx, subpassUsage});
					// don't go back any further than the first subpass dependency for any given resource. If the resource
					// is used by a previous subpass; then the subpass spCheckIdx will already have that dependency 
					break;
				}
			}

			return MakeAttachmentReference((uint32_t)std::distance(workingViewedAttachments.begin(), i2), subpassUsage, view);
		}
		
		RenderPassHelper(const FrameBufferDesc& layout)
		: _layout(&layout)
		{
			auto subpasses = layout.GetSubpasses();
			workingAttachments.reserve(subpasses.size()*2);	// approximate
			dependencies.reserve(subpasses.size()*2);	// approximate
		}

		const FrameBufferDesc* _layout;
	};

	VulkanUniquePtr<VkRenderPass> CreateVulkanRenderPass(
        const Metal_Vulkan::ObjectFactory& factory,
        const FrameBufferDesc& layout)
	{
		const auto subpasses = layout.GetSubpasses();
		auto samples = layout.GetProperties()._samples;
		auto attachmentCount = layout.GetAttachments().size();
		RenderPassHelper helper{layout};

		////////////////////////////////////////////////////////////////////////////////////
		// Build the VkSubpassDescription objects
		std::vector<VkAttachmentReference2> attachReferences;
        std::vector<VkSubpassDescription2> subpassDesc;
        subpassDesc.reserve(subpasses.size());
        for (unsigned spIdx=0; spIdx<subpasses.size(); ++spIdx) {
			const auto& spDesc = subpasses[spIdx];
            VkSubpassDescription2 desc;
			desc.sType = VK_STRUCTURE_TYPE_SUBPASS_DESCRIPTION_2;
			desc.pNext = nullptr;
            desc.flags = 0;
            desc.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
			desc.viewMask = 0;

			Internal::AttachmentResourceUsageType::BitField subpassAttachmentUsages[attachmentCount];
			for (unsigned c=0; c<attachmentCount; ++c) subpassAttachmentUsages[c] = 0;

			for (const auto& r:spDesc.GetOutputs()) 
				subpassAttachmentUsages[r._resourceName] |= Internal::AttachmentResourceUsageType::Output;
			for (const auto& r:spDesc.GetInputs()) 
				subpassAttachmentUsages[r._resourceName] |= Internal::AttachmentResourceUsageType::Input;
			if (spDesc.GetDepthStencil()._resourceName != SubpassDesc::Unused._resourceName)
				subpassAttachmentUsages[spDesc.GetDepthStencil()._resourceName] |= Internal::AttachmentResourceUsageType::DepthStencil;
			/*for (const auto& r:spDesc.GetResolveOutputs()) 
				subpassAttachmentUsages[r._resourceName] |= Internal::AttachmentResourceUsageType::Output;
			if (spDesc.GetResolveDepthStencil()._resourceName != SubpassDesc::Unused._resourceName)
				subpassAttachmentUsages[spDesc.GetDepthStencil()._resourceName] |= Internal::AttachmentResourceUsageType::Output;*/

            // Input attachments are going to be difficult, because they must be bound both
            // by the sub passes and by the descriptor set! (and they must be explicitly listed as
            // input attachments in the shader). Holy cow, the render pass, frame buffer, pipeline
            // layout, descriptor set and shader must all agree!
            auto beforeInputs = attachReferences.size();
            for (auto& a:spDesc.GetInputs())
				attachReferences.push_back(helper.CreateAttachmentReference(a._resourceName, a._window, subpassAttachmentUsages[a._resourceName], spIdx));
            desc.pInputAttachments = (const VkAttachmentReference2*)(beforeInputs+1);
            desc.inputAttachmentCount = uint32_t(attachReferences.size() - beforeInputs);

            auto beforeOutputs = attachReferences.size();
            for (auto& a:spDesc.GetOutputs())
				attachReferences.push_back(helper.CreateAttachmentReference(a._resourceName, a._window, subpassAttachmentUsages[a._resourceName], spIdx));
            desc.pColorAttachments = (const VkAttachmentReference2*)(beforeOutputs+1);
            desc.colorAttachmentCount = uint32_t(attachReferences.size() - beforeOutputs);
            desc.pResolveAttachments = nullptr; // not supported
			desc.pPreserveAttachments = nullptr;
			desc.preserveAttachmentCount = 0;

            if (spDesc.GetDepthStencil()._resourceName != SubpassDesc::Unused._resourceName) {
				const auto& a = spDesc.GetDepthStencil();
				desc.pDepthStencilAttachment = (const VkAttachmentReference2*)(attachReferences.size()+1);
				attachReferences.push_back(helper.CreateAttachmentReference(a._resourceName, a._window, subpassAttachmentUsages[a._resourceName], spIdx));
            } else {
                desc.pDepthStencilAttachment = nullptr;
            }

			// preserve & resolve attachments not supported currently

            subpassDesc.push_back(desc);
        }

        // we need to do a fixup pass over all of the subpasses to generate correct pointers
        for (auto&p:subpassDesc) {
            if (p.pInputAttachments)
                p.pInputAttachments = AsPointer(attachReferences.begin()) + size_t(p.pInputAttachments)-1;
            if (p.pColorAttachments)
                p.pColorAttachments = AsPointer(attachReferences.begin()) + size_t(p.pColorAttachments)-1;
            if (p.pResolveAttachments)
                p.pResolveAttachments = AsPointer(attachReferences.begin()) + size_t(p.pResolveAttachments)-1;
            if (p.pDepthStencilAttachment)
                p.pDepthStencilAttachment = AsPointer(attachReferences.begin()) + size_t(p.pDepthStencilAttachment)-1;
        }

		////////////////////////////////////////////////////////////////////////////////////
		// Build the VkAttachmentDescription objects
        std::vector<VkAttachmentDescription2> attachmentDescs;
		attachmentDescs.reserve(helper.workingViewedAttachments.size());
		for (const auto& a:helper.workingViewedAttachments) {
			auto& res = helper.workingAttachments[a._mappedAttachmentIdx];
			attachmentDescs.push_back(helper.CreateAttachmentDescription(res.first, res.second, a._view));
		}

		////////////////////////////////////////////////////////////////////////////////////
		// Build the VkSubpassDependency objects

        std::vector<VkSubpassDependency2> vkDeps;
		for (unsigned c=0;c<unsigned(subpasses.size()); ++c) {
			// Find the list of SubPassDependency objects where _second is this subpass. We'll
			// then find the unique list of subpasses referenced by _first, and generate the
			// Vulkan object from them.
			//
			// Note that there are implicit dependencies to "VK_SUBPASS_EXTERNAL" which are defined
			// with a standard form. We'll rely on those implicit dependencies, rather than 
			// explicitly creating them here.

			std::vector<RenderPassHelper::SubpassDependency> terminatingDependencies;
			for (const auto& d:helper.dependencies)
				if (d._subpassSecond == c && d._subpassFirst != ~0u)
					terminatingDependencies.push_back(d);

			std::vector<VkSubpassDependency2> deps;
			for (const auto& d:terminatingDependencies) {
				auto i = std::find_if(
					deps.begin(), deps.end(),
					[&d](const VkSubpassDependency2& vkd) { return vkd.srcSubpass == d._subpassFirst; });
				if (i == deps.end())
					i = deps.insert(deps.end(), {
						VK_STRUCTURE_TYPE_SUBPASS_DEPENDENCY_2, nullptr,
						d._subpassFirst, c, 
						0, 0, 0, 0,	// mask and access flags set below
						0, 0});

				// note -- making assumptions about attachments usage here -- (in particular, ignoring shader resources bound to shaders other than the fragment shader)
				if (d._usageFirst & Internal::AttachmentResourceUsageType::Output) {
					i->srcAccessMask |= VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
					i->srcStageMask |= VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
				}
				if (d._usageFirst & Internal::AttachmentResourceUsageType::DepthStencil) {
					i->srcAccessMask |= VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
					i->srcStageMask |= VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
				}
				if (d._usageFirst & Internal::AttachmentResourceUsageType::Input) {
					i->srcAccessMask |= VK_ACCESS_INPUT_ATTACHMENT_READ_BIT;
					i->srcStageMask |= VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
				}

				if (d._usageSecond & Internal::AttachmentResourceUsageType::Output) {
					i->dstAccessMask |= VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
					i->dstStageMask |= VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
				}
				if (d._usageSecond & Internal::AttachmentResourceUsageType::DepthStencil) {
					i->dstAccessMask |= VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
					i->dstStageMask |= VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
				}
				if (d._usageSecond & Internal::AttachmentResourceUsageType::Input) {
					i->dstAccessMask |= VK_ACCESS_INPUT_ATTACHMENT_READ_BIT;
					i->dstStageMask |= VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
				}
			}

			vkDeps.insert(vkDeps.end(), deps.begin(), deps.end());
        }

		/*
			Vulkan samples tend to setup something like this:

			    // Subpass dependency to wait for wsi image acquired semaphore before starting layout transition
			VkSubpassDependency subpass_dependency = {};
			subpass_dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
			subpass_dependency.dstSubpass = 0;
			subpass_dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
			subpass_dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
			subpass_dependency.srcAccessMask = 0;
			subpass_dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
			subpass_dependency.dependencyFlags = 0;
		*/

		////////////////////////////////////////////////////////////////////////////////////
		// Build the final render pass object

        VkRenderPassCreateInfo2 rp_info = {};
        rp_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO_2;
        rp_info.pNext = nullptr;
		rp_info.flags = 0;
        rp_info.attachmentCount = (uint32_t)attachmentDescs.size();
        rp_info.pAttachments = attachmentDescs.data();
        rp_info.subpassCount = (uint32_t)subpassDesc.size();
        rp_info.pSubpasses = subpassDesc.data();
        rp_info.dependencyCount = (uint32_t)vkDeps.size();
        rp_info.pDependencies = vkDeps.data();
		rp_info.correlatedViewMaskCount = 0;
		rp_info.pCorrelatedViewMasks = nullptr;

		Log(Verbose) << "Vulkan render pass generated: " << std::endl;
		Log(Verbose) << rp_info << std::endl;

        return factory.CreateRenderPass(rp_info);
	}

	struct MaxDims
	{
		unsigned _width = 0, _height = 0;
		unsigned _layers = 0;
	};

	static void BuildMaxDims(MaxDims& result, const ResourceDesc& desc, const TextureViewDesc& view)
	{
		assert(desc._type == ResourceDesc::Type::Texture);
		result._width = std::max(result._width, desc._textureDesc._width);
		result._height = std::max(result._height, desc._textureDesc._height);

		unsigned layerCount = view._arrayLayerRange._count;
		if (layerCount == TextureViewDesc::All._count)
			layerCount = (unsigned)desc._textureDesc._arrayCount;
		result._layers = std::max(result._layers, layerCount);
	}

	static BindFlag::Enum AsBindFlag(Internal::AttachmentResourceUsageType::BitField usageType)
	{
		if (usageType & Internal::AttachmentResourceUsageType::Output)
			return BindFlag::RenderTarget;

		if (usageType & Internal::AttachmentResourceUsageType::DepthStencil)
			return BindFlag::DepthStencil;

		return BindFlag::InputAttachment;
	}

    FrameBuffer::FrameBuffer(
        const ObjectFactory& factory,
        const FrameBufferDesc& fbDesc,
        const INamedAttachments& namedResources)
    {
		_layout = Internal::VulkanGlobalsTemp::GetInstance()._globalPools->_renderPassPool.CreateVulkanRenderPass(fbDesc);

        // We must create the frame buffer, including all views required.
        // We need to order the list of views in VkFramebufferCreateInfo in the
		// same order as the attachments were defined in the VkRenderPass object.
		auto subpasses = fbDesc.GetSubpasses();

		RenderPassHelper helper(fbDesc);
		unsigned attachmentCount = fbDesc.GetAttachments().size();

		// Duplicate the work from CreateRenderPass in order to prime RenderPassHelper
		// This must create an identical array
		for (unsigned spIdx=0; spIdx<subpasses.size(); ++spIdx) {
			const auto& spDesc = subpasses[spIdx];
			Internal::AttachmentResourceUsageType::BitField subpassAttachmentUsages[attachmentCount];
			for (unsigned c=0; c<attachmentCount; ++c) subpassAttachmentUsages[c] = 0;

			for (const auto& r:spDesc.GetOutputs()) 
				subpassAttachmentUsages[r._resourceName] |= Internal::AttachmentResourceUsageType::Output;
			for (const auto& r:spDesc.GetInputs()) 
				subpassAttachmentUsages[r._resourceName] |= Internal::AttachmentResourceUsageType::Input;
			if (spDesc.GetDepthStencil()._resourceName != SubpassDesc::Unused._resourceName)
				subpassAttachmentUsages[spDesc.GetDepthStencil()._resourceName] |= Internal::AttachmentResourceUsageType::DepthStencil;

            for (auto& a:spDesc.GetInputs())
				helper.CreateAttachmentReference(a._resourceName, a._window, subpassAttachmentUsages[a._resourceName], spIdx);
            for (auto& a:spDesc.GetOutputs())
				helper.CreateAttachmentReference(a._resourceName, a._window, subpassAttachmentUsages[a._resourceName], spIdx);

            if (spDesc.GetDepthStencil()._resourceName != SubpassDesc::Unused._resourceName) {
				const auto& a = spDesc.GetDepthStencil();
				helper.CreateAttachmentReference(a._resourceName, a._window, subpassAttachmentUsages[a._resourceName], spIdx);
            } 
        }

        VkImageView rawViews[16];
		unsigned rawViewCount = 0;
		_clearValuesOrdering.reserve(helper.workingViewedAttachments.size());
        MaxDims maxDims;

        for (const auto&a:helper.workingViewedAttachments) {
			// Note that we can't support TextureViewDesc properly here, because we don't support 
			// the same resource being used with more than one view
			// Note that bind flags/usages are not particular important for texture views in Vulkan
			// so we don't create unique views for each usage type
			auto& res = helper.workingAttachments[a._mappedAttachmentIdx];
			auto rtv = namedResources.GetResourceView(
				res.first, AsBindFlag(res.second._attachmentUsage), a._view,
				fbDesc.GetAttachments()[res.first], fbDesc.GetProperties());
			rawViews[rawViewCount++] = checked_cast<ResourceView*>(rtv.get())->GetImageView();

			ClearValue defaultClearValue = MakeClearValue(0.f, 0.f, 0.f, 1.f);
			if (res.second._attachmentUsage & Internal::AttachmentResourceUsageType::DepthStencil)
				defaultClearValue = MakeClearValue(1.0f, 0);
			_clearValuesOrdering.push_back({res.first, defaultClearValue});

			BuildMaxDims(maxDims, rtv->GetResource()->GetDesc(), a._view);
			_retainedViews.push_back(std::move(rtv));
        }

		if (rawViewCount == 0 && maxDims._width == 0 && maxDims._height == 0) {
			// It's valid to create a frame buffer with no attachments (eg, for stream output)
			// We still need width & height in these cases, though
			// This will effect the default viewport/scissor when using stream output -- but otherwise
			// it might be ot ok to just use arbitrary values?
			maxDims._width = maxDims._height = 256;
		}

        VkFramebufferCreateInfo fb_info = {};
        fb_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fb_info.pNext = nullptr;
        fb_info.renderPass = _layout.get();
        fb_info.attachmentCount = rawViewCount;
        fb_info.pAttachments = rawViews;
        fb_info.width = maxDims._width;
        fb_info.height = maxDims._height;
        fb_info.layers = std::max(1u, maxDims._layers);
        _underlying = factory.CreateFramebuffer(fb_info);
		_subpassCount = (unsigned)fbDesc.GetSubpasses().size();

        // todo --  do we need to create a "patch up" command buffer to assign the starting image layouts
        //          for all of the images we created?

		_defaultOffset = {0,0};
		_defaultExtent = {maxDims._width, maxDims._height};
		_defaultViewport = ViewportDesc { 0.f, 0.f, (float)maxDims._width, (float)maxDims._height };
    }

	FrameBuffer::FrameBuffer() : _subpassCount(0) {}
    FrameBuffer::~FrameBuffer() {}

}}

std::ostream& SerializationOperator(std::ostream& str, VkAttachmentLoadOp loadOp)
{
	switch (loadOp) {
	case VK_ATTACHMENT_LOAD_OP_LOAD: return str << "load";
    case VK_ATTACHMENT_LOAD_OP_CLEAR: return str << "clear";
    case VK_ATTACHMENT_LOAD_OP_DONT_CARE: return str << "dontcare";
	default: return str << "<<unknown>>";
	}
}

std::ostream& SerializationOperator(std::ostream& str, VkAttachmentStoreOp loadOp)
{
	switch (loadOp) {
	case VK_ATTACHMENT_STORE_OP_STORE: return str << "store";
    case VK_ATTACHMENT_STORE_OP_DONT_CARE: return str << "dontcare";
	default: return str << "<<unknown>>";
	}
}

std::ostream& SerializationOperator(std::ostream& str, VkImageLayout layout)
{
	switch (layout) {
    case VK_IMAGE_LAYOUT_UNDEFINED: return str << "UNDEFINED";
    case VK_IMAGE_LAYOUT_GENERAL: return str << "GENERAL";
    case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL: return str << "COLOR_ATTACHMENT_OPTIMAL";
    case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL: return str << "DEPTH_STENCIL_ATTACHMENT_OPTIMAL";
    case VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL: return str << "DEPTH_STENCIL_READ_ONLY_OPTIMAL";
    case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL: return str << "SHADER_READ_ONLY_OPTIMAL";
    case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL: return str << "TRANSFER_SRC_OPTIMAL";
    case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL: return str << "TRANSFER_DST_OPTIMAL";
    case VK_IMAGE_LAYOUT_PREINITIALIZED: return str << "PREINITIALIZED";
    case VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_STENCIL_ATTACHMENT_OPTIMAL: return str << "DEPTH_READ_ONLY_STENCIL_ATTACHMENT_OPTIMAL";
    case VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_STENCIL_READ_ONLY_OPTIMAL: return str << "DEPTH_ATTACHMENT_STENCIL_READ_ONLY_OPTIMAL";
    case VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL: return str << "DEPTH_ATTACHMENT_OPTIMAL";
    case VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL: return str << "DEPTH_READ_ONLY_OPTIMAL";
    case VK_IMAGE_LAYOUT_STENCIL_ATTACHMENT_OPTIMAL: return str << "STENCIL_ATTACHMENT_OPTIMAL";
    case VK_IMAGE_LAYOUT_STENCIL_READ_ONLY_OPTIMAL: return str << "STENCIL_READ_ONLY_OPTIMAL";
    case VK_IMAGE_LAYOUT_PRESENT_SRC_KHR: return str << "PRESENT_SRC_KHR";
    case VK_IMAGE_LAYOUT_SHARED_PRESENT_KHR: return str << "SHARED_PRESENT_KHR";
    case VK_IMAGE_LAYOUT_SHADING_RATE_OPTIMAL_NV: return str << "SHADING_RATE_OPTIMAL_NV";
    case VK_IMAGE_LAYOUT_FRAGMENT_DENSITY_MAP_OPTIMAL_EXT: return str << "FRAGMENT_DENSITY_MAP_OPTIMAL_EXT";
	default: return str << "<<unknown>>";
	}
}

std::ostream& SerializationOperator(std::ostream& str, const VkRenderPassCreateInfo2& rp)
{
	using namespace RenderCore;
	using namespace RenderCore::Metal_Vulkan;
	str << "--- [" << rp.attachmentCount << "] Attachments ------" << std::endl;
	for (unsigned c=0; c<rp.attachmentCount; ++c) {
		const auto& a = rp.pAttachments[c];
		str << "\t[" << c << "] " << AsString(AsFormat((VkFormat_)a.format)) << " L: " << a.loadOp << "/" << a.initialLayout << " S: " << a.storeOp << "/" << a.finalLayout << std::endl;
	}
	str << "--- [" << rp.dependencyCount << "] Subpass dependencies ------" << std::endl;
	for (unsigned c=0; c<rp.dependencyCount; ++c) {
		const auto& d = rp.pDependencies[c];
		str << "\t[" << c << "] " << "pass " << d.srcSubpass << "->" << d.dstSubpass << " stageMask: 0x" << std::hex << d.srcStageMask << "->0x" << d.dstStageMask << " accessMask: 0x" << d.srcAccessMask << "->0x" << d.dstAccessMask << std::dec << std::endl;
	}
	return str;
}
