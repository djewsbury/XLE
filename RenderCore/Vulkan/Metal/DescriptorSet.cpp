// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "DescriptorSet.h"
#include "TextureView.h"
#include "Pools.h"
#include "PipelineLayout.h"
#include "DeviceContext.h"
#include "ShaderReflection.h"
#include "ExtensionFunctions.h"
#include "../../ShaderService.h"
#include "../../UniformsStream.h"
#include "../../BufferView.h"
#include "../../../OSServices/Log.h"
#include "../../../Utility/MemoryUtils.h"
#include "../../../Utility/ArithmeticUtils.h"
#include "../../../Utility/StreamUtils.h"
#include "../../../Utility/BitUtils.h"
#include "../../../Utility/StringFormat.h"
#include "../../../Core/Prefix.h"
#include <sstream>

namespace RenderCore { namespace Metal_Vulkan
{
	static const std::string s_dummyDescriptorString{"<DummyDescriptor>"};

	VkDescriptorType_ AsVkDescriptorType(DescriptorType type);

	template<typename BindingInfo> static BindingInfo const*& InfoPtr(VkWriteDescriptorSet& writeDesc);
	template<> VkDescriptorImageInfo const*& InfoPtr(VkWriteDescriptorSet& writeDesc) 
	{ 
		assert(writeDesc.descriptorType == VK_DESCRIPTOR_TYPE_SAMPLER
			|| writeDesc.descriptorType == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER
			|| writeDesc.descriptorType == VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE
			|| writeDesc.descriptorType == VK_DESCRIPTOR_TYPE_STORAGE_IMAGE
			|| writeDesc.descriptorType == VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT);
		return writeDesc.pImageInfo; 
	}
	template<> VkDescriptorBufferInfo const*& InfoPtr(VkWriteDescriptorSet& writeDesc) 
	{
		assert(writeDesc.descriptorType == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER
			|| writeDesc.descriptorType == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER
			|| writeDesc.descriptorType == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC
			|| writeDesc.descriptorType == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC);
		return writeDesc.pBufferInfo; 
	}
	template<> VkBufferView const*& InfoPtr(VkWriteDescriptorSet& writeDesc) 
	{ 
		assert(writeDesc.descriptorType == VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER
			|| writeDesc.descriptorType == VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER);
		return writeDesc.pTexelBufferView; 
	}

	template<> 
		VkDescriptorImageInfo& ProgressiveDescriptorSetBuilder::AllocateInfo(const VkDescriptorImageInfo& init)
	{
		assert(_pendingImageInfos < s_pendingBufferLength);
		auto& i = _imageInfo[_pendingImageInfos++];
		i = init;
		return i;
	}

	template<> 
		VkDescriptorBufferInfo& ProgressiveDescriptorSetBuilder::AllocateInfo(const VkDescriptorBufferInfo& init)
	{
		assert(_pendingBufferInfos < s_pendingBufferLength);
		auto& i = _bufferInfo[_pendingBufferInfos++];
		i = init;
		return i;
	}

	template<> 
		VkBufferView& ProgressiveDescriptorSetBuilder::AllocateInfo(const VkBufferView& init)
	{
		assert(_pendingBufferViews < s_pendingBufferLength);
		auto& i = _bufferViews[_pendingBufferViews++];
		i = init;
		return i;
	}

	template<> 
		VkDescriptorImageInfo* ProgressiveDescriptorSetBuilder::AllocateInfos(unsigned count)
	{
		assert((_pendingImageInfos+count) <= s_pendingBufferLength);
		auto* i = &_imageInfo[_pendingImageInfos];
		_pendingImageInfos += count;
		return i;
	}

	template<> 
		VkDescriptorBufferInfo* ProgressiveDescriptorSetBuilder::AllocateInfos(unsigned count)
	{
		assert((_pendingBufferInfos+count) <= s_pendingBufferLength);
		auto* i = &_bufferInfo[_pendingBufferInfos];
		_pendingBufferInfos += count;
		return i;
	}

	template<> 
		VkBufferView* ProgressiveDescriptorSetBuilder::AllocateInfos(unsigned count)
	{
		assert((_pendingBufferViews+count) <= s_pendingBufferLength);
		auto* i = &_bufferViews[_pendingBufferViews];
		_pendingBufferViews += count;
		return i;
	}

	template<typename BindingInfo>
		void    ProgressiveDescriptorSetBuilder::WriteBinding(
			unsigned bindingPoint, VkDescriptorType_ type, const BindingInfo& bindingInfo, bool reallocateBufferInfo
			VULKAN_VERBOSE_DEBUG_ONLY(, const std::string& description))
	{
			// (we're limited by the number of bits in _sinceLastFlush)
		assert(bindingPoint < 64u);

		if (_sinceLastFlush & (1ull<<bindingPoint)) {
			// we already have a pending write to this slot. Let's find it, and just
			// update the details with the new view.
			bool foundExisting = false; (void)foundExisting;
			for (unsigned p=0; p<_pendingWrites; ++p) {
				auto& w = _writes[p];
				if (w.descriptorType == type && w.dstBinding == bindingPoint) {
					InfoPtr<BindingInfo>(w) = (reallocateBufferInfo) ? &AllocateInfo(bindingInfo) : &bindingInfo;
					foundExisting = true;
					break;
				}
			}
			assert(foundExisting);
		} else {
			_sinceLastFlush |= 1ull<<bindingPoint;

			assert(_pendingWrites < s_pendingBufferLength);
			auto& w = _writes[_pendingWrites++];
			w.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			w.pNext = nullptr;
			w.dstSet = nullptr;
			w.dstBinding = bindingPoint;
			w.dstArrayElement = 0;
			w.descriptorCount = 1;
			w.descriptorType = (VkDescriptorType)type;

			InfoPtr<BindingInfo>(w) = (reallocateBufferInfo) ? &AllocateInfo(bindingInfo) : &bindingInfo;
		}

		#if defined(VULKAN_VERBOSE_DEBUG)
			if (_verboseDescription._bindingDescriptions.size() <= bindingPoint)
				_verboseDescription._bindingDescriptions.resize(bindingPoint+1);
			_verboseDescription._bindingDescriptions[bindingPoint] = { type, description };
		#endif
	}

	template<typename BindingInfo>
		void    ProgressiveDescriptorSetBuilder::WriteArrayBinding(
			unsigned bindingPoint, VkDescriptorType_ type, unsigned dstArrayElement, IteratorRange<const BindingInfo*> bindingInfo
			VULKAN_VERBOSE_DEBUG_ONLY(, const std::string& description))
	{
			// (we're limited by the number of bits in _sinceLastFlush)
		assert(bindingPoint < 64u);
		assert(!bindingInfo.empty());

		if (_sinceLastFlush & (1ull<<bindingPoint)) {
			// we already have a pending write to this slot. Let's find it, and just
			// update the details with the new view.
			bool foundExisting = false; (void)foundExisting;
			for (unsigned p=0; p<_pendingWrites; ++p) {
				auto& w = _writes[p];
				if (w.descriptorType == type && w.dstBinding == bindingPoint) {
					w.dstArrayElement = dstArrayElement;
					w.descriptorCount = (uint32_t)bindingInfo.size();
					InfoPtr<BindingInfo>(w) = bindingInfo.begin();
					foundExisting = true;
					break;
				}
			}
			assert(foundExisting);
		} else {
			_sinceLastFlush |= 1ull<<bindingPoint;

			assert(_pendingWrites < s_pendingBufferLength);
			auto& w = _writes[_pendingWrites++];
			w.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			w.pNext = nullptr;
			w.dstSet = nullptr;
			w.dstBinding = bindingPoint;
			w.dstArrayElement = dstArrayElement;
			w.descriptorCount = (uint32_t)bindingInfo.size();
			w.descriptorType = (VkDescriptorType)type;

			InfoPtr<BindingInfo>(w) = bindingInfo.begin();
		}

		#if defined(VULKAN_VERBOSE_DEBUG)
			if (_verboseDescription._bindingDescriptions.size() <= bindingPoint)
				_verboseDescription._bindingDescriptions.resize(bindingPoint+1);
			_verboseDescription._bindingDescriptions[bindingPoint] = { type, description };
		#endif
	}

	static VkDescriptorImageInfo AsVkDescriptorImageInfo(const ResourceView& resourceView)
	{
		return VkDescriptorImageInfo {
			nullptr,
			resourceView.GetImageView(),
			resourceView.GetImageLayout()
		};
	}

	static uint64_t GetGuidForVisibility(const ResourceView& resourceView)
	{
		#if defined(VULKAN_VALIDATE_RESOURCE_VISIBILITY)
			auto& res = *resourceView.GetVulkanResource();
			return res.GetImage() ? res.GetGUID() : 0;
		#else
			return 0;
		#endif
	}

