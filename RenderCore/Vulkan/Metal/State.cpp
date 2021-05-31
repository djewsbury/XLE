// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "State.h"
#include "ObjectFactory.h"
#include "DeviceContext.h"
#include "../../../Utility/MemoryUtils.h"

namespace RenderCore { namespace Metal_Vulkan { namespace Internal
{
	static VkCullModeFlags AsVkCullMode(CullMode cullmode)
	{
		switch (cullmode) {
		default:
		case CullMode::None: return VK_CULL_MODE_NONE;
		case CullMode::Front: return VK_CULL_MODE_FRONT_BIT;
		case CullMode::Back: return VK_CULL_MODE_BACK_BIT;
		}

		// (VK_CULL_MODE_FRONT_AND_BACK not accessable)
	}

	static VkPolygonMode AsVkPolygonMode(FillMode cullmode)
	{
		switch (cullmode) {
		default:
		case FillMode::Solid: return VK_POLYGON_MODE_FILL;
		case FillMode::Wireframe: return VK_POLYGON_MODE_LINE;
		}

		// (VK_POLYGON_MODE_POINT not accessable)
	}

	static VkBlendOp AsVkBlendOp(BlendOp blendOp)
	{
		switch (blendOp) {
		default:
		case BlendOp::NoBlending:
		case BlendOp::Add:          return VK_BLEND_OP_ADD;
		case BlendOp::Subtract:     return VK_BLEND_OP_SUBTRACT;
		case BlendOp::RevSubtract:  return VK_BLEND_OP_REVERSE_SUBTRACT;
		case BlendOp::Min:          return VK_BLEND_OP_MIN;
		case BlendOp::Max:          return VK_BLEND_OP_MAX;
		}
	}

	static VkBlendFactor AsVkBlendFactor(Blend blendOp)
	{
		switch (blendOp)
		{
		case Blend::Zero: return VK_BLEND_FACTOR_ZERO;
		default:
		case Blend::One: return VK_BLEND_FACTOR_ONE;

		case Blend::SrcColor: return VK_BLEND_FACTOR_SRC_COLOR;
		case Blend::InvSrcColor: return VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
		case Blend::DestColor: return VK_BLEND_FACTOR_DST_COLOR;
		case Blend::InvDestColor: return VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR;

		case Blend::SrcAlpha: return VK_BLEND_FACTOR_SRC_ALPHA;
		case Blend::InvSrcAlpha: return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
		case Blend::DestAlpha: return VK_BLEND_FACTOR_DST_ALPHA;
		case Blend::InvDestAlpha: return VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
		}

		// not accessable:
		// VK_BLEND_FACTOR_CONSTANT_COLOR
		// VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_COLOR
		// VK_BLEND_FACTOR_CONSTANT_ALPHA
		// VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_ALPHA
		// VK_BLEND_FACTOR_SRC_ALPHA_SATURATE
		// VK_BLEND_FACTOR_SRC1_COLOR
		// VK_BLEND_FACTOR_ONE_MINUS_SRC1_COLOR
		// VK_BLEND_FACTOR_SRC1_ALPHA
		// VK_BLEND_FACTOR_ONE_MINUS_SRC1_ALPHA
	}

	static VkCompareOp AsVkCompareOp(CompareOp comparison)
	{
		switch (comparison)
		{
		case CompareOp::Never: return VK_COMPARE_OP_NEVER;
		case CompareOp::Less: return VK_COMPARE_OP_LESS;
		case CompareOp::Equal: return VK_COMPARE_OP_EQUAL;
		case CompareOp::LessEqual: return VK_COMPARE_OP_LESS_OR_EQUAL;
		case CompareOp::Greater: return VK_COMPARE_OP_GREATER;
		case CompareOp::NotEqual: return VK_COMPARE_OP_NOT_EQUAL;
		case CompareOp::GreaterEqual: return VK_COMPARE_OP_GREATER_OR_EQUAL;
		default:
		case CompareOp::Always: return VK_COMPARE_OP_ALWAYS;
		}
	}

	static VkStencilOp AsVkStencilOp(StencilOp stencilOp)
	{
		switch (stencilOp)
		{
		default:
		case StencilOp::DontWrite:      return VK_STENCIL_OP_KEEP;
		case StencilOp::Zero:           return VK_STENCIL_OP_ZERO;
		case StencilOp::Replace:        return VK_STENCIL_OP_REPLACE;
		case StencilOp::IncreaseSat:    return VK_STENCIL_OP_INCREMENT_AND_CLAMP;
		case StencilOp::DecreaseSat:    return VK_STENCIL_OP_DECREMENT_AND_CLAMP;
		case StencilOp::Invert:         return VK_STENCIL_OP_INVERT;
		case StencilOp::Increase:       return VK_STENCIL_OP_INCREMENT_AND_WRAP;
		case StencilOp::Decrease:       return VK_STENCIL_OP_DECREMENT_AND_WRAP;
		}
	}

