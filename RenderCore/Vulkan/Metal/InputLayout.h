// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "VulkanCore.h"
#include "../../UniformsStream.h"
#include "../../Types.h"		// for PipelineType
#include "../../../Utility/IteratorUtils.h"
#include <memory>
#include <vector>

namespace RenderCore
{
	class UniformsStream;
	class UniformsStreamInterface;
	class InputElementDesc;
	class MiniInputElementDesc;
	class CompiledShaderByteCode;
	class ICompiledPipelineLayout;
	class IResource;
	class IDescriptorSet;
	template <typename Type, int Count> class ResourceList;
	class ConstantBufferView;
}

namespace RenderCore { namespace Metal_Vulkan
{
	class DeviceContext;
	class ShaderProgram;
	class ComputeShader;
	class SPIRVReflection;

		////////////////////////////////////////////////////////////////////////////////////////////////

	class BoundInputLayout
	{
	public:
		bool AllAttributesBound() const { return _allAttributesBound; }

		BoundInputLayout(IteratorRange<const InputElementDesc*> layout, const CompiledShaderByteCode& shader);
		BoundInputLayout(IteratorRange<const InputElementDesc*> layout, const ShaderProgram& shader);
		
		struct SlotBinding
		{
			IteratorRange<const MiniInputElementDesc*> _elements;
			unsigned _instanceStepDataRate;     // set to 0 for per vertex, otherwise a per-instance rate
		};
		BoundInputLayout(
			IteratorRange<const SlotBinding*> layouts,
			const CompiledShaderByteCode& program);
		BoundInputLayout(
			IteratorRange<const SlotBinding*> layouts,
			const ShaderProgram& shader);

		BoundInputLayout();
		~BoundInputLayout();

		BoundInputLayout(BoundInputLayout&& moveFrom) never_throws = default;
		BoundInputLayout& operator=(BoundInputLayout&& moveFrom) never_throws = default;

		const IteratorRange<const VkVertexInputAttributeDescription*> GetAttributes() const { return MakeIteratorRange(_attributes); }
		const IteratorRange<const VkVertexInputBindingDescription*> GetVBBindings() const { return MakeIteratorRange(_vbBindingDescriptions); }
		uint64_t GetPipelineRelevantHash() const { return _pipelineRelevantHash; }
	private:
		std::vector<VkVertexInputAttributeDescription>	_attributes;
		std::vector<VkVertexInputBindingDescription>	_vbBindingDescriptions;
		uint64_t _pipelineRelevantHash;
		bool _allAttributesBound;

		void CalculateAllAttributesBound(const SPIRVReflection& reflection);
	};

		////////////////////////////////////////////////////////////////////////////////////////////////

	class SharedEncoder;
	class GraphicsPipeline;
	class ComputePipeline;
	class CompiledDescriptorSetLayout;
	class CompiledPipelineLayout;

	class BoundUniforms
	{
	public:
		void ApplyLooseUniforms(
			DeviceContext& context,
			SharedEncoder& encoder,
			const UniformsStream& group,
			unsigned groupIdx = 0) const;
		void UnbindLooseUniforms(DeviceContext& context, SharedEncoder& encoder, unsigned groupIdx=0) const;

		void ApplyDescriptorSets(
			DeviceContext& context,
			SharedEncoder& encoder,
			IteratorRange<const IDescriptorSet* const*> descriptorSets,
			unsigned groupIdx = 0) const;

		void ApplyDescriptorSet(
			DeviceContext& context,
			SharedEncoder& encoder,
			const IDescriptorSet& descriptorSet,
			unsigned groupIdx = 0, unsigned slotIdx = 0) const;

		uint64_t GetBoundLooseImmediateDatas(unsigned groupIdx = 0) const;
		uint64_t GetBoundLooseResources(unsigned groupIdx = 0) const;
		uint64_t GetBoundLooseSamplers(unsigned groupIdx = 0) const;
		uint64_t GetGroupRulesHash(unsigned groupIdx = 0) const;

		BoundUniforms(
			const ShaderProgram& shader,
			const UniformsStreamInterface& group0,
			const UniformsStreamInterface& group1 = {},
			const UniformsStreamInterface& group2 = {},
			const UniformsStreamInterface& group3 = {});
		BoundUniforms(
			const GraphicsPipeline& shader,
			const UniformsStreamInterface& group0,
			const UniformsStreamInterface& group1 = {},
			const UniformsStreamInterface& group2 = {},
			const UniformsStreamInterface& group3 = {});
		BoundUniforms(
			const ComputePipeline& shader,
			const UniformsStreamInterface& group0,
			const UniformsStreamInterface& group1 = {},
			const UniformsStreamInterface& group2 = {},
			const UniformsStreamInterface& group3 = {});
		BoundUniforms(
			const std::shared_ptr<ICompiledPipelineLayout>& pipelineLayout,
			const UniformsStreamInterface& group0,
			const UniformsStreamInterface& group1 = {},
			const UniformsStreamInterface& group2 = {},
			const UniformsStreamInterface& group3 = {});
		BoundUniforms();
		~BoundUniforms();
		BoundUniforms(const BoundUniforms&);
		BoundUniforms& operator=(const BoundUniforms&);
		BoundUniforms(BoundUniforms&&) never_throws;
		BoundUniforms& operator=(BoundUniforms&&) never_throws;

	private:
		struct AdaptiveSetBindingRules
		{
			uint32_t _descriptorSetIdx = 0u;
			uint32_t _shaderStageMask = 0u;
			std::shared_ptr<CompiledDescriptorSetLayout> _layout;

