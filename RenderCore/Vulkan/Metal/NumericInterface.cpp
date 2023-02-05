// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "InputLayout.h"
#include "PipelineLayout.h"
#include "TextureView.h"
#include "ObjectFactory.h"
#include "Pools.h"
#include "DescriptorSet.h"
#include "DeviceContext.h"
#include "CmdListAttachedStorage.h"
#include "../../BufferView.h"
#include "../../../OSServices/Log.h"
#include "../../../Utility/MemoryUtils.h"
#include "../../../Utility/ArithmeticUtils.h"
#include "../../../Utility/BitUtils.h"

#include "IncludeVulkan.h"

namespace RenderCore { namespace Metal_Vulkan
{

	class NumericUniformsInterface::Pimpl
    {
    public:
		static const unsigned s_maxBindings = 64u;

        DescriptorPool*     _descriptorPool = nullptr;
		GlobalPools*		_globalPools = nullptr;
		CmdListAttachedStorage* _cmdListAttachedStorage = nullptr;

		struct Binding
		{
			unsigned _descSetIndex = ~0u;
			unsigned _slotIndex = ~0u;
		};

        Binding		_constantBufferRegisters[s_maxBindings];
        Binding		_samplerRegisters[s_maxBindings];

		Binding		_srvRegisters[s_maxBindings];
		Binding		_uavRegisters[s_maxBindings];

		Binding		_srvRegisters_boundToBuffer[s_maxBindings];
		Binding		_uavRegisters_boundToBuffer[s_maxBindings];

		class DescSet
		{
		public:
			ProgressiveDescriptorSetBuilder		_builder;
			VulkanUniquePtr<VkDescriptorSet>    _activeDescSet;
			uint64_t							_slotsFilled = 0;
			uint64_t							_allSlotsMask = 0;
			std::shared_ptr<CompiledDescriptorSetLayout> _layout;
			unsigned							_bindSlot = 0;

			#if defined(VULKAN_VERBOSE_DEBUG)
				DescriptorSetDebugInfo _description;
			#endif

			DescSet(const std::shared_ptr<CompiledDescriptorSetLayout>& layout, unsigned bindSlot)
			: _builder(layout->GetDescriptorSlots()), _layout(layout), _bindSlot(bindSlot)
			{
				#if defined(VULKAN_VERBOSE_DEBUG)
					_description._descriptorSetInfo = "NumericUniformsInterface";
				#endif
				_allSlotsMask = (1ull<<uint64_t(layout->GetDescriptorSlots().size()))-1ull;
			}

			void Reset(GlobalPools& globalPools)
			{
				_builder.Reset();
				_activeDescSet.reset();
				_slotsFilled = 0;
				#if defined(VULKAN_VERBOSE_DEBUG)
					_description = DescriptorSetDebugInfo{};
					_description._descriptorSetInfo = "NumericUniformsInterface";
				#endif

				// Skip avoid binding dummys by default. This cuts down on the overhead
				// of setting up descriptor sets; but it's a bit dangerous because the
				// GPU can halt if it attempts to read from an uninitialized descriptor
				// _builder.BindDummyDescriptors(globalPools, _allSlotsMask);
			}
		};
		std::vector<DescSet> _descSet;
		bool _hasChanges = false;
		VkPipelineLayout _configuredPipelineLayout = nullptr;		// used for validating that the pipeline layout hasn't changed

		Pimpl(const CompiledPipelineLayout& layout)
		{
			_descSet.reserve(layout.GetDescriptorSetCount());
		}

		unsigned LookupDescriptorSet(const CompiledPipelineLayout& pipelineLayout, uint64_t bindingName)
		{
			auto bindingNames = pipelineLayout.GetDescriptorSetBindingNames();
			for (unsigned c=0; c<bindingNames.size(); ++c)
				if (bindingNames[c] == bindingName) {
					for (unsigned d=0; d<_descSet.size(); ++d)
						if (_descSet[d]._bindSlot == c)
							return d;
					_descSet.emplace_back(pipelineLayout.GetDescriptorSetLayout(c), c);
					return (unsigned)_descSet.size()-1;
				}
			return ~0u;
		}
    };

