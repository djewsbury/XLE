// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "PipelineLayout.h"
#include "DescriptorSet.h"
#include "ObjectFactory.h"
#include "Pools.h"
#include "ExtensionFunctions.h"
#include "IncludeVulkan.h"
#include "../../../Formatters/TextFormatter.h"
#include "../../../Utility/Threading/Mutex.h"
#include "../../../Utility/MemoryUtils.h"
#include "../../../Utility/BitUtils.h"

namespace RenderCore { namespace Metal_Vulkan
{
	static uint64_t s_nextCompiledPipelineLayoutGUID = 1;
	uint64_t CompiledPipelineLayout::GetGUID() const { return _guid; }
	PipelineLayoutInitializer CompiledPipelineLayout::GetInitializer() const { return _initializer; }

	static unsigned CalculateDynamicOffsetCount(IteratorRange<const DescriptorSlot*> signature)
	{
		unsigned result = 0;
		for (const auto& s:signature)
			if (s._type == DescriptorType::UniformBufferDynamicOffset || s._type == DescriptorType::UnorderedAccessBufferDynamicOffset)
				++result;
		return result;
	}

	CompiledPipelineLayout::CompiledPipelineLayout(
		ObjectFactory& factory,
		IteratorRange<const DescriptorSetBinding*> descriptorSets,
		IteratorRange<const PushConstantsBinding*> pushConstants,
		const PipelineLayoutInitializer& desc,
		StringSection<> name)
	: _guid(s_nextCompiledPipelineLayoutGUID++)
	, _initializer(desc)
	#if defined(_DEBUG)
		, _name(name.AsString())
	#endif
	{
		for (auto&c:_descriptorSetBindingNames) c = 0;
		for (auto&c:_pushConstantBufferBindingNames) c = 0;

		_descriptorSetCount = std::min((unsigned)descriptorSets.size(), s_maxBoundDescriptorSetCount);
		_pushConstantBufferCount = std::min((unsigned)pushConstants.size(), s_maxPushConstantBuffers);
		VkDescriptorSetLayout rawDescriptorSetLayouts[s_maxBoundDescriptorSetCount];
		unsigned maxDynamicOffsetsCount = 0;
		for (unsigned c=0; c<_descriptorSetCount; ++c) {
			_descriptorSetLayouts[c] = descriptorSets[c]._layout;
			if (_descriptorSetLayouts[c]) {
				rawDescriptorSetLayouts[c] = _descriptorSetLayouts[c]->GetUnderlying();
				_dynamicOffsetsCount[c] = CalculateDynamicOffsetCount(_descriptorSetLayouts[c]->GetDescriptorSlots());
			} else {
				rawDescriptorSetLayouts[c] = nullptr;
				_dynamicOffsetsCount[c] = 0;
			}
			_blankDescriptorSets[c] = descriptorSets[c]._blankDescriptorSet;
			_descriptorSetBindingNames[c] = Hash64(descriptorSets[c]._name);

			#if defined(VULKAN_VERBOSE_DEBUG)
				_blankDescriptorSetsDebugInfo[c] = descriptorSets[c]._blankDescriptorSetDebugInfo;
				_descriptorSetStringNames[c] = descriptorSets[c]._name;
			#endif

			maxDynamicOffsetsCount = std::max(maxDynamicOffsetsCount, _dynamicOffsetsCount[c]);
		}

		_dynamicOffsetsBuffer.resize(maxDynamicOffsetsCount, 0);

		// Vulkan is particular about how push constants work!
		// Each range is bound to specific shader stages; but you can't overlap ranges,
		// even if those ranges apply to different shader stages. Well, technically we
		// can here, in the layout. But when we come to call vkCmdPushConstants, we'll get
		// a validation error -- (when pushing constants to a particular range, we must set
		// the shader stages for all ranges that overlap the bytes pushed).
		// So if we have push constants used by different shaders in a shader program (ie, vertex & fragment shaders),
		// they must actually agree about the position of specific uniforms. You can't have different
		// shaders using the same byte offset for different uniforms. The most practical way
		// to deal with this would might be to only use push constants in a specific shader (ie, only in vertex
		// shaders, never in fragment shaders).
		unsigned pushConstantIterator = 0;
		for (unsigned c=0; c<_pushConstantBufferCount; ++c) {
			assert(pushConstants[c]._cbSize != 0);
			assert(pushConstants[c]._stageFlags != 0);
			auto size = CeilToMultiplePow2(pushConstants[c]._cbSize, 4);
			
			auto startOffset = pushConstantIterator;
			pushConstantIterator += size;

			assert(startOffset == CeilToMultiplePow2(startOffset, 4));
			_pushConstantRanges[c] = VkPushConstantRange { pushConstants[c]._stageFlags, startOffset, size };
			_pushConstantBufferBindingNames[c] = Hash64(pushConstants[c]._name);
		}

		_pipelineLayout = factory.CreatePipelineLayout(
			MakeIteratorRange(rawDescriptorSetLayouts, &rawDescriptorSetLayouts[_descriptorSetCount]),
			MakeIteratorRange(_pushConstantRanges, &_pushConstantRanges[_pushConstantBufferCount]));

		// When we switch from one pipeline layout to another, we retain descriptor set bindings upto the point
		// where the descriptor sets layouts are not perfectly identical. Ie, internally the driver is rolling
		// the separate descriptor sets into a single long array, but perhaps the amount of space in that array
		// varies between descriptor types. Therefore the position of a descriptor in a particular set depends
		// on the descriptor sets that came before.
		_sequentialDescSetHashes[0] = _descriptorSetLayouts[0] ? _descriptorSetLayouts[0]->GetHashCode() : DefaultSeed64;
		for (unsigned c=1; c<s_maxBoundDescriptorSetCount; ++c) {
			if (_descriptorSetLayouts[c]) _sequentialDescSetHashes[c] = HashCombine(_descriptorSetLayouts[c]->GetHashCode(), _sequentialDescSetHashes[c-1]);
			else _sequentialDescSetHashes[c] = _sequentialDescSetHashes[c-1];
		}

		#if defined(_DEBUG)
			// generate a validation buffer for push constant ranges
			// we're a little more permissive with range overlaps here than Vulkan may actually be...
			if (_pushConstantBufferCount) {
				std::vector<VkPushConstantRange> sortedPushConstants { _pushConstantRanges, &_pushConstantRanges[_pushConstantBufferCount] };
				std::sort(sortedPushConstants.begin(), sortedPushConstants.end(),
					[](const auto& lhs, const auto& rhs) { return lhs.offset < rhs.offset; });
				_pushConstantsRangeValidation.reserve(sortedPushConstants.size()+1);
				unsigned end = 0;
				for (auto i=sortedPushConstants.begin(); i!=sortedPushConstants.end(); ++i) {
					auto stageFlags = i->stageFlags;
					end = i->offset+i->size;
					if ((i+1) != sortedPushConstants.end()) end = std::min(end, (i+1)->offset);
					// check every range for an overlap and collate the stage flags
					for (auto i2=sortedPushConstants.begin(); i2!=sortedPushConstants.end(); ++i2)
						if (i2->offset < end && (i2->offset + i2->size) > i->offset)
							stageFlags |= i2->stageFlags;
					_pushConstantsRangeValidation.emplace_back(i->offset, stageFlags);
				}
				assert(end != 0);
				_pushConstantsRangeValidation.emplace_back(end, 0);
			}
		#endif

		#if defined(VULKAN_ENABLE_DEBUG_EXTENSIONS)
			if (factory.GetExtensionFunctions()._setObjectName && !name.IsEmpty()) {
				VLA(char, nameCopy, name.size()+1);
				XlCopyString(nameCopy, name.size()+1, name);		// copy to get a null terminator
				const VkDebugUtilsObjectNameInfoEXT pipelineLayoutNameInfo {
					VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT,
					NULL,
					VK_OBJECT_TYPE_PIPELINE_LAYOUT,
					(uint64_t)_pipelineLayout.get(),
					nameCopy
				};
				factory.GetExtensionFunctions()._setObjectName(factory.GetDevice().get(), &pipelineLayoutNameInfo);
			}
		#endif
	}

