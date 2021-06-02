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
#include <cmath>

namespace RenderCore { namespace Metal_Vulkan
{
    static VkAttachmentLoadOp AsLoadOp(LoadStore loadStore)
    {
        switch (loadStore)
        {
        default:
        case LoadStore::DontCare: 
        case LoadStore::DontCare_RetainStencil: 
        case LoadStore::DontCare_ClearStencil: 
            return VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        case LoadStore::Retain: 
        case LoadStore::Retain_RetainStencil: 
        case LoadStore::Retain_ClearStencil: 
            return VK_ATTACHMENT_LOAD_OP_LOAD;
        case LoadStore::Clear: 
        case LoadStore::Clear_RetainStencil: 
        case LoadStore::Clear_ClearStencil: 
            return VK_ATTACHMENT_LOAD_OP_CLEAR;
        }
    }

    static VkAttachmentStoreOp AsStoreOp(LoadStore loadStore)
    {
        switch (loadStore)
        {
        default:
        case LoadStore::Clear: 
        case LoadStore::Clear_RetainStencil: 
        case LoadStore::Clear_ClearStencil: 
        case LoadStore::DontCare: 
        case LoadStore::DontCare_RetainStencil: 
        case LoadStore::DontCare_ClearStencil: 
            return VK_ATTACHMENT_STORE_OP_DONT_CARE;
        case LoadStore::Retain: 
        case LoadStore::Retain_RetainStencil: 
        case LoadStore::Retain_ClearStencil: 
            return VK_ATTACHMENT_STORE_OP_STORE;
        }
    }

    static VkAttachmentLoadOp AsLoadOpStencil(LoadStore loadStore)
    {
        switch (loadStore)
        {
        default:
        case LoadStore::Clear: 
        case LoadStore::DontCare: 
        case LoadStore::Retain: 
            return VK_ATTACHMENT_LOAD_OP_DONT_CARE;

        case LoadStore::Clear_ClearStencil: 
        case LoadStore::DontCare_ClearStencil: 
        case LoadStore::Retain_ClearStencil: 
            return VK_ATTACHMENT_LOAD_OP_CLEAR;

        case LoadStore::Clear_RetainStencil: 
        case LoadStore::DontCare_RetainStencil: 
        case LoadStore::Retain_RetainStencil: 
            return VK_ATTACHMENT_LOAD_OP_LOAD;
        }
    }

    static VkAttachmentStoreOp AsStoreOpStencil(LoadStore loadStore)
    {
        switch (loadStore)
        {
        default:
        case LoadStore::Clear: 
        case LoadStore::DontCare: 
        case LoadStore::Retain: 
        case LoadStore::Clear_ClearStencil: 
        case LoadStore::DontCare_ClearStencil: 
        case LoadStore::Retain_ClearStencil: 
            return VK_ATTACHMENT_STORE_OP_DONT_CARE;

        case LoadStore::Clear_RetainStencil: 
        case LoadStore::DontCare_RetainStencil: 
        case LoadStore::Retain_RetainStencil: 
            return VK_ATTACHMENT_STORE_OP_STORE;
        }
    }

	namespace Internal
	{
		namespace AttachmentUsageType
		{
			enum Flags
			{
				Input = 1<<0, Output = 1<<1, DepthStencil = 1<<2
			};
			using BitField = unsigned;
		}
	}

	static VkImageLayout AsShaderReadLayout(unsigned attachmentUsage)
	{
		if (attachmentUsage & unsigned(Internal::AttachmentUsageType::DepthStencil))
			return VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
		return VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	}