    void    NumericUniformsInterface::Bind(unsigned startingPoint, IteratorRange<const IResourceView*const*> resources)
    {
		assert(_pimpl);
        for (unsigned c=0; c<unsigned(resources.size()); ++c) {
            assert((startingPoint + c) < Pimpl::s_maxBindings);
			if  (!resources[c]) continue;

			auto* resView = checked_cast<const ResourceView*>(resources[c]);
			auto viewType = resView->GetType();

			if (viewType == ResourceView::Type::ImageView) {
				const auto& binding = _pimpl->_srvRegisters[startingPoint + c];
				if (binding._slotIndex == ~0u) {
					Log(Debug) << "Texture view numeric binding (" << (startingPoint + c) << ") is off root signature" << std::endl;
					continue;
				}
				_pimpl->_descSet[binding._descSetIndex]._builder.Bind(binding._slotIndex, *resView);
				_pimpl->_hasChanges |= _pimpl->_descSet[binding._descSetIndex]._builder.HasChanges();
			} else if (viewType == ResourceView::Type::BufferView) {
				const auto& binding = _pimpl->_srvRegisters_boundToBuffer[startingPoint + c];
				if (binding._slotIndex == ~0u) {
					Log(Debug) << "Texture view numeric binding (" << (startingPoint + c) << ") is off root signature" << std::endl;
					continue;
				}
				_pimpl->_descSet[binding._descSetIndex]._builder.Bind(binding._slotIndex, *resView);
				_pimpl->_hasChanges |= _pimpl->_descSet[binding._descSetIndex]._builder.HasChanges();
			} else if (viewType == ResourceView::Type::BufferAndRange) {
				const auto& binding = _pimpl->_constantBufferRegisters[startingPoint + c];
				if (binding._slotIndex == ~0u) {
					Log(Debug) << "Texture view numeric binding (" << (startingPoint + c) << ") is off root signature" << std::endl;
					continue;
				}
				_pimpl->_descSet[binding._descSetIndex]._builder.Bind(binding._slotIndex, *resView);
				_pimpl->_hasChanges |= _pimpl->_descSet[binding._descSetIndex]._builder.HasChanges();
			}
        }
    }

    void    NumericUniformsInterface::Bind(unsigned startingPoint, IteratorRange<const ConstantBufferView*> constantBuffers)
    {
		assert(_pimpl);

		for (unsigned c=0; c<constantBuffers.size(); ++c) {
			if (!constantBuffers[c]._prebuiltBuffer) continue;

			const auto& binding = _pimpl->_constantBufferRegisters[startingPoint + c];
			if (binding._slotIndex == ~0u) {
				Log(Debug) << "Uniform buffer numeric binding (" << (startingPoint + c) << ") is off root signature" << std::endl;
				continue;
			}

			VkDescriptorBufferInfo bufferInfo;
			bufferInfo.buffer = checked_cast<const Resource*>(constantBuffers[c]._prebuiltBuffer)->GetBuffer();
			if (constantBuffers[c]._prebuiltRangeEnd != 0) {
				bufferInfo.offset = constantBuffers[c]._prebuiltRangeBegin;
				bufferInfo.range = constantBuffers[c]._prebuiltRangeEnd - constantBuffers[c]._prebuiltRangeBegin;
			} else {
				bufferInfo.offset = 0;
				bufferInfo.range = VK_WHOLE_SIZE;
			}

			_pimpl->_descSet[binding._descSetIndex]._builder.Bind(
				binding._slotIndex, bufferInfo,
				checked_cast<const Resource*>(constantBuffers[c]._prebuiltBuffer)->GetName());
			_pimpl->_hasChanges |= _pimpl->_descSet[binding._descSetIndex]._builder.HasChanges();
        }
    }

