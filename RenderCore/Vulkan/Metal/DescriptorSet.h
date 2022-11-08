// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "VulkanCore.h"
#include "Resource.h"
#include "../../IDevice.h"
#include "../../UniformsStream.h"
#include "../../../Utility/StringUtils.h"
#include "../../../Utility/IteratorUtils.h"

#include "IncludeVulkan.h"
#include <iosfwd>
#include <string>
#include <vector>

namespace RenderCore { class CompiledShaderByteCode; class UniformsStream; enum class PipelineType; struct DescriptorSlot; class LegacyRegisterBindingDesc; class IResource; }

namespace RenderCore { namespace Metal_Vulkan
{
	class ResourceView;
	class GlobalPools;
	class ObjectFactory;
	class SamplerState;

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
		void    Bind(unsigned descriptorSetBindPoint, const ResourceView& resource, StringSection<> shaderOrDescSetVariable={});
		void    Bind(unsigned descriptorSetBindPoint, VkDescriptorBufferInfo uniformBuffer, StringSection<> shaderOrDescSetVariable={}, StringSection<> bufferDescription={});
		void    Bind(unsigned descriptorSetBindPoint, VkSampler sampler, StringSection<> shaderOrDescSetVariable={}, StringSection<> samplerDescription={});

		void    BindArray(unsigned descriptorSetBindPoint, IteratorRange<const ResourceView*const*> resources, StringSection<> shaderOrDescSetVariable={});

		enum class ResourceDims : uint32_t
		{
			Dim1D, Dim1DArray, Dim2D, Dim2DArray, Dim3D,
			Dim2DMS, Dim2DMSArray,
			DimCube, DimCubeArray,
			DimBuffer,
			DimInputAttachment,
			Unknown
		};
		uint64_t	BindDummyDescriptors(
			GlobalPools& globalPools, uint64_t dummyDescWriteMask,
			IteratorRange<const ResourceDims*> shaderTypesExpected);		// the descriptor set layout itself doesn't care about specific texture );

		bool		HasChanges() const;
		void		Reset();

		uint64_t	FlushChanges(
			VkDevice device,
			VkDescriptorSet destination,
			VkDescriptorSet copyPrevDescriptors, uint64_t prevDescriptorMask
			VULKAN_VERBOSE_DEBUG_ONLY(, DescriptorSetDebugInfo& description));

		#if defined(VULKAN_VALIDATE_RESOURCE_VISIBILITY)
			std::vector<uint64_t> _pendingResourceVisibilityChanges;
			std::vector<std::pair<unsigned, unsigned>> _pendingResourceVisibilityChangesSlotAndCount;
		#endif

		struct Flags
		{
			enum Enum {};
			using BitField = unsigned;
		};

		ProgressiveDescriptorSetBuilder(
			IteratorRange<const DescriptorSlot*> signature,
			Flags::BitField flags = 0);
		~ProgressiveDescriptorSetBuilder();
		ProgressiveDescriptorSetBuilder(ProgressiveDescriptorSetBuilder&&);
		ProgressiveDescriptorSetBuilder& operator=(ProgressiveDescriptorSetBuilder&&);

	private:
        static const unsigned s_pendingBufferLength = 32;

        VkDescriptorBufferInfo  _bufferInfo[s_pendingBufferLength];
        VkDescriptorImageInfo   _imageInfo[s_pendingBufferLength];
		VkBufferView			_bufferViews[s_pendingBufferLength];
        VkWriteDescriptorSet    _writes[s_pendingBufferLength];

        unsigned	_pendingWrites = 0;
        unsigned	_pendingImageInfos = 0;
        unsigned	_pendingBufferInfos = 0;
		unsigned	_pendingBufferViews = 0;

        uint64_t	_sinceLastFlush = 0;
		IteratorRange<const DescriptorSlot*> _signature;		// avoid copying this because ProgressiveDescriptorSetBuilder is mostly for short-term use

		Flags::BitField _flags = 0;

		#if defined(VULKAN_VERBOSE_DEBUG)
			DescriptorSetDebugInfo _verboseDescription;
		#endif