	static bool HasRetain(LoadStore ls)
	{
		return ls == LoadStore::Retain
			|| ls == LoadStore::DontCare_RetainStencil
			|| ls == LoadStore::Retain_RetainStencil
			|| ls == LoadStore::Clear_RetainStencil
			|| ls == LoadStore::Retain_ClearStencil;
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

	VulkanUniquePtr<VkRenderPass> CreateVulkanRenderPass(
        const Metal_Vulkan::ObjectFactory& factory,
        const FrameBufferDesc& layout,
        TextureSamples samples)
	{
		const auto subpasses = layout.GetSubpasses();

		struct AttachmentUsage
		{
			unsigned _subpassIdx = ~0u;
			Internal::AttachmentUsageType::BitField _usage = 0;
		};
		struct WorkingAttachment
		{
			AttachmentDesc _desc;
			AttachmentUsage _lastSubpassWrite;
			AttachmentUsage _lastSubpassRead;
			TextureViewDesc::FormatFilter _formatFilter;
			Internal::AttachmentUsageType::BitField _attachmentUsage = 0;
		};
		std::vector<std::pair<AttachmentName, WorkingAttachment>> workingAttachments;
		workingAttachments.reserve(subpasses.size()*2);	// approximate

		struct SubpassDependency
		{
			AttachmentName _resource;
			AttachmentUsage _first;
			AttachmentUsage _second;
		};
		std::vector<SubpassDependency> dependencies;
		dependencies.reserve(subpasses.size()*2);	// approximate

		auto attachmentCount = layout.GetAttachments().size();

		////////////////////////////////////////////////////////////////////////////////////
		// Build up the list of subpass dependencies and the set of unique attachments
        for (unsigned spIdx=0; spIdx<subpasses.size(); spIdx++) {
			const auto& spDesc = subpasses[spIdx];

			Internal::AttachmentUsageType::BitField subpassAttachmentUsages[attachmentCount];
			for (unsigned c=0; c<attachmentCount; ++c) subpassAttachmentUsages[c] = 0;

			for (const auto& r:spDesc.GetOutputs()) 
				subpassAttachmentUsages[r._resourceName] |= Internal::AttachmentUsageType::Output;
			if (spDesc.GetDepthStencil()._resourceName != SubpassDesc::Unused._resourceName)
				subpassAttachmentUsages[spDesc.GetDepthStencil()._resourceName] |= Internal::AttachmentUsageType::DepthStencil;
			for (const auto& r:spDesc.GetInputs()) 
				subpassAttachmentUsages[r._resourceName] |= Internal::AttachmentUsageType::Input;

			//////////////////////////////////////////////////////////////////////////////////////////

			for (unsigned attachmentName=0; attachmentName<attachmentCount; ++attachmentName) {
				auto usage = subpassAttachmentUsages[attachmentName];
				if (!usage) continue;

				auto i = LowerBound(workingAttachments, attachmentName);
				if (i == workingAttachments.end() || i->first != attachmentName) {
					i = workingAttachments.insert(i, {attachmentName, WorkingAttachment{}});
					assert(attachmentName < layout.GetAttachments().size());
					i->second._desc = layout.GetAttachments()[attachmentName];
				}

				AttachmentUsage loadUsage { spIdx, usage };
				AttachmentUsage storeUsage { spIdx, usage };

				// If we're loading data from a previous phase, we've got to find it in
				// the working attachments, and create a subpass dependency
				// Otherwise, if there are any previous contents, they 
				// will be destroyed.
				// We do this even if there's not an explicit retain on the load step
				//	-- we assume "retain" between subpasses, even if the views contradict that
				//	(as per Vulkan, where LoadStore is only for the input/output of the entire render pass)
				dependencies.push_back({attachmentName, i->second._lastSubpassWrite, loadUsage});

				// We also need a dependency with the last subpass to read from this 
				// attachment. We can't write to it until the reading is finished
				if (usage & (Internal::AttachmentUsageType::Output | Internal::AttachmentUsageType::DepthStencil)) {
					if (i->second._lastSubpassRead._subpassIdx != ~0u)
						dependencies.push_back({attachmentName, i->second._lastSubpassRead, loadUsage});
					i->second._lastSubpassWrite = storeUsage;
				} else {
					i->second._lastSubpassRead = loadUsage;
				}

				i->second._attachmentUsage |= usage;
			}
		}

		////////////////////////////////////////////////////////////////////////////////////
		// Build the VkAttachmentDescription objects
		std::vector<VkAttachmentDescription> attachmentDesc;
        attachmentDesc.reserve(workingAttachments.size());
        for (auto&a:workingAttachments) {
            const auto& resourceDesc = a.second._desc;

			// We need to look through all of the places we use this attachment to finalize the
			// format filter
            auto formatFilter = a.second._formatFilter;
			for (const auto& spDesc:subpasses) {
				for (const auto& r:spDesc.GetOutputs())
					if (r._resourceName == a.first)
						MergeFormatFilter(formatFilter, r._window._format);
				if (spDesc.GetDepthStencil()._resourceName == a.first)
					MergeFormatFilter(formatFilter, spDesc.GetDepthStencil()._window._format);
				for (const auto& r:spDesc.GetInputs())
					if (r._resourceName == a.first)
						MergeFormatFilter(formatFilter, r._window._format);
			}

            BindFlag::Enum formatUsage = BindFlag::ShaderResource;
            if (a.second._attachmentUsage & Internal::AttachmentUsageType::Output) formatUsage = BindFlag::RenderTarget;
            if (a.second._attachmentUsage & Internal::AttachmentUsageType::DepthStencil) formatUsage = BindFlag::DepthStencil;
            auto resolvedFormat = ResolveFormat(resourceDesc._format, formatFilter, formatUsage);

			LoadStore originalLoad = resourceDesc._loadFromPreviousPhase;
			LoadStore finalStore = resourceDesc._storeToNextPhase;

            VkAttachmentDescription desc;
            desc.flags = 0;
            desc.format = (VkFormat)AsVkFormat(resolvedFormat);
            desc.samples = VK_SAMPLE_COUNT_1_BIT;
            desc.loadOp = AsLoadOp(originalLoad);
            desc.stencilLoadOp = AsLoadOpStencil(originalLoad);
			desc.storeOp = AsStoreOp(finalStore);
            desc.stencilStoreOp = AsStoreOpStencil(finalStore);
			assert(desc.format != VK_FORMAT_UNDEFINED);

			// If the attachment explicitly requests a specific final layout, let's use that
			// This needs to mirror the logic for "steady state" layouts for resources
			//    -- that is, where a resource is used in multiple ways, we will tend to
			//		default to just the "general" layout
			// Otherwise we default to keeping the layout that corresponds to how we where
			// using it in the render pass
			if (resourceDesc._finalLayout == BindFlag::ShaderResource) {
				desc.initialLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
				desc.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			} else if (resourceDesc._finalLayout == BindFlag::TransferSrc) {
				desc.initialLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
				desc.finalLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
			} else if (resourceDesc._finalLayout == BindFlag::TransferDst) {
				desc.initialLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
				desc.finalLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
			} else if (resourceDesc._finalLayout == BindFlag::PresentationSrc) {
				desc.initialLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
				desc.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
			} else if (resourceDesc._finalLayout != 0) {
				desc.initialLayout = VK_IMAGE_LAYOUT_GENERAL;
				desc.finalLayout = VK_IMAGE_LAYOUT_GENERAL;
			} else {
				bool isDepthStencil = !!(a.second._attachmentUsage & unsigned(Internal::AttachmentUsageType::DepthStencil));
				bool isColorOutput = !!(a.second._attachmentUsage & unsigned(Internal::AttachmentUsageType::Output));
				bool isAttachmentInput = !!(a.second._attachmentUsage & unsigned(Internal::AttachmentUsageType::Input));
				if (isDepthStencil) {
					assert(!isColorOutput);
					desc.initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
					desc.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
				} else if (isColorOutput) {
					desc.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
					desc.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
				} else if (isAttachmentInput) {
					desc.initialLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
					desc.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
				} else {
					assert(0);
					desc.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
					desc.finalLayout = VK_IMAGE_LAYOUT_UNDEFINED;
				}
			}

			// Just setting the initial layout across the board to "VK_IMAGE_LAYOUT_UNDEFINED" might be
			// handy here; just not sure what the consequences would be
			// desc.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            
            if (resourceDesc._flags & AttachmentDesc::Flags::Multisampled)
                desc.samples = (VkSampleCountFlagBits)AsSampleCountFlagBits(samples);

            // note --  do we need to set VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL or 
            //          VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL as appropriate for input attachments
            //          (we should be able to tell this from the subpasses)?

            attachmentDesc.push_back(desc);
        }


		////////////////////////////////////////////////////////////////////////////////////
		// Build the actual VkSubpassDescription objects

		std::vector<VkAttachmentReference> attachReferences;
        std::vector<uint32_t> preserveAttachments;

        std::vector<VkSubpassDescription> subpassDesc;
        subpassDesc.reserve(subpasses.size());
        for (auto&p:subpasses) {
            VkSubpassDescription desc;
            desc.flags = 0;
            desc.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;

            // Input attachments are going to be difficult, because they must be bound both
            // by the sub passes and by the descriptor set! (and they must be explicitly listed as
            // input attachments in the shader). Holy cow, the render pass, frame buffer, pipeline
            // layout, descriptor set and shader must all agree!
            auto beforeInputs = attachReferences.size();
            for (auto& a:p.GetInputs()) {
				auto resource = a._resourceName;
				auto i = LowerBound(workingAttachments, resource);
				assert(i != workingAttachments.end() && i->first == resource);
				auto internalName = std::distance(workingAttachments.begin(), i);
				attachReferences.push_back(VkAttachmentReference{(uint32_t)internalName, AsShaderReadLayout(i->second._attachmentUsage)});
            }
            desc.pInputAttachments = (const VkAttachmentReference*)(beforeInputs+1);
            desc.inputAttachmentCount = uint32_t(attachReferences.size() - beforeInputs);

            auto beforeOutputs = attachReferences.size();
            for (auto& a:p.GetOutputs()) {
				auto resource = a._resourceName;
				auto i = LowerBound(workingAttachments, resource);
				assert(i != workingAttachments.end() && i->first == resource);
				auto internalName = std::distance(workingAttachments.begin(), i);
				attachReferences.push_back(VkAttachmentReference{(uint32_t)internalName, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL}); // AsShaderReadLayout(i->second._attachmentUsage)});
            }
            desc.pColorAttachments = (const VkAttachmentReference*)(beforeOutputs+1);
            desc.colorAttachmentCount = uint32_t(attachReferences.size() - beforeOutputs);
            desc.pResolveAttachments = nullptr; // not supported
			desc.pPreserveAttachments = nullptr;
			desc.preserveAttachmentCount = 0;

            if (p.GetDepthStencil()._resourceName != SubpassDesc::Unused._resourceName) {
				auto resource = p.GetDepthStencil()._resourceName;
				auto i = LowerBound(workingAttachments, resource);
				assert(i != workingAttachments.end() && i->first == resource);
				auto internalName = std::distance(workingAttachments.begin(), i);
				desc.pDepthStencilAttachment = (const VkAttachmentReference*)(attachReferences.size()+1);
				attachReferences.push_back(VkAttachmentReference{(uint32_t)internalName, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL}); // AsShaderReadLayout(i->second._attachmentUsage)});
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
            if (p.pPreserveAttachments)
                p.pPreserveAttachments = AsPointer(preserveAttachments.begin()) + size_t(p.pPreserveAttachments)-1;
        }

		////////////////////////////////////////////////////////////////////////////////////
		// Build the actual VkSubpassDependency objects

        std::vector<VkSubpassDependency> vkDeps;
		for (unsigned c=0;c<unsigned(subpasses.size()); ++c) {
			// Find the list of SubPassDependency objects where _second is this subpass. We'll
			// then find the unique list of subpasses referenced by _first, and generate the
			// Vulkan object from them.
			//
			// Note that there are implicit dependencies to "VK_SUBPASS_EXTERNAL" which are defined
			// with a standard form. We'll rely on those implicit dependencies, rather than 
			// explicitly creating them here.

			std::vector<SubpassDependency> terminatingDependencies;
			for (const auto& d:dependencies)
				if (d._second._subpassIdx == c && d._first._subpassIdx != ~0u)
					terminatingDependencies.push_back(d);

			std::vector<VkSubpassDependency> deps;
			for (const auto& d:terminatingDependencies) {
				auto i = std::find_if(
					deps.begin(), deps.end(),
					[&d](const VkSubpassDependency& vkd) { return vkd.srcSubpass == d._first._subpassIdx; });
				if (i == deps.end())
					i = deps.insert(deps.end(), {
						d._first._subpassIdx, c, 
						0, 0, 0, 0,	// mask and access flags set below
						0});

				// note -- making assumptions about attachments usage here -- (in particular, ignoring shader resources bound to shaders other than the fragment shader)
				if (d._first._usage | Internal::AttachmentUsageType::Output) {
					i->srcAccessMask |= VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
					i->srcStageMask |= VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
				}
				if (d._first._usage | Internal::AttachmentUsageType::DepthStencil) {
					i->srcAccessMask |= VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
					i->srcStageMask |= VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
				}
				if (d._first._usage | Internal::AttachmentUsageType::Input) {
					i->srcAccessMask |= VK_ACCESS_INPUT_ATTACHMENT_READ_BIT;
					i->srcStageMask |= VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
				}

				if (d._second._usage | Internal::AttachmentUsageType::Output) {
					i->dstAccessMask |= VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
					i->dstStageMask |= VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
				}
				if (d._second._usage | Internal::AttachmentUsageType::DepthStencil) {
					i->dstAccessMask |= VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
					i->dstStageMask |= VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
				}
				if (d._second._usage | Internal::AttachmentUsageType::Input) {
					i->dstAccessMask |= VK_ACCESS_INPUT_ATTACHMENT_READ_BIT;
					i->dstStageMask |= VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
				}
			}

			vkDeps.insert(vkDeps.end(), deps.begin(), deps.end());
        }

		// Also create subpass dependencies for every subpass. This is required currently, because we can sometimes
		// use vkCmdPipelineBarrier with a global memory barrier to push through dynamic constants data. However, this
		// might defeat some of the key goals of the render pass system!
		/*
			Note -- this currently crashes the VK_LAYER_LUNARG_draw_state validation code... But it seems like we need it!
		for (unsigned c = 0; c<unsigned(subpasses.size()); ++c) {
			vkDeps.push_back(VkSubpassDependency{
				c, c, 
				VK_PIPELINE_STAGE_HOST_BIT,
				VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
				VK_ACCESS_HOST_WRITE_BIT,
				VK_ACCESS_INDIRECT_COMMAND_READ_BIT
				| VK_ACCESS_INDEX_READ_BIT
				| VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT
				| VK_ACCESS_UNIFORM_READ_BIT
				| VK_ACCESS_INPUT_ATTACHMENT_READ_BIT
				| VK_ACCESS_SHADER_READ_BIT,
				0});
		}
		*/

		////////////////////////////////////////////////////////////////////////////////////
		// Build the final render pass object

        VkRenderPassCreateInfo rp_info = {};
        rp_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        rp_info.pNext = nullptr;
        rp_info.attachmentCount = (uint32_t)attachmentDesc.size();
        rp_info.pAttachments = attachmentDesc.data();
        rp_info.subpassCount = (uint32_t)subpassDesc.size();
        rp_info.pSubpasses = subpassDesc.data();
        rp_info.dependencyCount = (uint32_t)vkDeps.size();
        rp_info.pDependencies = vkDeps.data();

        return factory.CreateRenderPass(rp_info);
	}

	struct MaxDims
	{
		unsigned _width = 0, _height = 0;
		unsigned _layers = 0;
	};

	static void BuildMaxDims(MaxDims& result, const ResourceDesc& desc)
	{
		assert(desc._type == ResourceDesc::Type::Texture);
		result._width = std::max(result._width, desc._textureDesc._width);
		result._height = std::max(result._height, desc._textureDesc._height);
		if (desc._textureDesc._dimensionality == TextureDesc::Dimensionality::CubeMap) {
			assert(desc._textureDesc._arrayCount == 6u);
			result._layers = std::max(result._layers, 6u);
		} else
			result._layers = std::max(result._layers, (unsigned)desc._textureDesc._arrayCount);
	}

	static BindFlag::Enum AsBindFlag(Internal::AttachmentUsageType::BitField usageType)
	{
		if (usageType & Internal::AttachmentUsageType::Output)
			return BindFlag::RenderTarget;

		if (usageType & Internal::AttachmentUsageType::DepthStencil)
			return BindFlag::DepthStencil;

		return BindFlag::InputAttachment;
	}

    FrameBuffer::FrameBuffer(
        const ObjectFactory& factory,
        const FrameBufferDesc& fbDesc,
        const INamedAttachments& namedResources)
    {
		_layout = Internal::VulkanGlobalsTemp::GetInstance()._globalPools->_renderPassPool.CreateVulkanRenderPass(fbDesc, fbDesc.GetProperties()._samples);

        // We must create the frame buffer, including all views required.
        // We need to order the list of views in VkFramebufferCreateInfo in the
		// same order as the attachments were defined in the VkRenderPass object.
		auto subpasses = fbDesc.GetSubpasses();

		std::vector<std::pair<AttachmentName, Internal::AttachmentUsageType::BitField>> attachments;
		attachments.reserve(subpasses.size()*4);	// estimate
		auto fbAttachments = fbDesc.GetAttachments();
		for (unsigned c=0; c<(unsigned)subpasses.size(); ++c) {
			const auto& spDesc = subpasses[c];

			for (const auto& r:spDesc.GetOutputs()) {
				attachments.push_back({r._resourceName, Internal::AttachmentUsageType::Output});
			}

			if (spDesc.GetDepthStencil()._resourceName != SubpassDesc::Unused._resourceName) {
				attachments.push_back({spDesc.GetDepthStencil()._resourceName, Internal::AttachmentUsageType::DepthStencil});
			}

			for (const auto& r:spDesc.GetInputs()) {
				// todo -- these srvs also need to be exposed to the caller, so they can be bound to
				// the shader during the subpass
				attachments.push_back({r._resourceName, Internal::AttachmentUsageType::Input});
			}
        }

		// Sort by AttachmentName, and combine multiple references to the same resource into a single view
		std::sort(attachments.begin(), attachments.end(), CompareFirst<AttachmentName, Internal::AttachmentUsageType::BitField>());
		std::vector<std::pair<AttachmentName, Internal::AttachmentUsageType::BitField>> uniqueAttachments;
		uniqueAttachments.reserve(attachments.size());

		for (auto i=attachments.begin(); i!=attachments.end();) {
			auto i2 = i;
			Internal::AttachmentUsageType::BitField mergedUsage = 0;
			while (i2!=attachments.end() && i2->first == i->first) { 
				mergedUsage |= i2->second;
				++i2;
			}
			uniqueAttachments.push_back({i->first, mergedUsage});
			i = i2;
		}

		ViewPool viewPool;
        VkImageView rawViews[16];
		unsigned rawViewCount = 0;
		_clearValuesOrdering.reserve(uniqueAttachments.size());
        MaxDims maxDims;

        for (const auto&a:uniqueAttachments) {
			// Note that we can't support TextureViewDesc properly here, because we don't support 
			// the same resource being used with more than one view
			auto resource = namedResources.GetResource(a.first, fbAttachments[a.first], fbDesc.GetProperties());
			auto rtv = viewPool.GetTextureView(resource, AsBindFlag(a.second), TextureViewDesc{});
			rawViews[rawViewCount++] = checked_cast<ResourceView*>(rtv.get())->GetImageView();

			ClearValue defaultClearValue = MakeClearValue(0.f, 0.f, 0.f, 1.f);
			if (a.second & Internal::AttachmentUsageType::DepthStencil)
				defaultClearValue = MakeClearValue(1.0f, 0);
			_clearValuesOrdering.push_back({a.first, defaultClearValue});

			_retainedViews.push_back(std::move(rtv));
			BuildMaxDims(maxDims, resource->GetDesc());
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

