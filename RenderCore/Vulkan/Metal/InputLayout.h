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

	class GraphicsEncoder;
	class GraphicsPipeline;
	class ComputePipeline;
	class CompiledDescriptorSetLayout;

	class BoundUniforms
	{
	public:
		void ApplyLooseUniforms(
			DeviceContext& context,
			GraphicsEncoder& encoder,
			const UniformsStream& stream) const;
		void UnbindLooseUniforms(DeviceContext& context, GraphicsEncoder& encoder);

		void ApplyDescriptorSets(
			DeviceContext& context,
			GraphicsEncoder& encoder,
			IteratorRange<const IDescriptorSet* const*> descriptorSets);

		uint64_t GetBoundLooseConstantBuffers() const { return _boundLooseUniformBuffers; }
		uint64_t GetBoundLooseResources() const { return _boundLooseResources; }
		uint64_t GetBoundLooseSamplers() const { return _boundLooseSamplerStates; }

		BoundUniforms(
			const ShaderProgram& shader,
			const UniformsStreamInterface& interf);
		BoundUniforms(
			const ComputeShader& shader,
			const UniformsStreamInterface& interf);
		BoundUniforms(
			const GraphicsPipeline& shader,
			const UniformsStreamInterface& interf);
		BoundUniforms(
			const ComputePipeline& shader,
			const UniformsStreamInterface& interf);
		BoundUniforms();
		~BoundUniforms();
		BoundUniforms(const BoundUniforms&) = default;
		BoundUniforms& operator=(const BoundUniforms&) = default;
		BoundUniforms(BoundUniforms&&) = default;
		BoundUniforms& operator=(BoundUniforms&&) = default;

	private:
		struct LooseUniformBind { uint32_t _descSetSlot; uint32_t _inputUniformStreamIdx; };
		struct AdaptiveSetBindingRules
		{
			uint32_t _descriptorSetIdx = 0u;
			uint32_t _shaderStageMask = 0u;
			std::shared_ptr<CompiledDescriptorSetLayout> _layout;

			std::vector<LooseUniformBind> _resourceViewBinds;
			std::vector<LooseUniformBind> _immediateDataBinds;
			std::vector<LooseUniformBind> _samplerBinds;
			
			// these exist so we default out slots that are used by the shader, but not provided as input
			std::vector<DescriptorSlot> _sig;
			uint64_t _shaderUsageMask = 0ull;
		};
		std::vector<AdaptiveSetBindingRules> _adaptiveSetRules;

		struct PushConstantBindingRules
		{
			uint32_t	_shaderStageBind;
			unsigned	_offset, _size;
			unsigned	_inputCBSlot;
		};
		std::vector<PushConstantBindingRules> _pushConstantsRules;

		struct FixedDescriptorSetBindingRules
		{
			uint32_t _inputSlot;
			uint32_t _outputSlot;
			uint32_t _shaderStageMask;
		};
		std::vector<FixedDescriptorSetBindingRules> _fixedDescriptorSetRules;
		
		PipelineType _pipelineType;
		uint64_t _boundLooseUniformBuffers;
		uint64_t _boundLooseResources;
		uint64_t _boundLooseSamplerStates;

		class ConstructionHelper;
		class BindingHelper;

		#if defined(_DEBUG)
			std::string _debuggingDescription;
		#endif
	};

		////////////////////////////////////////////////////////////////////////////////////////////////

	class SamplerState;
	class ResourceView;
	class ObjectFactory;
	class DescriptorSetDebugInfo;

	/// <summary>Bind uniforms at numeric binding points</summary>
	class NumericUniformsInterface
	{
	public:
		template<int Count> void Bind(const ResourceList<IResourceView, Count>&);
		template<int Count> void Bind(const ResourceList<SamplerState, Count>&);
		template<int Count> void Bind(const ResourceList<IResource, Count>&);

		void    Bind(unsigned startingPoint, IteratorRange<const IResourceView*const*> resources);
		void    Bind(unsigned startingPoint, IteratorRange<const ConstantBufferView*> uniformBuffers);

		void Reset();
		bool HasChanges() const;

		void Apply(
			DeviceContext& context,
			GraphicsEncoder& encoder) const;

		NumericUniformsInterface(
			const ObjectFactory& factory,
			const ICompiledPipelineLayout& pipelineLayout,
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
