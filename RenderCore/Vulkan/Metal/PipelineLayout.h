// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "VulkanForward.h"
#include "VulkanCore.h"
#include "DescriptorSet.h"		// (for DescriptorSetDebugInfo)
#include "../../IDevice.h"
#include "../../Types.h"
#include "../../UniformsStream.h"
#include "../../../Assets/AssetsCore.h"
#include "../../../Assets/AssetUtils.h"
#include <memory>
#include <vector>
#include <string>
#include <unordered_map>

namespace RenderCore { class CompiledShaderByteCode; class ConstantBufferElementDesc; class DescriptorSetSignature; }

namespace RenderCore { namespace Metal_Vulkan
{
	class ObjectFactory;
	class DescriptorSetSignatureFile;
	class RootSignature;
	class PushConstantsRangeSignature;
	class ShaderProgram;
	class ComputeShader;
}}

namespace RenderCore { namespace Metal_Vulkan
{
	static const unsigned s_maxDescriptorSetCount = 4;
	static const unsigned s_maxPushConstantBuffers = 4;

	class CompiledPipelineLayout : public ICompiledPipelineLayout
	{
	public:
		using DescriptorSetIndex = unsigned;
		using DescriptorSetLayoutPtr = std::shared_ptr<CompiledDescriptorSetLayout>;
		using DescriptorSetPtr = VulkanSharedPtr<VkDescriptorSet>;
		
		VkPipelineLayout				GetUnderlying() const;
		const DescriptorSetLayoutPtr&	GetDescriptorSetLayout(DescriptorSetIndex) const;
		const DescriptorSetPtr&			GetBlankDescriptorSet(DescriptorSetIndex) const;
		unsigned 						GetDescriptorSetCount() const;
		uint64_t 						GetGUID() const override;
		PipelineLayoutInitializer 		GetInitializer() const override;

		IteratorRange<const uint64_t*> 	GetDescriptorSetBindingNames() const;
		IteratorRange<const uint64_t*> 	GetPushConstantsBindingNames() const;
		const VkPushConstantRange&		GetPushConstantsRange(unsigned idx) const;		

		#if defined(VULKAN_VERBOSE_DEBUG)
			const DescriptorSetDebugInfo& GetBlankDescriptorSetDebugInfo(DescriptorSetIndex) const;
			void WriteDebugInfo(
				std::ostream& output,
				IteratorRange<const CompiledShaderByteCode**> shaders,
				IteratorRange<const DescriptorSetDebugInfo*> descriptorSets);
		#endif
                                                                                                        
		struct DescriptorSetBinding
		{
			std::string _name;
			std::shared_ptr<CompiledDescriptorSetLayout> _layout;
			DescriptorSetPtr _blankDescriptorSet;

			#if defined(VULKAN_VERBOSE_DEBUG)
				DescriptorSetDebugInfo _blankDescriptorSetDebugInfo;
			#endif
		};

		struct PushConstantsBinding
		{
			std::string _name;
			unsigned _cbSize = 0;
			VkShaderStageFlags _stageFlags = 0;
			IteratorRange<const ConstantBufferElementDesc*> _cbElements;
		};

		CompiledPipelineLayout(
			ObjectFactory& factory,
			IteratorRange<const DescriptorSetBinding*> descriptorSets,
			IteratorRange<const PushConstantsBinding*> pushConstants,
			const PipelineLayoutInitializer& desc);

		CompiledPipelineLayout(const CompiledPipelineLayout&) = delete;
		CompiledPipelineLayout& operator=(const CompiledPipelineLayout&) = delete;
	private:
		VulkanUniquePtr<VkPipelineLayout> _pipelineLayout;

		DescriptorSetLayoutPtr	_descriptorSetLayouts[s_maxDescriptorSetCount];
		DescriptorSetPtr		_blankDescriptorSets[s_maxDescriptorSetCount];
		VkPushConstantRange		_pushConstantRanges[s_maxPushConstantBuffers];
		unsigned				_descriptorSetCount;
		unsigned 				_pushConstantBufferCount;