	void    ProgressiveDescriptorSetBuilder::Bind(unsigned descriptorSetBindPoint, const ResourceView& resourceView, StringSection<> shaderOrDescSetVariable)
	{
		#if defined(VULKAN_VERBOSE_DEBUG)
			std::string description;
			if (resourceView.GetVulkanResource()) {
				description = resourceView.GetVulkanResource()->GetDesc()._name;
			} else {
				description = std::string{"ResourceView"};
			}
		#endif

		assert(descriptorSetBindPoint < _signature.size());
		auto slotType = _signature[descriptorSetBindPoint]._type;
		assert(_signature[descriptorSetBindPoint]._count == 1);
		auto vkSlotType = AsVkDescriptorType(slotType);
		#if defined(_DEBUG)
			const auto& physDevLimits = GetObjectFactory().GetPhysicalDeviceProperties().limits;
		#endif

		assert(resourceView.GetVulkanResource());
		switch (resourceView.GetType()) {
		case ResourceView::Type::ImageView:
			assert(resourceView.GetImageView());

			#if defined(_DEBUG)
				if (	vkSlotType != VK_DESCRIPTOR_TYPE_SAMPLER
					&&  vkSlotType != VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER
					&&  vkSlotType != VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE
					&&  vkSlotType != VK_DESCRIPTOR_TYPE_STORAGE_IMAGE
					&&  vkSlotType != VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT)
					Throw(std::runtime_error(StringMeld<256>() << "Binding mismatch for shader variable (" << shaderOrDescSetVariable << ") when binding resource (" << (resourceView.GetVulkanResource() ? resourceView.GetVulkanResource()->GetName() : StringSection<>{}) << ")"));
			#endif

			WriteBinding(
				descriptorSetBindPoint,
				vkSlotType,
				AsVkDescriptorImageInfo(resourceView), true
				VULKAN_VERBOSE_DEBUG_ONLY(, description));
			break;

		case ResourceView::Type::BufferAndRange:
			{
				#if defined(_DEBUG)
					if (	vkSlotType != VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER
						&&  vkSlotType != VK_DESCRIPTOR_TYPE_STORAGE_BUFFER
						&&  vkSlotType != VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC
						&&  vkSlotType != VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC)
						Throw(std::runtime_error(StringMeld<256>() << "Binding mismatch for shader variable (" << shaderOrDescSetVariable << ") when binding buffer (" << (resourceView.GetVulkanResource() ? resourceView.GetVulkanResource()->GetName() : StringSection<>{}) << ")"));
				#endif

				assert(resourceView.GetVulkanResource() && resourceView.GetVulkanResource()->GetBuffer());
				auto range = resourceView.GetBufferRangeOffsetAndSize();
				uint64_t rangeBegin = range.first, rangeSize = range.second;
				if (rangeBegin == 0 && rangeSize == 0)
					rangeSize = VK_WHOLE_SIZE;
				assert(rangeSize != 0);
				#if defined(_DEBUG)
					if (vkSlotType == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER || vkSlotType == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC)
						assert((rangeBegin % physDevLimits.minUniformBufferOffsetAlignment) == 0);
					else
						assert((rangeBegin % physDevLimits.minStorageBufferOffsetAlignment) == 0);
				#endif
				WriteBinding(
					descriptorSetBindPoint,
					vkSlotType,
					VkDescriptorBufferInfo { resourceView.GetVulkanResource()->GetBuffer(), rangeBegin, rangeSize }, true
					VULKAN_VERBOSE_DEBUG_ONLY(, description));
			}
			break;

		case ResourceView::Type::BufferView:
			#if defined(_DEBUG)
				if (	vkSlotType != VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER
					&&  vkSlotType != VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER)
					Throw(std::runtime_error(StringMeld<256>() << "Binding mismatch for shader variable (" << shaderOrDescSetVariable << ") when binding buffer (" << (resourceView.GetVulkanResource() ? resourceView.GetVulkanResource()->GetName() : StringSection<>{}) << ")"));
			#endif

			assert(resourceView.GetBufferView());
			WriteBinding(
				descriptorSetBindPoint,
				vkSlotType,
				resourceView.GetBufferView(), true
				VULKAN_VERBOSE_DEBUG_ONLY(, description));
			break;

		default:
			UNREACHABLE();
		}

		#if defined(VULKAN_VALIDATE_RESOURCE_VISIBILITY)
			auto guidForVisibility = GetGuidForVisibility(resourceView);
			if (resourceView.GetType() == ResourceView::Type::ImageView && guidForVisibility) {
				_pendingResourceVisibilityChanges.push_back(guidForVisibility);
				_pendingResourceVisibilityChangesSlotAndCount.emplace_back(descriptorSetBindPoint, 1);
			}
		#endif
	}

	void    ProgressiveDescriptorSetBuilder::BindArray(unsigned descriptorSetBindPoint, IteratorRange<const ResourceView*const*> resources, StringSection<> shaderOrDescSetVariable)
	{
		assert(!resources.empty());
		assert(resources[0]);
		#if defined(VULKAN_VERBOSE_DEBUG)
			std::string description{"ArrayOfResourceViews"};
		#endif

		assert(descriptorSetBindPoint < _signature.size());
		auto slotType = _signature[descriptorSetBindPoint]._type;
		auto signatureArrayCount = _signature[descriptorSetBindPoint]._count;
		auto vkSlotType = AsVkDescriptorType(slotType);
		assert(resources.size() <= signatureArrayCount);
		#if defined(_DEBUG)
			const auto& physDevLimits = GetObjectFactory().GetPhysicalDeviceProperties().limits;
		#endif

		switch (resources[0]->GetType()) {
		case ResourceView::Type::ImageView:
			{
				#if defined(_DEBUG)
					if (	vkSlotType != VK_DESCRIPTOR_TYPE_SAMPLER
						&&  vkSlotType != VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER
						&&  vkSlotType != VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE
						&&  vkSlotType != VK_DESCRIPTOR_TYPE_STORAGE_IMAGE
						&&  vkSlotType != VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT)
						Throw(std::runtime_error(StringMeld<256>() << "Binding mismatch for shader variable (" << shaderOrDescSetVariable << ") when binding resource (" << (resources[0]->GetVulkanResource() ? resources[0]->GetVulkanResource()->GetName() : StringSection<>{}) << ")"));
				#endif

				VkDescriptorImageInfo* imageInfos = AllocateInfos<VkDescriptorImageInfo>(resources.size());
				unsigned minElementIdx = UINT_MAX, maxElementIdx = 0;
				for (unsigned c=0; c<signatureArrayCount; ++c) {
					if (c < resources.size() && resources[c]) {
						assert(resources[c]->GetType() == ResourceView::Type::ImageView);
						assert(resources[c]->GetVulkanResource() && resources[c]->GetImageView());
						imageInfos[c] = AsVkDescriptorImageInfo(*resources[c]);
						minElementIdx = std::min(minElementIdx, c);
						maxElementIdx = std::max(maxElementIdx, c);
					} else {
						imageInfos[c] = {};		// we don't know the correct dummy type to apply here, so we can't set a good binding
					}
				}
				if (minElementIdx <= maxElementIdx)
					WriteArrayBinding<VkDescriptorImageInfo>(
						descriptorSetBindPoint,
						AsVkDescriptorType(slotType),
						minElementIdx,
						MakeIteratorRange(&imageInfos[minElementIdx], &imageInfos[maxElementIdx+1])
						VULKAN_VERBOSE_DEBUG_ONLY(, description));
			}
			break;

		case ResourceView::Type::BufferAndRange:
			{
				#if defined(_DEBUG)
					if (	vkSlotType != VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER
						&&  vkSlotType != VK_DESCRIPTOR_TYPE_STORAGE_BUFFER
						&&  vkSlotType != VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC
						&&  vkSlotType != VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC)
						Throw(std::runtime_error(StringMeld<256>() << "Binding mismatch for shader variable (" << shaderOrDescSetVariable << ") when binding buffer (" << (resources[0]->GetVulkanResource() ? resources[0]->GetVulkanResource()->GetName() : StringSection<>{}) << ")"));
				#endif

				VkDescriptorBufferInfo* bufferInfos = AllocateInfos<VkDescriptorBufferInfo>(resources.size());
				unsigned minElementIdx = UINT_MAX, maxElementIdx = 0;
				for (unsigned c=0; c<signatureArrayCount; ++c) {
					if (c < resources.size() && resources[c]) {
						assert(resources[c]->GetType() == ResourceView::Type::BufferAndRange);
						assert(resources[c]->GetVulkanResource() && resources[c]->GetVulkanResource()->GetBuffer());
						auto range = resources[c]->GetBufferRangeOffsetAndSize();
						uint64_t rangeBegin = range.first, rangeSize = range.second;
						if (rangeBegin == 0 && rangeSize == 0)
							rangeSize = VK_WHOLE_SIZE;
						assert(rangeSize != 0);
						#if defined(_DEBUG)
							if (vkSlotType == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER || vkSlotType == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC)
								assert((rangeBegin % physDevLimits.minUniformBufferOffsetAlignment) == 0);
							else
								assert((rangeBegin % physDevLimits.minStorageBufferOffsetAlignment) == 0);
						#endif
						bufferInfos[c] = VkDescriptorBufferInfo { resources[c]->GetVulkanResource()->GetBuffer(), rangeBegin, rangeSize };
						minElementIdx = std::min(minElementIdx, c);
						maxElementIdx = std::max(maxElementIdx, c);
					} else {
						bufferInfos[c] = {};
					}
				}
				if (minElementIdx <= maxElementIdx)
					WriteArrayBinding<VkDescriptorBufferInfo>(
						descriptorSetBindPoint,
						AsVkDescriptorType(slotType),
						minElementIdx,
						MakeIteratorRange(&bufferInfos[minElementIdx], &bufferInfos[maxElementIdx+1])
						VULKAN_VERBOSE_DEBUG_ONLY(, description));
			}
			break;

		case ResourceView::Type::BufferView:
			{
				#if defined(_DEBUG)
					if (	vkSlotType != VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER
						&&  vkSlotType != VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER)
						Throw(std::runtime_error(StringMeld<256>() << "Binding mismatch for shader variable (" << shaderOrDescSetVariable << ") when binding buffer (" << (resources[0]->GetVulkanResource() ? resources[0]->GetVulkanResource()->GetName() : StringSection<>{}) << ")"));
				#endif

				VkBufferView* bufferViews = AllocateInfos<VkBufferView>(resources.size());
				unsigned minElementIdx = UINT_MAX, maxElementIdx = 0;
				for (unsigned c=0; c<resources.size(); ++c) {
					if (c < resources.size() && resources[c]) {
						assert(resources[c]->GetType() == ResourceView::Type::BufferView);
						bufferViews[c] = resources[c]->GetBufferView();
						minElementIdx = std::min(minElementIdx, c);
						maxElementIdx = std::max(maxElementIdx, c);
					} else {
						bufferViews[c] = {};
					}
				}

				if (minElementIdx <= maxElementIdx)
					WriteArrayBinding<VkBufferView>(
						descriptorSetBindPoint,
						AsVkDescriptorType(slotType),
						minElementIdx,
						MakeIteratorRange(&bufferViews[minElementIdx], &bufferViews[maxElementIdx+1])
						VULKAN_VERBOSE_DEBUG_ONLY(, description));
			}
			break;

		default:
			UNREACHABLE();
		}

		#if defined(VULKAN_VALIDATE_RESOURCE_VISIBILITY)
			if (resources[0]->GetType() == ResourceView::Type::ImageView) {
				unsigned count = 0;
				for (const auto& r:resources)
					if (r)
						if (auto g=GetGuidForVisibility(*r)) {
							_pendingResourceVisibilityChanges.push_back(g);
							++count;
						}
				if (count)
					_pendingResourceVisibilityChangesSlotAndCount.emplace_back(descriptorSetBindPoint, count);
			}
		#endif
	}