			std::vector<uint32_t> _resourceViewBinds;
			std::vector<uint32_t> _immediateDataBinds;
			std::vector<uint32_t> _samplerBinds;
			
			// these exist so we default out slots that are used by the shader, but not provided as input
			uint64_t _dummyMask = 0ull;
			unsigned _sharedBuilder = ~0u;

			#if defined(_DEBUG)
				std::vector<std::string> _resourceViewNames;
				std::vector<std::string> _immedateDataNames;
				std::vector<std::string> _samplerNames;
			#endif

			uint64_t CalculateHash(uint64_t) const;
		};

		struct PushConstantBindingRules
		{
			uint32_t	_shaderStageBind;
			unsigned	_offset, _size;
			unsigned	_inputCBSlot;
		};

		struct FixedDescriptorSetBindingRules
		{
			uint32_t _inputSlot;
			uint32_t _outputSlot;
			uint32_t _shaderStageMask;
		};

		struct GroupRules
		{
			std::vector<AdaptiveSetBindingRules> _adaptiveSetRules;
			std::vector<PushConstantBindingRules> _pushConstantsRules;
			std::vector<FixedDescriptorSetBindingRules> _fixedDescriptorSetRules;
			
			uint64_t _groupRulesHash = 0ull;
			uint64_t _boundLooseImmediateDatas = 0ull;
			uint64_t _boundLooseResources = 0ull;
			uint64_t _boundLooseSamplerStates = 0ull;
		};
		GroupRules _group[4];
		PipelineType _pipelineType;
		std::shared_ptr<CompiledPipelineLayout> _pipelineLayout;

		class SharedDescSetBuilder;
		mutable std::vector<SharedDescSetBuilder> _sharedDescSetBuilders;

		class ConstructionHelper;
		class BindingHelper;

		#if defined(_DEBUG)
			std::string _debuggingDescription;
		#endif
	};

	inline uint64_t BoundUniforms::GetBoundLooseImmediateDatas(unsigned groupIdx) const { assert(groupIdx < dimof(_group)); return _group[groupIdx]._boundLooseImmediateDatas; }
	inline uint64_t BoundUniforms::GetBoundLooseResources(unsigned groupIdx) const { assert(groupIdx < dimof(_group)); return _group[groupIdx]._boundLooseResources; }
	inline uint64_t BoundUniforms::GetBoundLooseSamplers(unsigned groupIdx) const { assert(groupIdx < dimof(_group)); return _group[groupIdx]._boundLooseSamplerStates; }
	inline uint64_t BoundUniforms::GetGroupRulesHash(unsigned groupIdx) const { assert(groupIdx < dimof(_group)); return _group[groupIdx]._groupRulesHash; }

		////////////////////////////////////////////////////////////////////////////////////////////////

	class SamplerState;
	class ResourceView;
	class ObjectFactory;
	class DescriptorSetDebugInfo;
	class CmdListAttachedStorage;

	/// <summary>Bind uniforms at numeric binding points</summary>
	class NumericUniformsInterface
	{
	public:
		template<int Count> void Bind(const ResourceList<IResourceView, Count>&);
		template<int Count> void Bind(const ResourceList<SamplerState, Count>&);
		template<int Count> void Bind(const ResourceList<IResource, Count>&);

		void    Bind(unsigned startingPoint, IteratorRange<const IResourceView*const*> resources);
		void    Bind(unsigned startingPoint, IteratorRange<const ConstantBufferView*> uniformBuffers);
		void	BindConstantBuffers(unsigned startingPoint, IteratorRange<const UniformsStream::ImmediateData*> uniformBuffers);

		void Reset();
		bool HasChanges() const;

		void Apply(
			DeviceContext& context,
			SharedEncoder& encoder) const;

		NumericUniformsInterface(
			const ObjectFactory& factory,
			const ICompiledPipelineLayout& pipelineLayout,
			CmdListAttachedStorage& cmdListAttachedStorage,
			const LegacyRegisterBindingDesc& bindings);
		NumericUniformsInterface();
		~NumericUniformsInterface();

		NumericUniformsInterface(NumericUniformsInterface&&);
		NumericUniformsInterface& operator=(NumericUniformsInterface&&);
	protected:
		class Pimpl;
		std::unique_ptr<Pimpl> _pimpl;

		void    Bind(unsigned startingPoint, IteratorRange<const VkSampler*> samplers);
	};

		////////////////////////////////////////////////////////////////////////////////////////////////

	template<int Count> 
		void    NumericUniformsInterface::Bind(const ResourceList<IResourceView, Count>& shaderResources) 
		{
			Bind(
				shaderResources._startingPoint,
				MakeIteratorRange(shaderResources._buffers));
		}
	
	template<int Count> void    NumericUniformsInterface::Bind(const ResourceList<SamplerState, Count>& samplerStates) 
		{
			VkSampler samplers[Count];
			for (unsigned c=0; c<Count; ++c)
				samplers[c] = samplerStates._buffers[c]->GetUnderlying();
			Bind(
				samplerStates._startingPoint,
				MakeIteratorRange(samplers));
		}
	
	template<int Count> 
		void    NumericUniformsInterface::Bind(const ResourceList<IResource, Count>& constantBuffers) 
		{
			ConstantBufferView buffers[Count];
			for (unsigned c=0; c<Count; ++c)
				buffers[c]._prebuiltBuffer = constantBuffers._buffers[c];
			Bind(
				constantBuffers._startingPoint,
				MakeIteratorRange(buffers));
		}
}}