		template<typename BindingInfo> 
			void WriteBinding(
				unsigned bindingPoint, VkDescriptorType_ type, const BindingInfo& bindingInfo, bool reallocateBufferInfo
				VULKAN_VERBOSE_DEBUG_ONLY(, const std::string& description));
		template<typename BindingInfo> 
			void WriteArrayBinding(
				unsigned bindingPoint, VkDescriptorType_ type, IteratorRange<const BindingInfo*> bindingInfo
				VULKAN_VERBOSE_DEBUG_ONLY(, const std::string& description));
		template<typename BindingInfo>
			BindingInfo& AllocateInfo(const BindingInfo& init);
		template<typename BindingInfo>
			BindingInfo* AllocateInfos(unsigned count);
		VkDescriptorImageInfo* AllocateBlankImageInfos(GlobalPools&, ResourceDims, unsigned count);
		VkDescriptorImageInfo* AllocateBlankUavImageInfos(GlobalPools&, ResourceDims, unsigned count);
	};

/////////////////////////////////////////////////////////////////////////////////////////////////////////

	#if defined(VULKAN_VERBOSE_DEBUG)
		class DescriptorSetDebugInfo;
		std::ostream& WriteDescriptorSet(
			std::ostream& stream,
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
		VkShaderStageFlags GetVkShaderStageMask() const { return _vkShaderStageMask; }
		uint64_t GetDummyMask() const { return _dummyMask; }
		bool IsFixedSampler(unsigned slotIdx);

		CompiledDescriptorSetLayout(
			const ObjectFactory& factory, 
			IteratorRange<const DescriptorSlot*> srcLayout,
			IteratorRange<const  std::shared_ptr<ISampler>*> fixedSamplers,
			VkShaderStageFlags stageFlags);
		~CompiledDescriptorSetLayout();
		CompiledDescriptorSetLayout(CompiledDescriptorSetLayout&&) never_throws = default;
		CompiledDescriptorSetLayout& operator=(CompiledDescriptorSetLayout&&) never_throws = default;
	private:
		VulkanUniquePtr<VkDescriptorSetLayout>	_layout;
		std::vector<DescriptorSlot> _descriptorSlots;
		std::vector<std::shared_ptr<ISampler>> _fixedSamplers;
		VkShaderStageFlags _vkShaderStageMask;
		uint64_t _dummyMask = 0;
	};

	class CompiledDescriptorSet : public IDescriptorSet
	{
	public:
		VkDescriptorSet GetUnderlying() const { return _underlying.get(); }
		VkDescriptorSetLayout GetUnderlyingLayout() const { return _layout->GetUnderlying(); }
		void Write(const DescriptorSetInitializer& newDescriptors);

		#if defined(VULKAN_VERBOSE_DEBUG)
			const DescriptorSetDebugInfo& GetDescription() const { return _description; }
		#endif

		#if defined(VULKAN_VALIDATE_RESOURCE_VISIBILITY)
			IteratorRange<const uint64_t*> GetResourcesThatMustBeVisibleSorted() const { return _resourcesThatMustBeVisibleSorted; }
		#endif

		const CompiledDescriptorSetLayout& GetLayout() const { return *_layout; }

		CompiledDescriptorSet(
			ObjectFactory& factory,
			GlobalPools& globalPools,
			const std::shared_ptr<CompiledDescriptorSetLayout>& layout,
			VkShaderStageFlags stageFlags);
		~CompiledDescriptorSet();
	private:
		VulkanUniquePtr<VkDescriptorSet> _underlying;
		std::shared_ptr<CompiledDescriptorSetLayout> _layout;
		Resource _associatedLinearBufferData;
		#if defined(VULKAN_VERBOSE_DEBUG)
			DescriptorSetDebugInfo _description;
		#endif

		std::vector<ResourceView> _retainedViews;
		std::vector<SamplerState> _retainedSamplers;
		GlobalPools* _globalPools;

		#if defined(VULKAN_VALIDATE_RESOURCE_VISIBILITY)
			std::vector<uint64_t> _resourcesThatMustBeVisible;
			std::vector<std::pair<unsigned, unsigned>> _resourcesThatMustBeVisibleSlotAndCount;
			std::vector<uint64_t> _resourcesThatMustBeVisibleSorted;
		#endif

		void WriteInternal(
			ObjectFactory& factory,
			IteratorRange<const DescriptorSetInitializer::BindTypeAndIdx*> binds,
			const UniformsStream& uniforms);
	};

}}
