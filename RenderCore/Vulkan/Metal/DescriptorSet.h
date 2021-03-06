// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "VulkanCore.h"
#include "../../IDevice.h"
#include "../../UniformsStream.h"
#include "../../../Utility/StringUtils.h"
#include "../../../Utility/IteratorUtils.h"

#include "IncludeVulkan.h"
#include <iosfwd>
#include <string>
#include <vector>

namespace RenderCore { class CompiledShaderByteCode; class UniformsStream; enum class PipelineType; class ConstantBufferView; struct DescriptorSlot; class LegacyRegisterBindingDesc; class IResource; }

namespace RenderCore { namespace Metal_Vulkan
{
	class ResourceView;
	class GlobalPools;
	class ObjectFactory;

	#if defined(VULKAN_VERBOSE_DEBUG)
		class DescriptorSetDebugInfo
		{
		public:
			struct BindingDescription
			{
				VkDescriptorType_ _descriptorType = (VkDescriptorType_)~0u;
				std::string _description;
			};
			std::vector<BindingDescription> _bindingDescriptions;
			std::string _descriptorSetInfo;
		};		
	#endif

	class ProgressiveDescriptorSetBuilder
    {
    public:
		void    Bind(unsigned descriptorSetBindPoint, const ResourceView& resource);
		void    Bind(unsigned descriptorSetBindPoint, VkDescriptorBufferInfo uniformBuffer, StringSection<> description = {});
		void    Bind(unsigned descriptorSetBindPoint, VkSampler sampler, StringSection<> description = {});

		uint64_t	BindDummyDescriptors(GlobalPools& globalPools, uint64_t dummyDescWriteMask);

		bool		HasChanges() const;
		void		Reset();
		uint64_t	PendingWriteMask() const { return _pendingWrites; }

		uint64_t	FlushChanges(
			VkDevice device,
			VkDescriptorSet destination,
			VkDescriptorSet copyPrevDescriptors, uint64_t prevDescriptorMask,
			std::vector<uint64_t>& resourceVisibilityList
			VULKAN_VERBOSE_DEBUG_ONLY(, DescriptorSetDebugInfo& description));

		ProgressiveDescriptorSetBuilder(
			IteratorRange<const DescriptorSlot*> signature);
		~ProgressiveDescriptorSetBuilder();
		ProgressiveDescriptorSetBuilder(ProgressiveDescriptorSetBuilder&&);
		ProgressiveDescriptorSetBuilder& operator=(ProgressiveDescriptorSetBuilder&&);

	private:
        static const unsigned s_pendingBufferLength = 32;

        VkDescriptorBufferInfo  _bufferInfo[s_pendingBufferLength];
        VkDescriptorImageInfo   _imageInfo[s_pendingBufferLength];
        VkWriteDescriptorSet    _writes[s_pendingBufferLength];

        unsigned	_pendingWrites = 0;
        unsigned	_pendingImageInfos = 0;
        unsigned	_pendingBufferInfos = 0;

        uint64_t	_sinceLastFlush = 0;
		std::vector<DescriptorSlot> _signature;

		#if defined(VULKAN_VERBOSE_DEBUG)
			DescriptorSetDebugInfo _verboseDescription;
		#endif

		#if defined(VULKAN_VALIDATE_RESOURCE_VISIBILITY)
			std::vector<uint64_t> _resourcesThatMustBeVisible;
			void ValidateResourceVisibility(IResource& res);
		#endif

		template<typename BindingInfo> 
			void WriteBinding(
				unsigned bindingPoint, VkDescriptorType_ type, const BindingInfo& bindingInfo, bool reallocateBufferInfo
				VULKAN_VERBOSE_DEBUG_ONLY(, const std::string& description));
		template<typename BindingInfo>
			BindingInfo& AllocateInfo(const BindingInfo& init);
	};

/////////////////////////////////////////////////////////////////////////////////////////////////////////

	#if defined(VULKAN_VERBOSE_DEBUG)
		class DescriptorSetDebugInfo;
		std::ostream& WriteDescriptorSet(
			std::ostream&& stream,
			const DescriptorSetDebugInfo& bindingDescription,
			IteratorRange<const DescriptorSlot*> signature,
			const std::string& descriptorSetName,
			const LegacyRegisterBindingDesc& legacyRegisterBinding,
			IteratorRange<const CompiledShaderByteCode**> compiledShaderByteCode,
			unsigned descriptorSetIndex, bool isBound);
	#endif

	class CompiledDescriptorSetLayout
	{
	public:
		VkDescriptorSetLayout GetUnderlying() { return _layout.get(); }
		IteratorRange<const DescriptorSlot*> GetDescriptorSlots() const { return MakeIteratorRange(_descriptorSlots); }
		VkShaderStageFlags GetShaderStageFlags() { return _shaderStageFlags; }

		CompiledDescriptorSetLayout(
			const ObjectFactory& factory, 
			IteratorRange<const DescriptorSlot*> srcLayout,
			VkShaderStageFlags stageFlags);
		~CompiledDescriptorSetLayout();
		CompiledDescriptorSetLayout(CompiledDescriptorSetLayout&&) never_throws = default;
		CompiledDescriptorSetLayout& operator=(CompiledDescriptorSetLayout&&) never_throws = default;
	private:
		VulkanUniquePtr<VkDescriptorSetLayout>	_layout;
		std::vector<DescriptorSlot> _descriptorSlots;
		VkShaderStageFlags _shaderStageFlags;
	};

	class CompiledDescriptorSet : public IDescriptorSet
	{
	public:
		VkDescriptorSet GetUnderlying() const { return _underlying.get(); }
		VkDescriptorSetLayout GetUnderlyingLayout() const { return _layout->GetUnderlying(); }

		#if defined(VULKAN_VERBOSE_DEBUG)
			const DescriptorSetDebugInfo& GetDescription() const { return _description; }
		#endif

		CompiledDescriptorSet(
			ObjectFactory& factory,
			GlobalPools& globalPools,
			const std::shared_ptr<CompiledDescriptorSetLayout>& layout,
			VkShaderStageFlags stageFlags,
			IteratorRange<const DescriptorSetInitializer::BindTypeAndIdx*> binds,
			const UniformsStream& uniforms);
		~CompiledDescriptorSet();
	private:
		VulkanUniquePtr<VkDescriptorSet> _underlying;
		std::shared_ptr<CompiledDescriptorSetLayout> _layout;
		#if defined(VULKAN_VERBOSE_DEBUG)
			DescriptorSetDebugInfo _description;
		#endif
	};

}}