	#if defined(VULKAN_VERBOSE_DEBUG)
		void CompiledPipelineLayout::WriteDebugInfo(
			std::ostream& output,
			IteratorRange<const CompiledShaderByteCode**> shaders,
			IteratorRange<const DescriptorSetDebugInfo*> descriptorSets)
		{
			Log(Verbose) << "-------------Descriptors------------" << std::endl;
			for (unsigned descSetIdx=0; descSetIdx<_descriptorSetCount; ++descSetIdx) {
				WriteDescriptorSet(
					output,
					(descSetIdx < descriptorSets.size()) ? descriptorSets[descSetIdx] : _blankDescriptorSetsDebugInfo[descSetIdx],
					(descSetIdx < _descriptorSetCount) ? _descriptorSetLayouts[descSetIdx]->GetDescriptorSlots() : IteratorRange<const DescriptorSlot*>{},
					(descSetIdx < _descriptorSetCount) ? _descriptorSetStringNames[descSetIdx] : "<<unbound>>",
					Internal::VulkanGlobalsTemp::GetInstance()._legacyRegisterBindings,
					shaders,
					descSetIdx,
					descSetIdx < descriptorSets.size());
			}
		}
	#endif

	namespace Internal
	{

	///////////////////////////////////////////////////////////////////////////////////////////////////

