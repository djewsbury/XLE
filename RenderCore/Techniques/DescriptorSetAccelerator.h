// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../UniformsStream.h"
#include "../Types.h"
#include <vector>
#include <memory>
#include <future>

namespace RenderCore { namespace Assets { class PredefinedDescriptorSetLayout; }}
namespace RenderCore { class IDevice; class IDescriptorSet; class SamplerPool; }
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
		const std::shared_ptr<RenderCore::IDescriptorSet>& GetDescriptorSet() const { return _descriptorSet; }
		const ::Assets::DependencyValidation& GetDependencyValidation() const { return _depVal; }
		BufferUploads::CommandListID GetCompletionCommandList() const { return _completionCommandList; }

		std::shared_ptr<RenderCore::IDescriptorSet> _descriptorSet;
		DescriptorSetBindingInfo _bindingInfo;
		BufferUploads::CommandListID _completionCommandList;
		::Assets::DependencyValidation _depVal;

		ActualizedDescriptorSet();
		ActualizedDescriptorSet(ActualizedDescriptorSet&&);
		ActualizedDescriptorSet& operator=(ActualizedDescriptorSet&&);
		ActualizedDescriptorSet(const ActualizedDescriptorSet&);
		ActualizedDescriptorSet& operator=(const ActualizedDescriptorSet&);
		~ActualizedDescriptorSet();
	};

	void ConstructDescriptorSet(
		std::promise<ActualizedDescriptorSet>&& promise,
		const std::shared_ptr<IDevice>& device,
		const RenderCore::Assets::PredefinedDescriptorSetLayout& layout,
		const Utility::ParameterBox& constantBindings,
		const Utility::ParameterBox& resourceBindings,
		IteratorRange<const std::pair<uint64_t, std::shared_ptr<ISampler>>*> samplerBindings,
		SamplerPool* samplerPool,
		IteratorRange<const AnimatedParameterBinding*> animatedBindings = {},
		const std::shared_ptr<IResourceView>& dynamicPageResource = nullptr,
		PipelineType pipelineType = PipelineType::Graphics,
		bool generateBindingInfo = false);

	std::shared_ptr<IDescriptorSet> ConstructDescriptorSet(
		IDevice& device,
		const Assets::PredefinedDescriptorSetLayout& layout,
		const UniformsStreamInterface& usi,
		const UniformsStream& us,
		SamplerPool* samplerPool,
		PipelineType pipelineType = PipelineType::Graphics);

}}