	static VkSamplerAddressMode AsVkAddressMode(AddressMode addressMode)
	{
		switch (addressMode)
		{
		default:
		case AddressMode::Wrap: return VK_SAMPLER_ADDRESS_MODE_REPEAT;
		case AddressMode::Mirror: return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
		case AddressMode::Clamp: return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		case AddressMode::Border: return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
		}
	}

	static VkFrontFace AsVkFrontFace(FaceWinding faceWinding)
	{
		switch (faceWinding)
		{
		default:
		case FaceWinding::CCW: return VK_FRONT_FACE_COUNTER_CLOCKWISE;
		case FaceWinding::CW: return VK_FRONT_FACE_CLOCKWISE;
		}
	}

	VulkanRasterizerState::VulkanRasterizerState(const RasterizationDesc& desc)
	{
		sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
		pNext = nullptr;
		flags = 0;
		polygonMode = AsVkPolygonMode(FillMode::Solid);
		cullMode = AsVkCullMode(desc._cullMode);
		frontFace = AsVkFrontFace(desc._frontFaceWinding);
		depthClampEnable = VK_FALSE;
		rasterizerDiscardEnable = VK_FALSE;
		depthBiasEnable = VK_TRUE;
		depthBiasConstantFactor = desc._depthBiasConstantFactor;
		depthBiasClamp = desc._depthBiasClamp;
		depthBiasSlopeFactor = desc._depthBiasSlopeFactor;
		lineWidth = 1.0f;	// (set to 1.0f when this feature is disabled)
	}

	VulkanBlendState::VulkanBlendState( 
		IteratorRange<const AttachmentBlendDesc*> blendStates)
	{
		XlZeroMemory(_attachments);
		auto count = std::min(blendStates.size(), dimof(_attachments));
		for (unsigned c=0; c<count; ++c) {
			auto& inputState = blendStates[c];
			_attachments[c].colorWriteMask = inputState._writeMask;
			_attachments[c].blendEnable = inputState._blendEnable;

			_attachments[c].colorBlendOp = AsVkBlendOp(inputState._colorBlendOp);
			_attachments[c].srcColorBlendFactor = AsVkBlendFactor(inputState._srcColorBlendFactor);
			_attachments[c].dstColorBlendFactor = AsVkBlendFactor(inputState._dstColorBlendFactor);

			_attachments[c].alphaBlendOp = AsVkBlendOp(inputState._alphaBlendOp);
			_attachments[c].srcAlphaBlendFactor = AsVkBlendFactor(inputState._srcAlphaBlendFactor);
			_attachments[c].dstAlphaBlendFactor = AsVkBlendFactor(inputState._dstAlphaBlendFactor);
		}

		sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
		pNext = nullptr;
		flags = 0;
		attachmentCount = count;
		pAttachments = _attachments;
		logicOpEnable = VK_FALSE;
		logicOp = VK_LOGIC_OP_NO_OP;
		blendConstants[0] = 1.0f;
		blendConstants[1] = 1.0f;
		blendConstants[2] = 1.0f;
		blendConstants[3] = 1.0f;
	}

	static AttachmentBlendDesc s_defaultBlendDesc[] = { AttachmentBlendDesc{} };
	VulkanBlendState::VulkanBlendState()
	: VulkanBlendState(MakeIteratorRange(s_defaultBlendDesc))
	{}

	VulkanBlendState::VulkanBlendState(const VulkanBlendState& cloneFrom)
	{
		*(VkPipelineColorBlendStateCreateInfo*)this = *(VkPipelineColorBlendStateCreateInfo*)&cloneFrom;
		for (unsigned c=0; c<attachmentCount; ++c)
			_attachments[c] = cloneFrom._attachments[c];
		pAttachments = _attachments;
	}

	VulkanBlendState& VulkanBlendState::operator=(const VulkanBlendState& cloneFrom)
	{
		*(VkPipelineColorBlendStateCreateInfo*)this = *(VkPipelineColorBlendStateCreateInfo*)&cloneFrom;
		for (unsigned c=0; c<attachmentCount; ++c)
			_attachments[c] = cloneFrom._attachments[c];
		pAttachments = _attachments;
		return *this;
	}