	void	NumericUniformsInterface::BindConstantBuffers(unsigned startingPoint, IteratorRange<const UniformsStream::ImmediateData*> constantBuffers)
	{
		assert(_pimpl);

		size_t totalSize = 0;
		auto alignment = GetObjectFactory().GetPhysicalDeviceProperties().limits.minUniformBufferOffsetAlignment;
		for (unsigned c=0; c<constantBuffers.size(); ++c) {
			if (constantBuffers[c].empty()) continue;

			const auto& binding = _pimpl->_constantBufferRegisters[startingPoint + c];
			if (binding._slotIndex == ~0u) {
				Log(Debug) << "Uniform buffer numeric binding (" << (startingPoint + c) << ") is off root signature" << std::endl;
				continue;
			}

			auto alignedSize = CeilToMultiple((unsigned)constantBuffers[c].size(), alignment);
			totalSize += alignedSize;
		}

		if (!totalSize) return;

		auto temporaryMapping = _pimpl->_cmdListAttachedStorage->MapStorage(totalSize, BindFlag::ConstantBuffer);
		if (temporaryMapping.GetData().empty()) {
			Log(Warning) << "Failed to allocate temporary buffer space in numeric uniforms interface" << std::endl;
			return;
		}

		size_t iterator = 0;
		auto beginInResource = temporaryMapping.GetBeginAndEndInResource().first;

		for (unsigned c=0; c<constantBuffers.size(); ++c) {
			if (constantBuffers[c].empty()) continue;

			const auto& binding = _pimpl->_constantBufferRegisters[startingPoint + c];
			if (binding._slotIndex == ~0u) continue;

			auto pkt = constantBuffers[c];
			assert(!pkt.empty());
			std::memcpy(PtrAdd(temporaryMapping.GetData().begin(), iterator), pkt.data(), pkt.size());
			VkDescriptorBufferInfo tempSpace;
			tempSpace.buffer = checked_cast<Resource*>(temporaryMapping.GetResource().get())->GetBuffer();
			tempSpace.offset = beginInResource + iterator;
			tempSpace.range = pkt.size();
			_pimpl->_descSet[binding._descSetIndex]._builder.Bind(
				binding._slotIndex, tempSpace
				VULKAN_VERBOSE_DEBUG_ONLY(, {}, "temporary buffer"));

			auto alignedSize = CeilToMultiple((unsigned)pkt.size(), alignment);
			iterator += alignedSize;

			_pimpl->_hasChanges |= _pimpl->_descSet[binding._descSetIndex]._builder.HasChanges();
        }
	}

    void    NumericUniformsInterface::Bind(unsigned startingPoint, IteratorRange<const VkSampler*> samplers)
    {
		assert(_pimpl);
        for (unsigned c=0; c<unsigned(samplers.size()); ++c) {
            if (!samplers[c]) continue;

			const auto& binding = _pimpl->_samplerRegisters[startingPoint + c];
			if (binding._slotIndex == ~0u) {
				Log(Debug) << "Sampler numeric binding (" << (startingPoint + c) << ") is off root signature" << std::endl;
				continue;
			}

			_pimpl->_descSet[binding._descSetIndex]._builder.Bind(binding._slotIndex, samplers[c]);
			_pimpl->_hasChanges |= _pimpl->_descSet[binding._descSetIndex]._builder.HasChanges();
        }
    }

	void NumericUniformsInterface::Apply(
		DeviceContext& context,
		SharedEncoder& encoder) const
	{
		assert(_pimpl);
		if (encoder.GetUnderlyingPipelineLayout() != _pimpl->_configuredPipelineLayout)
			Throw(std::runtime_error("Pipeline layout has changed while using NumericUniformsInterface. After the pipeline layout changes, remember to call BeginNumericUniformsInterface() on the encoder again to begin a new uniforms interface"));

        // If we've had any changes this last time, we must create new
        // descriptor sets. We will use vkUpdateDescriptorSets to fill in these
        // sets with the latest changes. Note that this will require copy across the
        // bindings that haven't changed.
        // It turns out that copying using VkCopyDescriptorSet is probably going to be
        // slow. We should try a different approach.
		for(unsigned dIdx=0; dIdx<_pimpl->_descSet.size(); ++dIdx) {
			auto&d = _pimpl->_descSet[dIdx];
			if (d._builder.HasChanges()) {
				auto newSet = _pimpl->_descriptorPool->Allocate(*d._layout);

				#if defined(VULKAN_VALIDATE_RESOURCE_VISIBILITY)
					if (!d._builder._pendingResourceVisibilityChanges.empty())
						context.GetActiveCommandList().RequireResourceVisibility(d._builder._pendingResourceVisibilityChanges);
				#endif

				auto written = d._builder.FlushChanges(
					_pimpl->_descriptorPool->GetDevice(),
					newSet.get(),
					d._activeDescSet.get(),
					d._slotsFilled
					VULKAN_VERBOSE_DEBUG_ONLY(, d._description));

				d._slotsFilled |= written;
				d._activeDescSet = std::move(newSet);

				encoder.BindDescriptorSet(
					d._bindSlot, d._activeDescSet.get(), {}
					VULKAN_VERBOSE_DEBUG_ONLY(, DescriptorSetDebugInfo{d._description}));
			}
		}
    }