	void    ProgressiveDescriptorSetBuilder::Bind(unsigned descriptorSetBindPoint, VkDescriptorBufferInfo uniformBuffer, StringSection<> shaderOrDescSetVariable, StringSection<> bufferDescription)
	{
		assert(descriptorSetBindPoint < _signature.size());
		auto slotType = _signature[descriptorSetBindPoint]._type;
		assert(_signature[descriptorSetBindPoint]._count == 1);
		assert(uniformBuffer.buffer);
		assert(uniformBuffer.range != 0); 
		auto vkSlotType = AsVkDescriptorType(slotType);

		#if defined(_DEBUG)
			if (	vkSlotType != VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER
				&&  vkSlotType != VK_DESCRIPTOR_TYPE_STORAGE_BUFFER
				&&  vkSlotType != VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC
				&&  vkSlotType != VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC)
				Throw(std::runtime_error(StringMeld<256>() << "Binding mismatch for shader variable (" << shaderOrDescSetVariable << ") when binding buffer (" << bufferDescription << ")"));
		#endif

		switch (slotType) {
		case DescriptorType::UniformBuffer:
		case DescriptorType::UnorderedAccessBuffer:
		case DescriptorType::UniformTexelBuffer:
		case DescriptorType::UnorderedAccessTexelBuffer:
		case DescriptorType::UniformBufferDynamicOffset:
		case DescriptorType::UnorderedAccessBufferDynamicOffset:
			WriteBinding(
				descriptorSetBindPoint,
				vkSlotType,
				uniformBuffer, true
				VULKAN_VERBOSE_DEBUG_ONLY(, bufferDescription.AsString()));
			break;

		default:
			UNREACHABLE();
		}
	}