	VulkanDepthStencilState::VulkanDepthStencilState(const DepthStencilDesc& desc)
	{
		sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
		pNext = nullptr;
		flags = 0;
		depthTestEnable = desc._depthTest != CompareOp::Always;
		depthWriteEnable = desc._depthWrite;
		depthCompareOp = AsVkCompareOp(desc._depthTest);
		depthBoundsTestEnable = desc._depthBoundsTestEnable;
		minDepthBounds = 0;
		maxDepthBounds = 0;
		stencilTestEnable = desc._stencilEnable;

		front.failOp = AsVkStencilOp(desc._frontFaceStencil._failOp);
		front.passOp = AsVkStencilOp(desc._frontFaceStencil._passOp);
		front.compareOp = AsVkCompareOp(desc._frontFaceStencil._comparisonOp);
		front.compareMask = desc._stencilReadMask;
		front.reference = 0;
		front.depthFailOp = AsVkStencilOp(desc._frontFaceStencil._depthFailOp);
		front.writeMask = desc._stencilWriteMask;

		back.failOp = AsVkStencilOp(desc._backFaceStencil._failOp);
		back.passOp = AsVkStencilOp(desc._backFaceStencil._passOp);
		back.compareOp = AsVkCompareOp(desc._backFaceStencil._comparisonOp);
		back.compareMask = desc._stencilReadMask;
		back.reference = 0;
		back.depthFailOp = AsVkStencilOp(desc._backFaceStencil._depthFailOp);
		back.writeMask = desc._stencilWriteMask;

		// todo:
		//   desc._stencilReference
	}

}}}


namespace RenderCore { namespace Metal_Vulkan
{
	SamplerState::SamplerState(   
		ObjectFactory& objectFactory, 
		const SamplerDesc& desc)
	: _desc(desc)
	{
		VkSamplerCreateInfo samplerCreateInfo = {};
		samplerCreateInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
		samplerCreateInfo.pNext = nullptr;
		samplerCreateInfo.flags = 0;

		assert(desc._enableMipmaps == true);

		samplerCreateInfo.compareEnable = VK_FALSE;
		samplerCreateInfo.compareOp = VK_COMPARE_OP_NEVER;
		samplerCreateInfo.anisotropyEnable = VK_FALSE;
		samplerCreateInfo.maxAnisotropy = 0;

		switch (desc._filter) {
		default:
		case FilterMode::Point:                 
			samplerCreateInfo.magFilter = VK_FILTER_NEAREST;
			samplerCreateInfo.minFilter = VK_FILTER_NEAREST;
			samplerCreateInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
			break;
		case FilterMode::Anisotropic:
			samplerCreateInfo.magFilter = VK_FILTER_LINEAR;
			samplerCreateInfo.minFilter = VK_FILTER_LINEAR;
			samplerCreateInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
			samplerCreateInfo.anisotropyEnable = VK_TRUE;
			samplerCreateInfo.maxAnisotropy = 16;
			break;
		case FilterMode::ComparisonBilinear:    
			samplerCreateInfo.magFilter = VK_FILTER_LINEAR;
			samplerCreateInfo.minFilter = VK_FILTER_LINEAR;
			samplerCreateInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
			samplerCreateInfo.compareEnable = VK_TRUE;
			samplerCreateInfo.compareOp = Internal::AsVkCompareOp(desc._comparison);
			break;
		case FilterMode::Bilinear:              
			samplerCreateInfo.magFilter = VK_FILTER_LINEAR;
			samplerCreateInfo.minFilter = VK_FILTER_LINEAR;
			samplerCreateInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
			break;
		case FilterMode::Trilinear:             
			samplerCreateInfo.magFilter = VK_FILTER_LINEAR;
			samplerCreateInfo.minFilter = VK_FILTER_LINEAR;
			samplerCreateInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
			break;
		}
		
		samplerCreateInfo.addressModeU = Internal::AsVkAddressMode(desc._addressU);
		samplerCreateInfo.addressModeV = Internal::AsVkAddressMode(desc._addressU);
		samplerCreateInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;

		samplerCreateInfo.mipLodBias = 0.f;
		samplerCreateInfo.minLod = 0.f;
		samplerCreateInfo.maxLod = VK_LOD_CLAMP_NONE;
		samplerCreateInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
		samplerCreateInfo.unnormalizedCoordinates = VK_FALSE;       // (interesting)
		_sampler = objectFactory.CreateSampler(samplerCreateInfo);
	}

	SamplerState::~SamplerState() {}

	SamplerDesc SamplerState::GetDesc() const { return _desc; }

}}