	void    NumericUniformsInterface::Reset()
	{
		if (_pimpl) {
			for (auto&d:_pimpl->_descSet)
				d.Reset(*_pimpl->_globalPools);
			_pimpl->_hasChanges = false;
		}
	}

	bool	NumericUniformsInterface::HasChanges() const
	{
		return _pimpl->_hasChanges;
	}

    NumericUniformsInterface::NumericUniformsInterface(
        const ObjectFactory& factory,
		const ICompiledPipelineLayout& ipipelineLayout,
		CmdListAttachedStorage& cmdListAttachedStorage,
        const LegacyRegisterBindingDesc& bindings)
    {
		const auto& pipelineLayout = *checked_cast<const CompiledPipelineLayout*>(&ipipelineLayout);
        _pimpl = std::make_unique<Pimpl>(pipelineLayout);
        _pimpl->_globalPools = &GetGlobalPools();
		_pimpl->_descriptorPool = &_pimpl->_globalPools->_mainDescriptorPool;
		_pimpl->_cmdListAttachedStorage = &cmdListAttachedStorage;
		_pimpl->_configuredPipelineLayout = pipelineLayout.GetUnderlying();
		_pimpl->_hasChanges = false;

		for (auto passThrough:bindings.GetPassThroughDescriptorSets()) {
			// "pass-through" are simplier configuration settings, whereby we jsut expose the registers of the bound pipeline
			// layout in a 1:1 way. LegacyRegisterBindingDesc gives us a name of a descriptor set, and we'll search for that in 
			// the pipeline layout binding
			auto descriptorSet = _pimpl->LookupDescriptorSet(pipelineLayout, passThrough);
			if (descriptorSet != ~0u) {
				// register all slots 1:1 in this desc set
				auto& descSetLayout = *pipelineLayout.GetDescriptorSetLayout(_pimpl->_descSet[descriptorSet]._bindSlot);

				for (unsigned dIdx=0; dIdx<descSetLayout.GetDescriptorSlots().size(); ++dIdx) {
					const auto& d = descSetLayout.GetDescriptorSlots()[dIdx];
					if (d._count != 1) continue;		// can't handle arrays
					switch (d._type) {
					case DescriptorType::InputAttachment:
					case DescriptorType::SampledTexture:
						_pimpl->_srvRegisters[dIdx]._descSetIndex = descriptorSet;
						_pimpl->_srvRegisters[dIdx]._slotIndex = dIdx;
						break;
					case DescriptorType::UniformBuffer:
						_pimpl->_constantBufferRegisters[dIdx]._descSetIndex = descriptorSet;
						_pimpl->_constantBufferRegisters[dIdx]._slotIndex = dIdx;
						break;
					case DescriptorType::Sampler:
						_pimpl->_samplerRegisters[dIdx]._descSetIndex = descriptorSet;
						_pimpl->_samplerRegisters[dIdx]._slotIndex = dIdx;
						break;
					case DescriptorType::UnorderedAccessTexture:
						_pimpl->_uavRegisters[dIdx]._descSetIndex = descriptorSet;
						_pimpl->_uavRegisters[dIdx]._slotIndex = dIdx;
						break;
					case DescriptorType::UnorderedAccessBuffer:
						_pimpl->_uavRegisters[dIdx]._descSetIndex = descriptorSet;
						_pimpl->_uavRegisters[dIdx]._slotIndex = dIdx;
						break;
					case DescriptorType::UniformTexelBuffer:
						_pimpl->_srvRegisters_boundToBuffer[dIdx]._descSetIndex = descriptorSet;
						_pimpl->_srvRegisters_boundToBuffer[dIdx]._slotIndex = dIdx;
						break;
					case DescriptorType::UnorderedAccessTexelBuffer:
						_pimpl->_uavRegisters_boundToBuffer[dIdx]._descSetIndex = descriptorSet;
						_pimpl->_uavRegisters_boundToBuffer[dIdx]._slotIndex = dIdx;
						break;
					case DescriptorType::UniformBufferDynamicOffset:
					case DescriptorType::UnorderedAccessBufferDynamicOffset:
					default:
						break;	// unhandled type
					}
				}
			}
		}

		for (const auto&e:bindings.GetEntries(LegacyRegisterBindingDesc::RegisterType::Sampler)) {
			assert(e._end <= Pimpl::s_maxBindings);
			for (unsigned b=e._begin; b!=e._end; ++b) {
				auto descSet = _pimpl->LookupDescriptorSet(pipelineLayout, e._targetDescriptorSetBindingName);
				if (descSet != ~0u) {
					_pimpl->_samplerRegisters[b]._descSetIndex = descSet;
					_pimpl->_samplerRegisters[b]._slotIndex = b-e._begin+e._targetBegin;
				}
			}
		}

		for (const auto&e:bindings.GetEntries(LegacyRegisterBindingDesc::RegisterType::ConstantBuffer)) {
			assert(e._end <= Pimpl::s_maxBindings);
			for (unsigned b=e._begin; b!=e._end; ++b) {
				auto descSet = _pimpl->LookupDescriptorSet(pipelineLayout, e._targetDescriptorSetBindingName);
				if (descSet != ~0u) {
					_pimpl->_constantBufferRegisters[b]._descSetIndex = descSet;
					_pimpl->_constantBufferRegisters[b]._slotIndex = b-e._begin+e._targetBegin;
				}
			}
		}

		for (const auto&e:bindings.GetEntries(LegacyRegisterBindingDesc::RegisterType::ShaderResource)) {
			assert(e._end <= Pimpl::s_maxBindings);
			for (unsigned b=e._begin; b!=e._end; ++b) {
				auto descSet = _pimpl->LookupDescriptorSet(pipelineLayout, e._targetDescriptorSetBindingName);
				if (descSet != ~0u) {
					_pimpl->_srvRegisters[b]._descSetIndex = descSet;
					_pimpl->_srvRegisters[b]._slotIndex = b-e._begin+e._targetBegin;
				}
			}
		}

		for (const auto&e:bindings.GetEntries(LegacyRegisterBindingDesc::RegisterType::UnorderedAccess)) {
			assert(e._end <= Pimpl::s_maxBindings);
			for (unsigned b=e._begin; b!=e._end; ++b) {
				auto descSet = _pimpl->LookupDescriptorSet(pipelineLayout, e._targetDescriptorSetBindingName);
				if (descSet != ~0u) {
					_pimpl->_uavRegisters[b]._descSetIndex = descSet;
					_pimpl->_uavRegisters[b]._slotIndex = b-e._begin+e._targetBegin;
				}
			}
		}

		for (const auto&e:bindings.GetEntries(LegacyRegisterBindingDesc::RegisterType::ShaderResource, LegacyRegisterBindingDesc::RegisterQualifier::Buffer)) {
			assert(e._end <= Pimpl::s_maxBindings);
			for (unsigned b=e._begin; b!=e._end; ++b) {
				auto descSet = _pimpl->LookupDescriptorSet(pipelineLayout, e._targetDescriptorSetBindingName);
				if (descSet != ~0u) {
					_pimpl->_srvRegisters_boundToBuffer[b]._descSetIndex = descSet;
					_pimpl->_srvRegisters_boundToBuffer[b]._slotIndex = b-e._begin+e._targetBegin;
				}
			}
		}

		for (const auto&e:bindings.GetEntries(LegacyRegisterBindingDesc::RegisterType::UnorderedAccess, LegacyRegisterBindingDesc::RegisterQualifier::Buffer)) {
			assert(e._end <= Pimpl::s_maxBindings);
			for (unsigned b=e._begin; b!=e._end; ++b) {
				auto descSet = _pimpl->LookupDescriptorSet(pipelineLayout, e._targetDescriptorSetBindingName);
				if (descSet != ~0u) {
					_pimpl->_uavRegisters_boundToBuffer[b]._descSetIndex = descSet;
					_pimpl->_uavRegisters_boundToBuffer[b]._slotIndex = b-e._begin+e._targetBegin;
				}
			}
		}
    }

	NumericUniformsInterface::NumericUniformsInterface() 
	{
	}

    NumericUniformsInterface::~NumericUniformsInterface()
    {
    }

	NumericUniformsInterface::NumericUniformsInterface(NumericUniformsInterface&& moveFrom)
	: _pimpl(std::move(moveFrom._pimpl))
	{
	}

	NumericUniformsInterface& NumericUniformsInterface::operator=(NumericUniformsInterface&& moveFrom)
	{
		_pimpl = std::move(moveFrom._pimpl);
		return *this;
	}

}}