	void    ProgressiveDescriptorSetBuilder::Bind(unsigned descriptorSetBindPoint, VkSampler sampler, StringSection<> shaderOrDescSetVariable, StringSection<> samplerDescription)
	{
		assert(descriptorSetBindPoint < _signature.size());
		auto slotType = _signature[descriptorSetBindPoint]._type;
		assert(_signature[descriptorSetBindPoint]._count == 1);
		auto vkSlotType = AsVkDescriptorType(slotType);

		#if defined(_DEBUG)
			if (	vkSlotType != VK_DESCRIPTOR_TYPE_SAMPLER
				&&  vkSlotType != VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
				Throw(std::runtime_error(StringMeld<256>() << "Binding mismatch for shader variable (" << shaderOrDescSetVariable << ") when binding sampler (" << samplerDescription << ")"));
		#endif

		switch (slotType) {
		case DescriptorType::Sampler:
			WriteBinding(
				descriptorSetBindPoint,
				vkSlotType,
				VkDescriptorImageInfo { sampler }, true
				VULKAN_VERBOSE_DEBUG_ONLY(, samplerDescription.AsString()));
			break;

		default:
			UNREACHABLE();
		}
	}

	VkDescriptorImageInfo* ProgressiveDescriptorSetBuilder::AllocateBlankImageInfos(GlobalPools& globalPools, ResourceDims shaderTypeExpected, unsigned count)
	{
		auto* result = AllocateInfos<VkDescriptorImageInfo>(count);
		switch (shaderTypeExpected)
		{
		case ResourceDims::Dim1D:
			for (unsigned c=0; c<count; ++c)
				result[c] = VkDescriptorImageInfo {
					globalPools._dummyResources._blankSampler->GetUnderlying(),
					globalPools._dummyResources._blankImage1DSrv.GetImageView(),
					VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
			break;
		case ResourceDims::Dim2D:
			for (unsigned c=0; c<count; ++c)
				result[c] = VkDescriptorImageInfo {
					globalPools._dummyResources._blankSampler->GetUnderlying(),
					globalPools._dummyResources._blankImage2DSrv.GetImageView(),
					VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
			break;
		case ResourceDims::Dim3D:
			for (unsigned c=0; c<count; ++c)
				result[c] = VkDescriptorImageInfo {
					globalPools._dummyResources._blankSampler->GetUnderlying(),
					globalPools._dummyResources._blankImage3DSrv.GetImageView(),
					VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
			break;
		case ResourceDims::DimCube:
			for (unsigned c=0; c<count; ++c)
				result[c] = VkDescriptorImageInfo {
					globalPools._dummyResources._blankSampler->GetUnderlying(),
					globalPools._dummyResources._blankImageCubeSrv.GetImageView(),
					VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
			break;

		case ResourceDims::Dim1DArray:
			for (unsigned c=0; c<count; ++c)
				result[c] = VkDescriptorImageInfo {
					globalPools._dummyResources._blankSampler->GetUnderlying(),
					globalPools._dummyResources._blankImage1DArraySrv.GetImageView(),
					VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
			break;
		case ResourceDims::Dim2DArray:
			for (unsigned c=0; c<count; ++c)
				result[c] = VkDescriptorImageInfo {
					globalPools._dummyResources._blankSampler->GetUnderlying(),
					globalPools._dummyResources._blankImage2DArraySrv.GetImageView(),
					VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
			break;
		case ResourceDims::DimCubeArray:
			for (unsigned c=0; c<count; ++c)
				result[c] = VkDescriptorImageInfo {
					globalPools._dummyResources._blankSampler->GetUnderlying(),
					globalPools._dummyResources._blankImageCubeArraySrv.GetImageView(),
					VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
			break;

		case ResourceDims::DimBuffer:
		case ResourceDims::DimInputAttachment:
			UNREACHABLE();	// invalid cases

		default:
			// fallback to 2d image (multisample types will fallback here currently, because they are used only for specific shaders)
			for (unsigned c=0; c<count; ++c)
				result[c] = VkDescriptorImageInfo {
					globalPools._dummyResources._blankSampler->GetUnderlying(),
					globalPools._dummyResources._blankImage2DSrv.GetImageView(),
					VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
			break;
		}

		return result;
	}

	VkDescriptorImageInfo* ProgressiveDescriptorSetBuilder::AllocateBlankUavImageInfos(GlobalPools& globalPools, ResourceDims shaderTypeExpected, unsigned count)
	{
		// binding dummy "UAV" resources is a little questionable; because any data written out will be passed onto the next user
		// since they are shared, there's also lots of race condition hazards
		// This should only be used as a safety barrier; to avoid a GPU crash and allow debugging
		Log(Warning) << "Binding dummy storage image to descriptor set. Do not rely on this behaviour because the contents of the dummies is undefined" << std::endl;
		auto* result = AllocateInfos<VkDescriptorImageInfo>(count);
		switch (shaderTypeExpected)
		{
		case ResourceDims::Dim1D:
			for (unsigned c=0; c<count; ++c)
				result[c] = VkDescriptorImageInfo {
					globalPools._dummyResources._blankSampler->GetUnderlying(),
					globalPools._dummyResources._blankImage1DUav.GetImageView(),
					VK_IMAGE_LAYOUT_GENERAL };
			break;
		case ResourceDims::Dim2D:
			for (unsigned c=0; c<count; ++c)
				result[c] = VkDescriptorImageInfo {
					globalPools._dummyResources._blankSampler->GetUnderlying(),
					globalPools._dummyResources._blankImage2DUav.GetImageView(),
					VK_IMAGE_LAYOUT_GENERAL };
			break;
		case ResourceDims::Dim3D:
			for (unsigned c=0; c<count; ++c)
				result[c] = VkDescriptorImageInfo {
					globalPools._dummyResources._blankSampler->GetUnderlying(),
					globalPools._dummyResources._blankImage3DUav.GetImageView(),
					VK_IMAGE_LAYOUT_GENERAL };
			break;
		case ResourceDims::DimCube:
			for (unsigned c=0; c<count; ++c)
				result[c] = VkDescriptorImageInfo {
					globalPools._dummyResources._blankSampler->GetUnderlying(),
					globalPools._dummyResources._blankImageCubeUav.GetImageView(),
					VK_IMAGE_LAYOUT_GENERAL };
			break;

		case ResourceDims::Dim1DArray:
			for (unsigned c=0; c<count; ++c)
				result[c] = VkDescriptorImageInfo {
					globalPools._dummyResources._blankSampler->GetUnderlying(),
					globalPools._dummyResources._blankImage1DArrayUav.GetImageView(),
					VK_IMAGE_LAYOUT_GENERAL };
			break;
		case ResourceDims::Dim2DArray:
			for (unsigned c=0; c<count; ++c)
				result[c] = VkDescriptorImageInfo {
					globalPools._dummyResources._blankSampler->GetUnderlying(),
					globalPools._dummyResources._blankImage2DArrayUav.GetImageView(),
					VK_IMAGE_LAYOUT_GENERAL };
			break;
		case ResourceDims::DimCubeArray:
			for (unsigned c=0; c<count; ++c)
				result[c] = VkDescriptorImageInfo {
					globalPools._dummyResources._blankSampler->GetUnderlying(),
					globalPools._dummyResources._blankImageCubeArrayUav.GetImageView(),
					VK_IMAGE_LAYOUT_GENERAL };
			break;

		case ResourceDims::DimBuffer:
		case ResourceDims::DimInputAttachment:
			UNREACHABLE();	// invalid cases

		default:
			// fallback to 2d image (multisample types will fallback here currently, because they are used only for specific shaders)
			for (unsigned c=0; c<count; ++c)
				result[c] = VkDescriptorImageInfo {
					globalPools._dummyResources._blankSampler->GetUnderlying(),
					globalPools._dummyResources._blankImage2DUav.GetImageView(),
					VK_IMAGE_LAYOUT_GENERAL };
			break;
		}

		return result;
	}

	uint64_t	ProgressiveDescriptorSetBuilder::BindDummyDescriptors(
		GlobalPools& globalPools, uint64_t dummyDescWriteMask,
		IteratorRange<const ResourceDims*> shaderTypesExpected)
	{
		uint64_t bindingsWrittenTo = 0u;

		auto& blankBuffer = AllocateInfo(
			VkDescriptorBufferInfo { 
				globalPools._dummyResources._blankBuffer->GetBuffer(),
				0, VK_WHOLE_SIZE });	
		auto& blankSampler = AllocateInfo(
			VkDescriptorImageInfo {
				globalPools._dummyResources._blankSampler->GetUnderlying(),
				nullptr,
				VK_IMAGE_LAYOUT_UNDEFINED });
		auto& blankStorageBuffer = AllocateInfo(
			VkDescriptorBufferInfo {
				globalPools._dummyResources._blankBufferUav.GetVulkanResource()->GetBuffer(),
				0, VK_WHOLE_SIZE });

		unsigned minBit = xl_ctz8(dummyDescWriteMask);
		unsigned maxBit = std::min(64u - xl_clz8(dummyDescWriteMask), (unsigned)_signature.size()-1);

		for (unsigned bIndex=minBit; bIndex<=maxBit; ++bIndex) {
			if (!(dummyDescWriteMask & (1ull<<uint64(bIndex)))) continue;

			auto b = _signature[bIndex]._type;
			if (_signature[bIndex]._count == 1) {
				if (b == DescriptorType::UniformBuffer || b == DescriptorType::UniformBufferDynamicOffset || b == DescriptorType::UnorderedAccessBufferDynamicOffset) {
					WriteBinding(
						bIndex,
						AsVkDescriptorType(b),
						blankBuffer, false
						VULKAN_VERBOSE_DEBUG_ONLY(, s_dummyDescriptorString));
				} else if (b == DescriptorType::SampledTexture) {
					auto& blankImage = *AllocateBlankImageInfos(globalPools, shaderTypesExpected[bIndex], 1);
					WriteBinding(
						bIndex,
						AsVkDescriptorType(b),
						blankImage, false
						VULKAN_VERBOSE_DEBUG_ONLY(, s_dummyDescriptorString));
				} else if (b == DescriptorType::Sampler) {
					WriteBinding(
						bIndex,
						AsVkDescriptorType(b),
						blankSampler, false
						VULKAN_VERBOSE_DEBUG_ONLY(, s_dummyDescriptorString));
				} else if (b == DescriptorType::UnorderedAccessTexture) {
					auto& blankImage = *AllocateBlankUavImageInfos(globalPools, shaderTypesExpected[bIndex], 1);
					WriteBinding(
						bIndex,
						AsVkDescriptorType(b),
						blankImage, false
						VULKAN_VERBOSE_DEBUG_ONLY(, s_dummyDescriptorString));
				} else if (b == DescriptorType::UnorderedAccessBuffer) {
					Log(Warning) << "Binding dummy storage buffer to descriptor set. Do not rely on this behaviour because the contents of the dummies is undefined" << std::endl;
					WriteBinding(
						bIndex,
						AsVkDescriptorType(b),
						blankStorageBuffer, false
						VULKAN_VERBOSE_DEBUG_ONLY(, s_dummyDescriptorString));
				} else if (b == DescriptorType::UniformTexelBuffer || b == DescriptorType::UnorderedAccessTexelBuffer) {
					/* note sure if there's a good dummy here, because we may have to match the texel format from the shader */
				} else if (b == DescriptorType::InputAttachment) {
					/* not sure what would be a correct dummy descriptor for an input attachment */
				} else if (b == DescriptorType::Empty) {
					/* treated as empty */
				} else {
					UNREACHABLE();
					continue;
				}
			} else {
				if (b == DescriptorType::UnorderedAccessBuffer) {
					Log(Warning) << "Binding dummy storage buffer to descriptor set. Do not rely on this behaviour because the contents of the dummies is undefined" << std::endl;
					auto* bindingInfos = AllocateInfos<std::decay_t<decltype(blankStorageBuffer)>>(_signature[bIndex]._count);
					for (unsigned c=0; c<_signature[bIndex]._count; ++c)
						bindingInfos[c] = blankStorageBuffer;
					WriteArrayBinding<std::decay_t<decltype(blankStorageBuffer)>>(
						bIndex,
						AsVkDescriptorType(b),
						0, MakeIteratorRange(bindingInfos, bindingInfos+_signature[bIndex]._count)
						VULKAN_VERBOSE_DEBUG_ONLY(, s_dummyDescriptorString));
				} else if (b == DescriptorType::SampledTexture) {
					auto* bindingInfos = AllocateBlankImageInfos(globalPools, shaderTypesExpected[bIndex], _signature[bIndex]._count);
					WriteArrayBinding<std::decay_t<decltype(*bindingInfos)>>(
						bIndex,
						AsVkDescriptorType(b),
						0, MakeIteratorRange(bindingInfos, bindingInfos+_signature[bIndex]._count)
						VULKAN_VERBOSE_DEBUG_ONLY(, s_dummyDescriptorString));
				} else if (b == DescriptorType::UnorderedAccessTexture) {
					auto* bindingInfos = AllocateBlankUavImageInfos(globalPools, shaderTypesExpected[bIndex], _signature[bIndex]._count);
					WriteArrayBinding<std::decay_t<decltype(*bindingInfos)>>(
						bIndex,
						AsVkDescriptorType(b),
						0, MakeIteratorRange(bindingInfos, bindingInfos+_signature[bIndex]._count)
						VULKAN_VERBOSE_DEBUG_ONLY(, s_dummyDescriptorString));
				} else if (b == DescriptorType::Empty) {
					/* treated as empty */
				} else {
					UNREACHABLE();
					continue;
				}
			}
			bindingsWrittenTo |= 1ull << uint64(bIndex);
		}

		return bindingsWrittenTo;
	}

	uint64_t	ProgressiveDescriptorSetBuilder::FlushChanges(
		VkDevice device,
		VkDescriptorSet destination,
		VkDescriptorSet copyPrevDescriptors, uint64_t prevDescriptorMask
		VULKAN_VERBOSE_DEBUG_ONLY(, DescriptorSetDebugInfo& description))
	{
		// Flush out changes to the given descriptor set.
		// Copy unwritten descriptors from copyPrevDescriptors
		// Return a mask of the writes that we actually committed

		VkCopyDescriptorSet copies[64];
		unsigned copyCount = 0;

		if (copyPrevDescriptors && prevDescriptorMask) {
			auto filledButNotWritten = prevDescriptorMask & ~_sinceLastFlush;
			unsigned msbBit = 64u - xl_clz8(filledButNotWritten);
			unsigned lsbBit = xl_ctz8(filledButNotWritten);
			for (unsigned b=lsbBit; b<=msbBit; ++b) {
				if (filledButNotWritten & (1ull<<b)) {
					assert(copyCount < dimof(copies));
					auto& cpy = copies[copyCount++];
					cpy.sType = VK_STRUCTURE_TYPE_COPY_DESCRIPTOR_SET;
					cpy.pNext = nullptr;

					cpy.srcSet = copyPrevDescriptors;
					cpy.srcBinding = b;
					cpy.srcArrayElement = 0;

					cpy.dstSet = destination;
					cpy.dstBinding = b;
					cpy.dstArrayElement = 0;
					cpy.descriptorCount = 1;        // (we can set this higher to set multiple sequential descriptors)
				}
			}
		}

		for (unsigned c=0; c<_pendingWrites; ++c)
			_writes[c].dstSet = destination;
		vkUpdateDescriptorSets(
			device, 
			_pendingWrites, _writes, 
			copyCount, copies);

		_pendingWrites = 0;
		_pendingImageInfos = _pendingBufferInfos = _pendingBufferViews = 0;
		auto result = _sinceLastFlush;
		_sinceLastFlush = 0;

		#if defined(VULKAN_VERBOSE_DEBUG)
			if (description._bindingDescriptions.size() < _verboseDescription._bindingDescriptions.size())
				description._bindingDescriptions.resize(_verboseDescription._bindingDescriptions.size());
			for (unsigned b=0; b<_verboseDescription._bindingDescriptions.size(); ++b) {
				if (!(result && 1ull<<uint64_t(b))) continue;
				description._bindingDescriptions[b] = _verboseDescription._bindingDescriptions[b];
			}
		#endif

		#if defined(VULKAN_VALIDATE_RESOURCE_VISIBILITY)
			_pendingResourceVisibilityChanges.clear();
			_pendingResourceVisibilityChangesSlotAndCount.clear();
		#endif

		return result;
	}

	bool    ProgressiveDescriptorSetBuilder::HasChanges() const
	{
		// note --  we have to bind some descriptor set for the first draw of the frame,
		//          even if nothing has been bound! So, when _activeDescSets is empty
		//          we must return true here.
		return _sinceLastFlush != 0;
	}

	void    ProgressiveDescriptorSetBuilder::Reset()
	{
		_pendingWrites = 0u;
		_pendingImageInfos = 0u;
		_pendingBufferInfos = 0u;
		_pendingBufferViews = 0u;

		XlZeroMemory(_bufferInfo);
		XlZeroMemory(_imageInfo);
		XlZeroMemory(_writes);
		#if defined(VULKAN_VALIDATE_RESOURCE_VISIBILITY)
			_pendingResourceVisibilityChanges.clear();
		#endif

		_sinceLastFlush = 0x0u;
	}

	ProgressiveDescriptorSetBuilder::ProgressiveDescriptorSetBuilder(
		IteratorRange<const DescriptorSlot*> signature,
		Flags::BitField flags)
	: _signature(signature)
	{
		_flags = flags;
		_sinceLastFlush = 0;
		XlZeroMemory(_bufferInfo);
		XlZeroMemory(_imageInfo);
		XlZeroMemory(_writes);
		#if defined(VULKAN_VALIDATE_RESOURCE_VISIBILITY)
			_pendingResourceVisibilityChanges.clear();
		#endif
	}

	ProgressiveDescriptorSetBuilder::~ProgressiveDescriptorSetBuilder()
	{
	}

	ProgressiveDescriptorSetBuilder::ProgressiveDescriptorSetBuilder(ProgressiveDescriptorSetBuilder&& moveFrom)
	: _signature(std::move(moveFrom._signature))
	#if defined(VULKAN_VERBOSE_DEBUG)
		, _verboseDescription(std::move(moveFrom._verboseDescription))
	#endif
	{
		moveFrom._signature = {};
		_pendingWrites = moveFrom._pendingWrites; moveFrom._pendingWrites = 0;
        _pendingImageInfos = moveFrom._pendingImageInfos; moveFrom._pendingImageInfos = 0;
        _pendingBufferInfos = moveFrom._pendingBufferInfos; moveFrom._pendingBufferInfos = 0;
		_pendingBufferViews = moveFrom._pendingBufferViews; moveFrom._pendingBufferViews = 0;
        _sinceLastFlush = moveFrom._sinceLastFlush; moveFrom._sinceLastFlush = 0;
		_flags = moveFrom._flags; moveFrom._flags = 0;

		std::memcpy(_bufferInfo, moveFrom._bufferInfo, sizeof(_bufferInfo));
		std::memcpy(_imageInfo, moveFrom._imageInfo, sizeof(_imageInfo));
		std::memcpy(_writes, moveFrom._writes, sizeof(_writes));
		XlZeroMemory(moveFrom._bufferInfo);
		XlZeroMemory(moveFrom._imageInfo);
		XlZeroMemory(moveFrom._writes);

		#if defined(VULKAN_VALIDATE_RESOURCE_VISIBILITY)
			_pendingResourceVisibilityChanges = std::move(moveFrom._pendingResourceVisibilityChanges);
			_pendingResourceVisibilityChangesSlotAndCount = std::move(moveFrom._pendingResourceVisibilityChangesSlotAndCount);
		#endif
	}

	ProgressiveDescriptorSetBuilder& ProgressiveDescriptorSetBuilder::operator=(ProgressiveDescriptorSetBuilder&& moveFrom)
	{
		_signature = std::move(moveFrom._signature);
		moveFrom._signature = {};
		#if defined(VULKAN_VERBOSE_DEBUG)
			_verboseDescription = std::move(moveFrom._verboseDescription);
		#endif

		_pendingWrites = moveFrom._pendingWrites; moveFrom._pendingWrites = 0;
        _pendingImageInfos = moveFrom._pendingImageInfos; moveFrom._pendingImageInfos = 0;
        _pendingBufferInfos = moveFrom._pendingBufferInfos; moveFrom._pendingBufferInfos = 0;
		_pendingBufferViews = moveFrom._pendingBufferViews; moveFrom._pendingBufferViews = 0;
        _sinceLastFlush = moveFrom._sinceLastFlush; moveFrom._sinceLastFlush = 0;
		_flags = moveFrom._flags; moveFrom._flags = 0;

		std::memcpy(_bufferInfo, moveFrom._bufferInfo, sizeof(_bufferInfo));
		std::memcpy(_imageInfo, moveFrom._imageInfo, sizeof(_imageInfo));
		std::memcpy(_writes, moveFrom._writes, sizeof(_writes));
		XlZeroMemory(moveFrom._bufferInfo);
		XlZeroMemory(moveFrom._imageInfo);
		XlZeroMemory(moveFrom._writes);

		#if defined(VULKAN_VALIDATE_RESOURCE_VISIBILITY)
			_pendingResourceVisibilityChanges = std::move(moveFrom._pendingResourceVisibilityChanges);
			_pendingResourceVisibilityChangesSlotAndCount = std::move(moveFrom._pendingResourceVisibilityChangesSlotAndCount);
		#endif
		return *this;
	}

	#if defined(VULKAN_VERBOSE_DEBUG)

		static char GetRegisterPrefix(LegacyRegisterBindingDesc::RegisterType regType)
		{
			switch (regType) {
			case LegacyRegisterBindingDesc::RegisterType::Sampler: return 's';
			case LegacyRegisterBindingDesc::RegisterType::ShaderResource: return 't';
			case LegacyRegisterBindingDesc::RegisterType::ConstantBuffer: return 'b';
			case LegacyRegisterBindingDesc::RegisterType::UnorderedAccess: return 'u';
			default:
				assert(0);
				return ' ';
			}
		}

		static const std::string s_columnHeader0 = "Root Signature";
		static const std::string s_columnHeader2 = "Binding";
		static const std::string s_columnHeader3 = "Legacy Binding";

		const char* AsString(DescriptorType type)
		{
			const char* descriptorTypeNames[] = {
				"SampledTexture",
				"UniformBuffer",
				"UnorderedAccessTexture",
				"UnorderedAccessBuffer",
				"Sampler",
				"Unknown"
			};
			if (unsigned(type) < dimof(descriptorTypeNames))
				return descriptorTypeNames[unsigned(type)];
			return "<<unknown>>";
		}

		std::ostream& WriteDescriptorSet(
			std::ostream& stream,
			const DescriptorSetDebugInfo& bindingDescription,
			IteratorRange<const DescriptorSlot*> signature,
			const std::string& descriptorSetName,
			const LegacyRegisterBindingDesc& legacyBinding,
			IteratorRange<const CompiledShaderByteCode**> compiledShaderByteCode,
			unsigned descriptorSetIndex, bool isBound)
		{
			std::vector<std::string> signatureColumn;
			std::vector<std::string> shaderColumns[(unsigned)ShaderStage::Max];
			std::vector<std::string> legacyBindingColumn;

			size_t signatureColumnMax = 0, bindingColumnMax = 0, legacyBindingColumnMax = 0;
			size_t shaderColumnMax[(unsigned)ShaderStage::Max] = {};

			signatureColumn.resize(signature.size());
			for (unsigned c=0; c<signature.size(); ++c) {
				signatureColumn[c] = std::string{AsString(signature[c]._type)};
				signatureColumnMax = std::max(signatureColumnMax, signatureColumn[c].size());
			}
			signatureColumnMax = std::max(signatureColumnMax, s_columnHeader0.size());

			for (unsigned stage=0; stage<std::min((unsigned)ShaderStage::Max, (unsigned)compiledShaderByteCode.size()); ++stage) {
				if (!compiledShaderByteCode[stage] || compiledShaderByteCode[stage]->GetByteCode().empty())
					continue;

				shaderColumns[stage].reserve(signature.size());
				SPIRVReflection reflection{compiledShaderByteCode[stage]->GetByteCode()};
				for (const auto& v:reflection._bindings) {
					if (v.second._descriptorSet != descriptorSetIndex || v.second._bindingPoint == ~0u)
						continue;
					if (shaderColumns[stage].size() <= v.second._bindingPoint)
						shaderColumns[stage].resize(v.second._bindingPoint+1);
					
					shaderColumns[stage][v.second._bindingPoint] = reflection.GetName(v.first).AsString();
					shaderColumnMax[stage] = std::max(shaderColumnMax[stage], shaderColumns[stage][v.second._bindingPoint].size());
				}

				if (shaderColumnMax[stage] != 0)
					shaderColumnMax[stage] = std::max(shaderColumnMax[stage], std::strlen(AsString((ShaderStage)stage)));
			}

			for (const auto&b:bindingDescription._bindingDescriptions)
				bindingColumnMax = std::max(bindingColumnMax, b._description.size());
			bindingColumnMax = std::max(bindingColumnMax, s_columnHeader2.size());

			auto rowCount = (unsigned)std::max(signatureColumn.size(), bindingDescription._bindingDescriptions.size());
			for (unsigned stage=0; stage<(unsigned)ShaderStage::Max; ++stage)
				rowCount = std::max(rowCount, (unsigned)shaderColumns[stage].size());

			legacyBindingColumn.resize(rowCount);
			for (unsigned regType=0; regType<(unsigned)LegacyRegisterBindingDesc::RegisterType::Unknown; ++regType) {
				auto prefix = GetRegisterPrefix((LegacyRegisterBindingDesc::RegisterType)regType);
				auto entries = legacyBinding.GetEntries((LegacyRegisterBindingDesc::RegisterType)regType, LegacyRegisterBindingDesc::RegisterQualifier::None);
				for (const auto&e:entries)
					if (e._targetDescriptorSetIdx == descriptorSetIndex && e._targetBegin < rowCount) {
						for (unsigned t=e._targetBegin; t<std::min(e._targetEnd, rowCount); ++t) {
							if (!legacyBindingColumn[t].empty())
								legacyBindingColumn[t] += ", ";
							legacyBindingColumn[t] += prefix + std::to_string(t-e._targetBegin+e._begin);
						}
					}
			}
			for (const auto&e:legacyBindingColumn)
				legacyBindingColumnMax = std::max(legacyBindingColumnMax, e.size());
			if (legacyBindingColumnMax)
				legacyBindingColumnMax = std::max(legacyBindingColumnMax, s_columnHeader3.size());

			stream << "[" << descriptorSetIndex << "] Descriptor Set: " << descriptorSetName;
			if (isBound) {
				stream << " (bound with UniformsStream: " << bindingDescription._descriptorSetInfo << ")" << std::endl;
			} else {
				stream << " (not bound to any UniformsStream)" << std::endl;
			}
			stream << " " << s_columnHeader0 << StreamIndent(unsigned(signatureColumnMax - s_columnHeader0.size())) << " | ";
			size_t accumulatedShaderColumns = 0;
			for (unsigned stage=0; stage<(unsigned)ShaderStage::Max; ++stage) {
				if (!shaderColumnMax[stage]) continue;
				auto* title = AsString((ShaderStage)stage);
				stream << title << StreamIndent(unsigned(shaderColumnMax[stage] - std::strlen(title))) << " | ";
				accumulatedShaderColumns += shaderColumnMax[stage] + 3;
			}
			stream << s_columnHeader2 << StreamIndent(unsigned(bindingColumnMax - s_columnHeader2.size()));
			if (legacyBindingColumnMax) {
				stream << " | " << s_columnHeader3 << StreamIndent(unsigned(legacyBindingColumnMax - s_columnHeader3.size()));
			}
			stream << std::endl;
			auto totalWidth = signatureColumnMax + bindingColumnMax + accumulatedShaderColumns + 5;
			if (legacyBindingColumnMax) totalWidth += 3 + legacyBindingColumnMax;
			stream << StreamIndent{unsigned(totalWidth), '-'} << std::endl;

			for (unsigned row=0; row<rowCount; ++row) {
				stream << " ";
				if (row < signatureColumn.size()) {
					stream << signatureColumn[row] << StreamIndent(unsigned(signatureColumnMax - signatureColumn[row].size()));
				} else {
					stream << StreamIndent(unsigned(signatureColumnMax));
				}
				stream << " | ";

				for (unsigned stage=0; stage<(unsigned)ShaderStage::Max; ++stage) {
					if (!shaderColumnMax[stage]) continue;
					if (row < shaderColumns[stage].size()) {
						stream << shaderColumns[stage][row] << StreamIndent(unsigned(shaderColumnMax[stage] - shaderColumns[stage][row].size()));
					} else {
						stream << StreamIndent(unsigned(shaderColumnMax[stage]));
					}
					stream << " | ";
				}

				if (row < bindingDescription._bindingDescriptions.size()) {
					stream << bindingDescription._bindingDescriptions[row]._description << StreamIndent(unsigned(bindingColumnMax - bindingDescription._bindingDescriptions[row]._description.size()));
				} else {
					stream << StreamIndent(unsigned(bindingColumnMax));
				}

				if (legacyBindingColumnMax) {
					stream << " | ";
					if (row < legacyBindingColumn.size()) {
						stream << legacyBindingColumn[row] << StreamIndent(unsigned(legacyBindingColumnMax - legacyBindingColumn[row].size()));
					} else {
						stream << StreamIndent(unsigned(legacyBindingColumnMax));
					}
				}

				stream << std::endl;
			}
			stream << StreamIndent{unsigned(totalWidth), '-'} << std::endl;

			return stream;
		}

	#endif

/////////////////////////////////////////////////////////////////////////////////////////////////////////

	CompiledDescriptorSetLayout::CompiledDescriptorSetLayout(
		ObjectFactory& factory, 
		IteratorRange<const DescriptorSlot*> srcLayout,
		IteratorRange<const  std::shared_ptr<ISampler>*> fixedSamplers,
		VkShaderStageFlags stageFlags,
		uint64_t hashCode,
		const std::string& name)
	: _vkShaderStageMask(stageFlags)
	, _descriptorSlots(srcLayout.begin(), srcLayout.end())
	, _fixedSamplers(fixedSamplers.begin(), fixedSamplers.end())
	, _hashCode(hashCode)
	#if defined(_DEBUG)
		, _name(name)
	#endif
	{
		std::vector<VkDescriptorSetLayoutBinding> bindings;
		bindings.reserve(srcLayout.size());
		VLA(VkSampler, tempSamplerArray, _fixedSamplers.size());
		uint64_t dummyMask = 0;
		for (unsigned bIndex=0; bIndex<(unsigned)srcLayout.size(); ++bIndex) {
			if (srcLayout[bIndex]._type == DescriptorType::Empty) continue;
			
			VkDescriptorSetLayoutBinding dstBinding = {};
			dstBinding.binding = bIndex;
			dstBinding.descriptorType = (VkDescriptorType)AsVkDescriptorType(srcLayout[bIndex]._type);
			dstBinding.descriptorCount = srcLayout[bIndex]._count;
			dstBinding.stageFlags = stageFlags;
			if (dstBinding.descriptorType == VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT) {
				assert(stageFlags & VK_SHADER_STAGE_FRAGMENT_BIT);
				dstBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;	// only fragment shaders can access input attachments
			}
			if (bIndex < _fixedSamplers.size() && _fixedSamplers[bIndex]) {
				tempSamplerArray[bIndex] = checked_cast<SamplerState*>(_fixedSamplers[bIndex].get())->GetUnderlying();
				dstBinding.pImmutableSamplers = &tempSamplerArray[bIndex];
			} else {
				dstBinding.pImmutableSamplers = nullptr;
				dummyMask |= 1ull << uint64_t(bIndex);
			}
			bindings.push_back(dstBinding);
		}
		_layout = factory.CreateDescriptorSetLayout(MakeIteratorRange(bindings));
		_dummyMask = dummyMask;

		#if defined(VULKAN_ENABLE_DEBUG_EXTENSIONS)
			if (factory.GetExtensionFunctions()._setObjectName && !name.empty()) {
				const VkDebugUtilsObjectNameInfoEXT descriptorSetLayoutNameInfo {
					VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT,
					NULL,
					VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT,
					(uint64_t)_layout.get(),
					name.c_str()
				};
				factory.GetExtensionFunctions()._setObjectName(factory.GetDevice().get(), &descriptorSetLayoutNameInfo);
			}
		#endif

		for (auto& i:_descriptorTypesCount) i=0;
		for (const auto& t:srcLayout) {
			switch (t._type) {
			case DescriptorType::SampledTexture: ++_descriptorTypesCount[VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE]; break;
			case DescriptorType::UniformBuffer: ++_descriptorTypesCount[VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER]; break;
			case DescriptorType::UnorderedAccessTexture: ++_descriptorTypesCount[VK_DESCRIPTOR_TYPE_STORAGE_IMAGE]; break;
			case DescriptorType::UnorderedAccessBuffer: ++_descriptorTypesCount[VK_DESCRIPTOR_TYPE_STORAGE_BUFFER]; break;
			case DescriptorType::Sampler: ++_descriptorTypesCount[VK_DESCRIPTOR_TYPE_SAMPLER]; break;
			case DescriptorType::InputAttachment: ++_descriptorTypesCount[VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT]; break;
			case DescriptorType::UniformTexelBuffer: ++_descriptorTypesCount[VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER]; break;
			case DescriptorType::UnorderedAccessTexelBuffer: ++_descriptorTypesCount[VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER]; break;
			case DescriptorType::UniformBufferDynamicOffset: ++_descriptorTypesCount[VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC]; break;
			case DescriptorType::UnorderedAccessBufferDynamicOffset: ++_descriptorTypesCount[VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC]; break;
			default: break;
			}
		}
	}

	CompiledDescriptorSetLayout::~CompiledDescriptorSetLayout()
	{
	}

	bool CompiledDescriptorSetLayout::IsFixedSampler(unsigned slotIdx)
	{
		return (slotIdx < _fixedSamplers.size()) && (_fixedSamplers[slotIdx] != nullptr);
	}

	namespace Internal
	{
		static const std::string s_dummyDescriptorString{"<DummyDescriptor>"};

		const DescriptorSetCacheResult*	CompiledDescriptorSetLayoutCache::CompileDescriptorSetLayout(
			const DescriptorSetSignature& signature,
			const std::string& name,
			VkShaderStageFlags stageFlags)
		{
			ScopedLock(_lock);
			auto hash = HashCombine(signature.GetHashIgnoreNames(), stageFlags);
			auto i = LowerBound(_cache, hash);
			if (i != _cache.end() && i->first == hash)
				return i->second.get();

			auto ds = std::make_unique<DescriptorSetCacheResult>();
			ds->_layout = std::make_unique<CompiledDescriptorSetLayout>(
				*_objectFactory, MakeIteratorRange(signature._slots), MakeIteratorRange(signature._fixedSamplers), 
				stageFlags,
				hash, name);
			
			{
				ProgressiveDescriptorSetBuilder builder { MakeIteratorRange(signature._slots), 0 };
				VLA(ProgressiveDescriptorSetBuilder::ResourceDims, resourceDims, signature._slots.size());
				for (unsigned c=0; c<signature._slots.size(); ++c) resourceDims[c] = ProgressiveDescriptorSetBuilder::ResourceDims::Unknown;
				builder.BindDummyDescriptors(*_globalPools, ds->_layout->GetDummyMask(), MakeIteratorRange(resourceDims, &resourceDims[signature._slots.size()]));
				ds->_blankBindings = _globalPools->_longTermDescriptorPool.Allocate(*ds->_layout);
				VULKAN_VERBOSE_DEBUG_ONLY(ds->_blankBindingsDescription._descriptorSetInfo = s_dummyDescriptorString);
				builder.FlushChanges(
					_objectFactory->GetDevice().get(),
					ds->_blankBindings.get(),
					0, 0 VULKAN_VERBOSE_DEBUG_ONLY(, ds->_blankBindingsDescription));
			}

			i = _cache.insert(i, std::make_pair(hash, std::move(ds)));
			return i->second.get();
		}

		CompiledDescriptorSetLayoutCache::CompiledDescriptorSetLayoutCache(ObjectFactory& objectFactory, GlobalPools& globalPools)
		: _objectFactory(&objectFactory)
		, _globalPools(&globalPools)
		{}

		CompiledDescriptorSetLayoutCache::~CompiledDescriptorSetLayoutCache() {}

		std::shared_ptr<CompiledDescriptorSetLayoutCache> CreateCompiledDescriptorSetLayoutCache()
		{
			return std::make_shared<CompiledDescriptorSetLayoutCache>(GetObjectFactory(), GetGlobalPools());
		}
	}

	void CompiledDescriptorSet::WriteInternal(
		ObjectFactory& factory,
		IteratorRange<const DescriptorSetInitializer::BindTypeAndIdx*> bindsInit,
		const UniformsStream& uniforms,
		WriteFlags::BitField flags)
	{
		constexpr bool clearUnchangedSlots = true;
		// _retainedViews & _retainedSamplers must be per-slot, so we release the previous binding to the
		// slot. Since we will fill in unwritten slots with dummies, we can release all previous retained views & samplers
		// Note that due to the synchronization methods, the actual release of the previous view
		// might happen one frame too late
		static_assert(clearUnchangedSlots, "Partial update not supported for _retainedViews & _retainedSamplers");
		_retainedViews.clear();
		_retainedSamplers.clear();

		std::vector<DescriptorSetInitializer::BindTypeAndIdx> sortedBinds { bindsInit.begin(), bindsInit.end() };
		std::sort(
			sortedBinds.begin(), sortedBinds.end(),
			[](const auto& lhs, const auto& rhs) { 
				if (lhs._descriptorSetSlot < rhs._descriptorSetSlot) return true;
				if (lhs._descriptorSetSlot > rhs._descriptorSetSlot) return false;
				return lhs._descriptorSetArrayIdx < rhs._descriptorSetArrayIdx;
			});

		uint64_t writtenMask = 0ull;
		size_t linearBufferIterator = 0;
		unsigned offsetMultiple = std::max(1u, (unsigned)factory.GetPhysicalDeviceProperties().limits.minUniformBufferOffsetAlignment);
		ProgressiveDescriptorSetBuilder builder { _layout->GetDescriptorSlots(), 0 };
		for (auto b=sortedBinds.begin(); b!=sortedBinds.end();) {

			auto bStart = b;
			while (b != sortedBinds.end() && b->_descriptorSetSlot == bStart->_descriptorSetSlot) ++b;

			auto slotArrayCount = _layout->GetDescriptorSlots()[bStart->_descriptorSetSlot]._count;
			bool arraySlot = slotArrayCount != 1;

			if (!arraySlot) {

				assert(b == (bStart+1));		// if you hit this, we're attempting to bind multiple things to the same non-array slot
				assert(bStart->_descriptorSetArrayIdx == 0);

				if (bStart->_type == DescriptorSetInitializer::BindType::ResourceView) {
					assert(uniforms._resourceViews[bStart->_uniformsStreamIdx]);
					auto* view = checked_cast<const ResourceView*>(uniforms._resourceViews[bStart->_uniformsStreamIdx]);
					builder.Bind(bStart->_descriptorSetSlot, *view, {});
					writtenMask |= 1ull<<uint64_t(bStart->_descriptorSetSlot);
					if (!(flags & WriteFlags::DontRetainViews))
						_retainedViews.push_back(*view);
				} else if (bStart->_type == DescriptorSetInitializer::BindType::Sampler) {
					assert(uniforms._samplers[bStart->_uniformsStreamIdx]);
					auto* sampler = checked_cast<const SamplerState*>(uniforms._samplers[bStart->_uniformsStreamIdx]);
					builder.Bind(bStart->_descriptorSetSlot, sampler->GetUnderlying(), {}, {});
					writtenMask |= 1ull<<uint64_t(bStart->_descriptorSetSlot);
					if (!(flags & WriteFlags::DontRetainViews))
						_retainedSamplers.push_back(*sampler);
				} else if (bStart->_type == DescriptorSetInitializer::BindType::ImmediateData) {
					// Only constant buffers are supported for immediate data; partially for consistency
					// across APIs.
					// to support different descriptor types, we'd need to change the offset alignment
					// values and change the bind flag used to create the buffer
					assert(_layout->GetDescriptorSlots()[bStart->_descriptorSetSlot]._type == DescriptorType::UniformBuffer 
						|| _layout->GetDescriptorSlots()[bStart->_descriptorSetSlot]._type == DescriptorType::UniformBufferDynamicOffset);
					auto size = uniforms._immediateData[bStart->_uniformsStreamIdx].size();
					linearBufferIterator += CeilToMultiple(size, offsetMultiple);
					writtenMask |= 1ull<<uint64_t(bStart->_descriptorSetSlot);
				} else {
					UNREACHABLE();
				}

			} else {

				for (auto b2=bStart; b2!=(b-1); ++b2) {
					assert(b2->_type == (b2+1)->_type);
					assert(b2->_descriptorSetArrayIdx < (b2+1)->_descriptorSetArrayIdx);
				}

				if (bStart->_type == DescriptorSetInitializer::BindType::ResourceView) {

					VLA(const ResourceView*, arrayOfResources, slotArrayCount);
					for (unsigned c=0; c<slotArrayCount; ++c) arrayOfResources[c] = nullptr;

					assert(uniforms._resourceViews[bStart->_uniformsStreamIdx]);
					for (auto b2=bStart; b2!=b; ++b2) {
						arrayOfResources[b2->_descriptorSetArrayIdx] = checked_cast<const ResourceView*>(uniforms._resourceViews[b2->_uniformsStreamIdx]);
						if (!(flags & WriteFlags::DontRetainViews))
							_retainedViews.push_back(*arrayOfResources[b2->_descriptorSetArrayIdx]);
					}

					builder.BindArray(bStart->_descriptorSetSlot, MakeIteratorRange(arrayOfResources, &arrayOfResources[slotArrayCount]), {});
					writtenMask |= 1ull<<uint64_t(bStart->_descriptorSetSlot);

				} else {
					UNREACHABLE();		// only arrays of resource views are supported
				}

			}
		}

		if (linearBufferIterator != 0) {
			auto linearBufferSize = linearBufferIterator;
			linearBufferIterator = 0;
			std::vector<uint8_t> initData(linearBufferSize, 0);
			for (unsigned c=0; c<sortedBinds.size(); ++c)
				if (sortedBinds[c]._type == DescriptorSetInitializer::BindType::ImmediateData) {
					auto size = uniforms._immediateData[sortedBinds[c]._uniformsStreamIdx].size();
					auto range = MakeIteratorRange(initData.begin() + linearBufferIterator, initData.begin() + linearBufferIterator + size);
					std::memcpy(AsPointer(range.begin()), uniforms._immediateData[sortedBinds[c]._uniformsStreamIdx].begin(), size);
					linearBufferIterator += CeilToMultiple(size, offsetMultiple);
				}
			assert(linearBufferIterator == linearBufferSize);
			auto desc = CreateDesc(BindFlag::ConstantBuffer, LinearBufferDesc::Create(linearBufferSize));
			_associatedLinearBufferData = Resource(factory, desc, "descriptor-set-bound-data", MakeIteratorRange(initData).Cast<const void*>());

			linearBufferIterator = 0;
			for (unsigned c=0; c<sortedBinds.size(); ++c)
				if (sortedBinds[c]._type == DescriptorSetInitializer::BindType::ImmediateData) {
					auto size = uniforms._immediateData[sortedBinds[c]._uniformsStreamIdx].size();
					assert(size);
					builder.Bind(sortedBinds[c]._descriptorSetSlot, VkDescriptorBufferInfo{_associatedLinearBufferData.GetBuffer(), linearBufferIterator, size}, {}, "descriptor-set-bound-data");
					linearBufferIterator += CeilToMultiple(size, offsetMultiple);
				}
		}

		if (clearUnchangedSlots) {
			VLA(ProgressiveDescriptorSetBuilder::ResourceDims, resourceDims, _layout->GetDescriptorSlots().size());
			for (unsigned c=0; c<_layout->GetDescriptorSlots().size(); ++c) resourceDims[c] = ProgressiveDescriptorSetBuilder::ResourceDims::Unknown;
			builder.BindDummyDescriptors(*_globalPools, _layout->GetDummyMask() & ~writtenMask, MakeIteratorRange(resourceDims, &resourceDims[_layout->GetDescriptorSlots().size()]));
		}

		#if defined(VULKAN_VALIDATE_RESOURCE_VISIBILITY)
			// update resource visibility before we call FlushChanges()
			// default behaviour is to write to every slot (anything that is not explicitly filled is filled with
			// filled with dummies) -- so we can just clear out any previous visibility requirements
			if (clearUnchangedSlots) {
				_resourcesThatMustBeVisible.clear();
				_resourcesThatMustBeVisibleSlotAndCount.clear();
			} else {
				for (const auto& newAssignment:builder._pendingResourceVisibilityChangesSlotAndCount) {
					unsigned idx=0;
					for (auto c=_resourcesThatMustBeVisibleSlotAndCount.begin(); c!=_resourcesThatMustBeVisibleSlotAndCount.end(); ++c) {
						if (c->first == newAssignment.first) {
							_resourcesThatMustBeVisible.erase(_resourcesThatMustBeVisible.begin()+idx, _resourcesThatMustBeVisible.begin()+idx+c->second);
							_resourcesThatMustBeVisibleSlotAndCount.erase(c);
							break;
						} else
							idx += c->second;
					}
				}
			}

			// append new bindings
			_resourcesThatMustBeVisible.insert(_resourcesThatMustBeVisible.end(), builder._pendingResourceVisibilityChanges.begin(), builder._pendingResourceVisibilityChanges.end());
			_resourcesThatMustBeVisibleSlotAndCount.insert(_resourcesThatMustBeVisibleSlotAndCount.end(), builder._pendingResourceVisibilityChangesSlotAndCount.begin(), builder._pendingResourceVisibilityChangesSlotAndCount.end());

			// rebuild the sorted copy -- and re-sort and remove duplicates
			// this must be a second copy, because we may need to have to support overwriting some slots (but not all)
			// in future Write() operations
			_resourcesThatMustBeVisibleSorted.clear();
			_resourcesThatMustBeVisibleSorted.insert(_resourcesThatMustBeVisibleSorted.end(), _resourcesThatMustBeVisible.begin(), _resourcesThatMustBeVisible.end());
			std::sort(_resourcesThatMustBeVisibleSorted.begin(), _resourcesThatMustBeVisibleSorted.end());
			_resourcesThatMustBeVisibleSorted.erase(std::unique(_resourcesThatMustBeVisibleSorted.begin(), _resourcesThatMustBeVisibleSorted.end()), _resourcesThatMustBeVisibleSorted.end());
		#endif

		builder.FlushChanges(
			factory.GetDevice().get(),
			_underlying.get(),
			nullptr, 0
			VULKAN_VERBOSE_DEBUG_ONLY(, _description));
	}

	void CompiledDescriptorSet::Write(const DescriptorSetInitializer& newDescriptors, WriteFlags::BitField flags, IThreadContext* usageRestriction)
	{
		WriteInternal(GetObjectFactory(), newDescriptors._slotBindings, newDescriptors._bindItems, flags);

		_commandListRestriction = 0;
		if (flags & WriteFlags::RestrictToCommandList) {
			assert(usageRestriction);
			_commandListRestriction = DeviceContext::Get(*usageRestriction)->GetActiveCommandList().GetGUID();
		}
	}

	CompiledDescriptorSet::CompiledDescriptorSet(
		ObjectFactory& factory,
		GlobalPools& globalPools,
		const std::shared_ptr<CompiledDescriptorSetLayout>& layout,
		VkShaderStageFlags shaderStageFlags,
		StringSection<> name)
	: _layout(layout)
	, _globalPools(&globalPools)
	#if defined(_DEBUG)
		, _name(name.AsString())
	#endif
	, _commandListRestriction{0}
	{
		_underlying = globalPools._longTermDescriptorPool.Allocate(*_layout);

		#if defined(VULKAN_ENABLE_DEBUG_EXTENSIONS)
			if (factory.GetExtensionFunctions()._setObjectName && !name.IsEmpty()) {
				VLA(char, nameCopy, name.size()+1);
				XlCopyString(nameCopy, name.size()+1, name);		// copy to get a null terminator
				const VkDebugUtilsObjectNameInfoEXT descriptorSetNameInfo {
					VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT,
					NULL,
					VK_OBJECT_TYPE_DESCRIPTOR_SET,
					(uint64_t)_underlying.get(),
					nameCopy
				};
				factory.GetExtensionFunctions()._setObjectName(factory.GetDevice().get(), &descriptorSetNameInfo);
			}
		#endif
	}

	CompiledDescriptorSet::~CompiledDescriptorSet()
	{}

/////////////////////////////////////////////////////////////////////////////////////////////////////////

	VkDescriptorType_ AsVkDescriptorType(DescriptorType type)
	{
		//
		// Vulkan has a few less common descriptor types:
		//
		// VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER
		//			-- as the name suggests
		//
		// VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC
		// VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC
		//			-- like the non-dynamic versions, but there is an offset value specified
		//			   during the call to vkCmdBindDescriptorSets
		//			   presumably the typical use case is to bind a large host synchronized 
		// 			   dynamic buffer and update the offset for each draw call
		//
		// VK_DESCRIPTOR_TYPE_INLINE_UNIFORM_BLOCK_EXT
		// VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR
		// VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_NV
		//			-- extension features
		//

		switch (type) {
		case DescriptorType::Sampler:						return VK_DESCRIPTOR_TYPE_SAMPLER;
		case DescriptorType::SampledTexture:				return VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
		case DescriptorType::UniformBuffer:					return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		case DescriptorType::UnorderedAccessTexture:		return VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
		case DescriptorType::UnorderedAccessBuffer:			return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		case DescriptorType::UnorderedAccessTexelBuffer:	return VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER;
		case DescriptorType::UniformTexelBuffer:			return VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER;
		case DescriptorType::InputAttachment:				return VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
		case DescriptorType::UniformBufferDynamicOffset:			return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
		case DescriptorType::UnorderedAccessBufferDynamicOffset:	return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC;
		default:											return VK_DESCRIPTOR_TYPE_SAMPLER;
		}
	}
}}

