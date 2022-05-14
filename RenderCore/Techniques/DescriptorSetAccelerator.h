// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../UniformsStream.h"
#include "../Types.h"
#include <vector>
#include <memory>
#include <future>

namespace RenderCore { namespace Assets { class PredefinedDescriptorSetLayout; class ScaffoldCmdIterator; }}
namespace RenderCore { class IDevice; class IDescriptorSet; class SamplerPool; class IResource; }
namespace Utility { class ParameterBox; }
namespace BufferUploads { using CommandListID = uint32_t; }

namespace RenderCore { namespace Techniques 
{
	/// <summary>Describes in string form how a descriptor set was constructed</summary>
	/// Intended for debugging & unit tests. Don't rely on the output for important functionality
	/// Since the IDescriptorSet itself is an opaque type, we can't otherwise tell if specific
	/// shader inputs got bound. So this provides a means to verify that the bindings happened
	/// as expected
	class DescriptorSetBindingInfo
	{
	public:
		struct Slot
		{
			std::string _layoutName;
			DescriptorType _layoutSlotType;
			DescriptorSetInitializer::BindType _bindType;
			std::string _binding;
		};
		std::vector<Slot> _slots;
	};
	
	class AnimatedUniformBufferHelper;

	class ActualizedDescriptorSet
	{
	public:
		const std::shared_ptr<IDescriptorSet>& GetDescriptorSet() const { return _descriptorSet; }
		const ::Assets::DependencyValidation& GetDependencyValidation() const { return _depVal; }
		BufferUploads::CommandListID GetCompletionCommandList() const { return _completionCommandList; }
		bool ApplyDeformAcceleratorOffset() const { return _applyDeformAcceleratorOffset; }

		std::shared_ptr<IDescriptorSet> _descriptorSet;
		DescriptorSetBindingInfo _bindingInfo;
		BufferUploads::CommandListID _completionCommandList;
		::Assets::DependencyValidation _depVal;
		bool _applyDeformAcceleratorOffset = false;

		ActualizedDescriptorSet();
		ActualizedDescriptorSet(ActualizedDescriptorSet&&);
		ActualizedDescriptorSet& operator=(ActualizedDescriptorSet&&);
		ActualizedDescriptorSet(const ActualizedDescriptorSet&);
		ActualizedDescriptorSet& operator=(const ActualizedDescriptorSet&);
		~ActualizedDescriptorSet();
	};

	struct DeformerToDescriptorSetBinding
	{
		using DescSetSlotAndPageOffset = std::pair<unsigned, unsigned>;
		std::vector<DescSetSlotAndPageOffset> _animatedSlots;
		std::shared_ptr<IResource> _dynamicPageResource;
		const uint64_t GetHash() const;
	};

	struct ConstructDescriptorSetHelper
	{
		void Construct(
			std::promise<ActualizedDescriptorSet>&& promise,
			const Assets::PredefinedDescriptorSetLayout& layout,
			IteratorRange<Assets::ScaffoldCmdIterator> materialMachine,
			const DeformerToDescriptorSetBinding* deformBinding);

		std::shared_ptr<IDescriptorSet> ConstructImmediately(
			const Assets::PredefinedDescriptorSetLayout& layout,
			const UniformsStreamInterface& usi,
			const UniformsStream& us);

		std::shared_ptr<IDevice> _device;
		SamplerPool* _samplerPool = nullptr;
		PipelineType _pipelineType = PipelineType::Graphics;
		bool _generateBindingInfo = false;
	};

	uint64_t HashMaterialMachine(IteratorRange<Assets::ScaffoldCmdIterator> materialMachine);

	// Create a material machine that can be passed to ConstructDescriptorSetHelper::Construct
	// Fairly primitive implementation, mostly intended for unit tests
	struct ManualMaterialMachine
	{
	public:
		IteratorRange<Assets::ScaffoldCmdIterator> GetMaterialMachine() const;
		ManualMaterialMachine(
			const ParameterBox& constantBindings,
			const ParameterBox& resourceBindings,
			IteratorRange<const std::pair<uint64_t, SamplerDesc>*> samplerBindings = {});
	private:
		std::unique_ptr<uint8_t[], PODAlignedDeletor> _dataBlock;
		size_t _primaryBlockSize;
	};

}}