		uint64_t				_descriptorSetBindingNames[s_maxDescriptorSetCount];
		uint64_t				_pushConstantBufferBindingNames[s_maxPushConstantBuffers];

		uint64_t				_guid;
		PipelineLayoutInitializer	_initializer;

		#if defined(VULKAN_VERBOSE_DEBUG)
			DescriptorSetDebugInfo _blankDescriptorSetsDebugInfo[s_maxDescriptorSetCount];
			std::string _descriptorSetStringNames[s_maxDescriptorSetCount];
		#endif
	};

	inline VkPipelineLayout CompiledPipelineLayout::GetUnderlying() const { return _pipelineLayout.get(); }
		
	inline auto CompiledPipelineLayout::GetDescriptorSetLayout(DescriptorSetIndex binding) const -> const DescriptorSetLayoutPtr&
	{
		assert(binding < _descriptorSetCount);
		return _descriptorSetLayouts[binding];
	}

	inline auto CompiledPipelineLayout::GetBlankDescriptorSet(DescriptorSetIndex binding) const -> const DescriptorSetPtr&
	{
		assert(binding < _descriptorSetCount);
		return _blankDescriptorSets[binding];
	}

	inline auto CompiledPipelineLayout::GetDescriptorSetBindingNames() const -> IteratorRange<const uint64_t*>
	{
		return MakeIteratorRange(_descriptorSetBindingNames, _descriptorSetBindingNames + _descriptorSetCount);
	}

	inline auto CompiledPipelineLayout::GetPushConstantsBindingNames() const -> IteratorRange<const uint64_t*>
	{
		return MakeIteratorRange(_pushConstantBufferBindingNames, _pushConstantBufferBindingNames + _pushConstantBufferCount);
	}

	inline const VkPushConstantRange& CompiledPipelineLayout::GetPushConstantsRange(unsigned idx) const
	{
		assert(idx < _pushConstantBufferCount);
		return _pushConstantRanges[idx];
	}

	inline unsigned CompiledPipelineLayout::GetDescriptorSetCount() const
	{
		return _descriptorSetCount;
	}

	#if defined(VULKAN_VERBOSE_DEBUG)
		inline const DescriptorSetDebugInfo& CompiledPipelineLayout::GetBlankDescriptorSetDebugInfo(DescriptorSetIndex binding) const
		{
			assert(binding < _descriptorSetCount);
			return _blankDescriptorSetsDebugInfo[binding];
		}
	#endif

	namespace Internal
	{
		void ValidatePipelineLayout(
			VkPhysicalDevice physDev,
			const PipelineLayoutInitializer& pipelineLayout);

	///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

		struct DescriptorSetCacheResult
		{
			std::shared_ptr<CompiledDescriptorSetLayout> _layout;
			VulkanSharedPtr<VkDescriptorSet>		_blankBindings;
			
			#if defined(VULKAN_VERBOSE_DEBUG)
				DescriptorSetDebugInfo _blankBindingsDescription;
				std::string _name;
			#endif
		};
		
		class CompiledDescriptorSetLayoutCache
		{
		public:
			const DescriptorSetCacheResult*	CompileDescriptorSetLayout(
				const DescriptorSetSignature& signature,
				const std::string& name,
				VkShaderStageFlags stageFlags);

			CompiledDescriptorSetLayoutCache(ObjectFactory& objectFactory, GlobalPools& globalPools);
			~CompiledDescriptorSetLayoutCache();
		private:
			ObjectFactory*	_objectFactory;
			GlobalPools*	_globalPools;

			std::vector<std::pair<uint64_t, std::unique_ptr<DescriptorSetCacheResult>>> _cache;
		};
		std::shared_ptr<CompiledDescriptorSetLayoutCache> CreateCompiledDescriptorSetLayoutCache();

		class VulkanGlobalsTemp
		{
		public:
			GlobalPools* _globalPools;
			LegacyRegisterBindingDesc _legacyRegisterBindings;
			static VulkanGlobalsTemp& GetInstance();
		};
	}
}}