		class DescSetLimits
		{
		public:
			unsigned _sampledImageCount;
			unsigned _samplerCount;
			unsigned _uniformBufferCount;
			unsigned _storageBufferCount;
			unsigned _storageImageCount;
			unsigned _inputAttachmentCount;

			void Add(const DescSetLimits& other)
			{
				_sampledImageCount += other._sampledImageCount;
				_samplerCount += other._samplerCount;
				_uniformBufferCount += other._uniformBufferCount;
				_storageBufferCount += other._storageBufferCount;
				_storageImageCount += other._storageImageCount;
				_inputAttachmentCount += other._inputAttachmentCount;
			}
		};

		static DescSetLimits BuildLimits(const DescriptorSetSignature& setSig)
		{
			DescSetLimits result = {};
			for (auto& b:setSig._slots) {
				switch (b._type) {
				case DescriptorType::Sampler:
					result._samplerCount += b._count;
					break;

				case DescriptorType::SampledTexture:
				case DescriptorType::UniformTexelBuffer:
					result._sampledImageCount += b._count;
					break;

				case DescriptorType::UniformBuffer:
				case DescriptorType::UniformBufferDynamicOffset:
					result._uniformBufferCount += b._count;
					break;

				case DescriptorType::UnorderedAccessBuffer:
				case DescriptorType::UnorderedAccessBufferDynamicOffset:
					result._storageBufferCount += b._count;
					break;

				case DescriptorType::UnorderedAccessTexture:
				case DescriptorType::UnorderedAccessTexelBuffer:
					result._storageImageCount += b._count;
					break;

				default:
					break;
				}
			}
			return result;
		}

		void ValidatePipelineLayout(
			VkPhysicalDevice physDev,
			const PipelineLayoutInitializer& pipelineLayout)
		{
			// Validate the root signature against the physical device, and throw an exception
			// if there are problems.
			// Things to check:
			//      VkPhysicalDeviceLimits.maxBoundDescriptorSets
			//      VkPhysicalDeviceLimits.maxPerStageDescriptor*
			//      VkPhysicalDeviceLimits.maxDescriptorSet*

			VkPhysicalDeviceProperties props;
			vkGetPhysicalDeviceProperties(physDev, &props);
			const auto& limits = props.limits;

			// Here, we are assuming all descriptors apply equally to all stages.
			DescSetLimits totalLimits = {};
			for (const auto& s:pipelineLayout.GetDescriptorSets()) {
				auto ds = BuildLimits(s._signature);
				// not really clear how these ones work...?
				if (    ds._sampledImageCount > limits.maxDescriptorSetSampledImages
					||  ds._samplerCount > limits.maxPerStageDescriptorSamplers
					||  ds._uniformBufferCount > limits.maxPerStageDescriptorUniformBuffers
					||  ds._storageBufferCount > limits.maxPerStageDescriptorStorageBuffers
					||  ds._storageImageCount > limits.maxPerStageDescriptorStorageImages
					||  ds._inputAttachmentCount > limits.maxPerStageDescriptorInputAttachments)
					Throw(::Exceptions::BasicLabel("Root signature exceeds the maximum number of bound resources in a single descriptor set that is supported by the device"));
				totalLimits.Add(ds);
			}

			if (    totalLimits._sampledImageCount > limits.maxDescriptorSetSampledImages
				||  totalLimits._samplerCount > limits.maxPerStageDescriptorSamplers
				||  totalLimits._uniformBufferCount > limits.maxPerStageDescriptorUniformBuffers
				||  totalLimits._storageBufferCount > limits.maxPerStageDescriptorStorageBuffers
				||  totalLimits._storageImageCount > limits.maxPerStageDescriptorStorageImages
				||  totalLimits._inputAttachmentCount > limits.maxPerStageDescriptorInputAttachments)
				Throw(::Exceptions::BasicLabel("Root signature exceeds the maximum number of bound resources per stage that is supported by the device"));
		}

		VulkanGlobalsTemp& VulkanGlobalsTemp::GetInstance()
		{
			static VulkanGlobalsTemp s_instance;
			return s_instance;
		}
	}
	
}}
