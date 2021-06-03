// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../UniformsStream.h"
#include "../../Assets/AssetFuture.h"
#include <vector>
#include <memory>

namespace RenderCore { namespace Assets { class PredefinedDescriptorSetLayout; }}
namespace RenderCore { class IDevice; class IDescriptorSet; }
namespace Utility { class ParameterBox; }

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

	void ConstructDescriptorSet(
		::Assets::FuturePtr<RenderCore::IDescriptorSet>& future,
		const std::shared_ptr<IDevice>& device,
		const Utility::ParameterBox& constantBindings,
		const Utility::ParameterBox& resourceBindings,
		IteratorRange<const std::pair<uint64_t, std::shared_ptr<ISampler>>*> samplerBindings,
		const RenderCore::Assets::PredefinedDescriptorSetLayout& layout,
		DescriptorSetBindingInfo* bindingInfo = nullptr);

}}
