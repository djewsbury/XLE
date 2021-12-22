// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "PipelineLayout.h"
#include "DescriptorSet.h"
#include "ObjectFactory.h"
#include "Pools.h"
#include "IncludeVulkan.h"
#include "../../../OSServices/Log.h"
#include "../../../Utility/Threading/Mutex.h"
#include "../../../Utility/Streams/StreamFormatter.h"
#include "../../../Utility/MemoryUtils.h"
#include "../../../Utility/BitUtils.h"
#include "../../../xleres/FileList.h"
#include <iostream>

namespace RenderCore { namespace Metal_Vulkan
{
	static uint64_t s_nextCompiledPipelineLayoutGUID = 1;
	uint64_t CompiledPipelineLayout::GetGUID() const { return _guid; }
	PipelineLayoutInitializer CompiledPipelineLayout::GetInitializer() const { return _initializer; }

	CompiledPipelineLayout::CompiledPipelineLayout(
		ObjectFactory& factory,
		IteratorRange<const DescriptorSetBinding*> descriptorSets,
		IteratorRange<const PushConstantsBinding*> pushConstants,
		const PipelineLayoutInitializer& desc)
	: _guid(s_nextCompiledPipelineLayoutGUID++)
	, _initializer(desc)
	{
		for (auto&c:_descriptorSetBindingNames) c = 0;
		for (auto&c:_pushConstantBufferBindingNames) c = 0;

		_descriptorSetCount = std::min((unsigned)descriptorSets.size(), s_maxBoundDescriptorSetCount);
		_pushConstantBufferCount = std::min((unsigned)pushConstants.size(), s_maxPushConstantBuffers);
		VkDescriptorSetLayout rawDescriptorSetLayouts[s_maxBoundDescriptorSetCount];
		for (unsigned c=0; c<_descriptorSetCount; ++c) {
			_descriptorSetLayouts[c] = descriptorSets[c]._layout;
			rawDescriptorSetLayouts[c] = _descriptorSetLayouts[c]->GetUnderlying();
			_blankDescriptorSets[c] = descriptorSets[c]._blankDescriptorSet;
			_descriptorSetBindingNames[c] = Hash64(descriptorSets[c]._name);

			#if defined(VULKAN_VERBOSE_DEBUG)
				_blankDescriptorSetsDebugInfo[c] = descriptorSets[c]._blankDescriptorSetDebugInfo;
				_descriptorSetStringNames[c] = descriptorSets[c]._name;
			#endif
		}

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
			ds->_layout = std::make_unique<CompiledDescriptorSetLayout>(*_objectFactory, MakeIteratorRange(signature._slots), MakeIteratorRange(signature._fixedSamplers), stageFlags);
			
			{
				ProgressiveDescriptorSetBuilder builder { MakeIteratorRange(signature._slots), 0 };
				builder.BindDummyDescriptors(*_globalPools, ds->_layout->GetDummyMask());
				ds->_blankBindings = _globalPools->_longTermDescriptorPool.Allocate(ds->_layout->GetUnderlying());
				VULKAN_VERBOSE_DEBUG_ONLY(ds->_blankBindingsDescription._descriptorSetInfo = s_dummyDescriptorString);
				std::vector<uint64_t> resourceVisibilityList;
				builder.FlushChanges(
					_objectFactory->GetDevice().get(),
					ds->_blankBindings.get(),
					0, 0, resourceVisibilityList VULKAN_VERBOSE_DEBUG_ONLY(, ds->_blankBindingsDescription));
			}

			VULKAN_VERBOSE_DEBUG_ONLY(ds->_name = name);

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
					result._uniformBufferCount += b._count;
					break;

				case DescriptorType::UnorderedAccessBuffer:
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

		static void ValidatePipelineLayout(
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
