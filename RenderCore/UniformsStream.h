// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "Types.h"		// for ShaderStage
#include "Assets/PredefinedDescriptorSetLayout.h"
#include "../Utility/IteratorUtils.h"
#include <vector>

namespace RenderCore 
{
	class MiniInputElementDesc;
	class IResourceView;
	class ISampler;
	enum class Format;
	class DescriptorSetSignature;

	class UniformsStream
	{
	public:
		using ImmediateData = IteratorRange<const void*>;
		IteratorRange<const IResourceView*const*> _resourceViews = {};
		IteratorRange<const ImmediateData*> _immediateData = {};
		IteratorRange<const ISampler*const*> _samplers = {};
	};

	class ImmediateDataStream
	{
	public:
		std::vector<UniformsStream::ImmediateData> _immediateDatas;
		operator UniformsStream()
		{
			return UniformsStream { {}, _immediateDatas, {} };
		}

		ImmediateDataStream(IteratorRange<const void*> b0);
		ImmediateDataStream(IteratorRange<const void*> b0, IteratorRange<const void*> b1);
		ImmediateDataStream(IteratorRange<const void*> b0, IteratorRange<const void*> b1, IteratorRange<const void*> b2);
		ImmediateDataStream(IteratorRange<const void*> b0, IteratorRange<const void*> b1, IteratorRange<const void*> b2, IteratorRange<const void*> b3);

		template<typename T0> ImmediateDataStream(const T0& b0);
		template<typename T0, typename T1> ImmediateDataStream(const T0& b0, const T1& b1);
		template<typename T0, typename T1, typename T2> ImmediateDataStream(const T0& b0, const T1& b1, const T2& b2);
		template<typename T0, typename T1, typename T2, typename T3> ImmediateDataStream(const T0& b0, const T1& b1, const T2& b2, const T3& b3);
	};

	class ResourceViewStream
	{
	public:
		std::vector<const IResourceView*> _resourceViews;
		operator UniformsStream()
		{
			return UniformsStream { _resourceViews, {}, {} };
		}

		ResourceViewStream(const IResourceView&);
		ResourceViewStream(const IResourceView&, const IResourceView&);
		ResourceViewStream(const IResourceView&, const IResourceView&, const IResourceView&);
		ResourceViewStream(const IResourceView&, const IResourceView&, const IResourceView&, const IResourceView&);
	};

	class ConstantBufferElementDesc
	{
	public:
		uint64_t    _semanticHash = 0ull;
		Format      _nativeFormat = Format(0);
		unsigned    _offset = 0u;
		unsigned    _arrayElementCount = 0u;            // set to zero if the element is not actually an array (ie, use std::max(1u, _arrayElementCount) in most cases)
	};

	unsigned CalculateSize(IteratorRange<const ConstantBufferElementDesc*> elements);

	class UniformsStreamInterface
	{
	public:
		void BindResourceView(unsigned slot, uint64_t hashName, IteratorRange<const ConstantBufferElementDesc*> _cbElements = {});
		void BindImmediateData(unsigned slot, uint64_t hashName, IteratorRange<const ConstantBufferElementDesc*> _cbElements = {});
		void BindSampler(unsigned slot, uint64_t hashName);
		void BindFixedDescriptorSet(unsigned slot, uint64_t hashName, const DescriptorSetSignature* signature = nullptr);

		struct ExplicitCBLayout
		{
			std::vector<ConstantBufferElementDesc> _elements = {};
		};
		IteratorRange<const ConstantBufferElementDesc*> GetCBLayoutElements(uint64_t hashName) const;

		struct FixedDescriptorSetBinding
		{
			const DescriptorSetSignature* _signature = nullptr;
		};
		const DescriptorSetSignature* GetDescriptorSetSignature(uint64_t hashName) const;

		uint64_t GetHash() const;		// returns zero for an empty object

		IteratorRange<const uint64_t*> GetResourceViewBindings() const { return _resourceViewBindings; };
		IteratorRange<const uint64_t*> GetImmediateDataBindings() const { return _immediateDataBindings; }
		IteratorRange<const uint64_t*> GetSamplerBindings() const { return _samplerBindings; }
		IteratorRange<const uint64_t*> GetFixedDescriptorSetBindings() const { return _fixedDescriptorSetBindings; }

		void Reset();

		UniformsStreamInterface();
		~UniformsStreamInterface();

	////////////////////////////////////////////////////////////////////////
	protected:
		std::vector<uint64_t> _resourceViewBindings;
		std::vector<uint64_t> _immediateDataBindings;
		std::vector<uint64_t> _samplerBindings;
		std::vector<uint64_t> _fixedDescriptorSetBindings;
		std::vector<std::pair<uint64_t, ExplicitCBLayout>> _cbLayouts;
		std::vector<std::pair<uint64_t, FixedDescriptorSetBinding>> _descriptorSetLayouts;

		mutable uint64_t _hash;
	};

	enum class DescriptorType
	{
		SampledTexture,
		UniformBuffer,
		UnorderedAccessTexture,
		UnorderedAccessBuffer,
		Sampler,
		InputAttachment,
		UniformTexelBuffer,					// "uniform texel buffer" in Vulkan terminology 
		UnorderedAccessTexelBuffer,			// "storage texel buffer" in Vulkan terminology 
		UniformBufferDynamicOffset,
		UnorderedAccessBufferDynamicOffset,
		Empty
	};

	const char* AsString(DescriptorType);

	struct DescriptorSlot
	{
		DescriptorType _type = DescriptorType::Empty;
		unsigned _count = 1;
	};

	class DescriptorSetSignature
	{
	public:
		std::vector<DescriptorSlot> _slots;
		std::vector<uint64_t> _slotNames;
		std::vector<std::shared_ptr<ISampler>> _fixedSamplers;		// this is parallel to "_slots" -- ie, it applies to the slot with the corresponding index
		uint64_t GetHashIgnoreNames() const;

		DescriptorSetSignature() = default;
		DescriptorSetSignature(std::initializer_list<DescriptorSlot> init) : _slots(std::move(init)) {}
		DescriptorSetSignature(std::initializer_list<std::pair<DescriptorSlot, uint64_t>> init);
	};

	class DescriptorSetInitializer
	{
	public:
		enum class BindType { ResourceView, Sampler, ImmediateData };
		struct BindTypeAndIdx
		{
			BindType _type = BindType::ResourceView;
			unsigned _uniformsStreamIdx = ~0u;
			unsigned _descriptorSetSlot = ~0u;
			unsigned _descriptorSetArrayIdx = 0u;
		};
		IteratorRange<const BindTypeAndIdx*> _slotBindings;
		UniformsStream _bindItems;
	};

	class PipelineLayoutInitializer
	{
	public:
		struct DescriptorSetBinding
		{
			std::string _name;
			DescriptorSetSignature _signature;
			PipelineType _pipelineType;
		};

		struct PushConstantsBinding
		{
			std::string _name;
			unsigned _cbSize = 0;
			ShaderStage _shaderStage;
			std::vector<ConstantBufferElementDesc> _cbElements;
		};

		void AppendDescriptorSet(
			const std::string& name,
			const DescriptorSetSignature& signature,
			PipelineType pipelineType);

		void AppendPushConstants(
			const std::string& name,
			IteratorRange<const ConstantBufferElementDesc*> elements,
			ShaderStage shaderStage);

		void AppendPushConstants(
			const std::string& name,
			size_t bufferSize,
			ShaderStage shaderStage);

		IteratorRange<const DescriptorSetBinding*> GetDescriptorSets() const { return MakeIteratorRange(_descriptorSets); }
		IteratorRange<const PushConstantsBinding*> GetPushConstants() const { return MakeIteratorRange(_pushConstants); }

		PipelineLayoutInitializer();
		PipelineLayoutInitializer(
			IteratorRange<const DescriptorSetBinding*> descriptorSets,
			IteratorRange<const PushConstantsBinding*> pushConstants);
		~PipelineLayoutInitializer();

		std::vector<DescriptorSetBinding> _descriptorSets;
		std::vector<PushConstantsBinding> _pushConstants;
	};

	class LegacyRegisterBindingDesc
	{
	public:
		enum class RegisterType { Sampler, ShaderResource, ConstantBuffer, UnorderedAccess, Unknown };
		enum class RegisterQualifier { Texture, Buffer, None };

		struct Entry
		{
			unsigned		_begin = 0, _end = 0;
			uint64_t		_targetDescriptorSetBindingName = 0ull;
			unsigned		_targetDescriptorSetIdx = 0;
			unsigned		_targetBegin = 0, _targetEnd = 0;
		};

		void AppendEntry(
			RegisterType type, RegisterQualifier qualifier,
			const Entry& entry);

		IteratorRange<const Entry*>	GetEntries(RegisterType type, RegisterQualifier qualifier = RegisterQualifier::None) const;

		LegacyRegisterBindingDesc();
		~LegacyRegisterBindingDesc();
	private:
		std::vector<Entry> _samplerRegisters;
		std::vector<Entry> _constantBufferRegisters;
		std::vector<Entry> _srvRegisters;
		std::vector<Entry> _uavRegisters;
		std::vector<Entry> _srvRegisters_boundToBuffer;
		std::vector<Entry> _uavRegisters_boundToBuffer;
	};

	template<typename T0> ImmediateDataStream::ImmediateDataStream(const T0& b0) : ImmediateDataStream(MakeOpaqueIteratorRange(b0)) {}
	template<typename T0, typename T1> ImmediateDataStream::ImmediateDataStream(const T0& b0, const T1& b1) : ImmediateDataStream(MakeOpaqueIteratorRange(b0), MakeOpaqueIteratorRange(b1)) {}
	template<typename T0, typename T1, typename T2> ImmediateDataStream::ImmediateDataStream(const T0& b0, const T1& b1, const T2& b2) : ImmediateDataStream(MakeOpaqueIteratorRange(b0), MakeOpaqueIteratorRange(b1), MakeOpaqueIteratorRange(b2)) {}
	template<typename T0, typename T1, typename T2, typename T3> ImmediateDataStream::ImmediateDataStream(const T0& b0, const T1& b1, const T2& b2, const T3& b3) : ImmediateDataStream(MakeOpaqueIteratorRange(b0), MakeOpaqueIteratorRange(b1), MakeOpaqueIteratorRange(b2), MakeOpaqueIteratorRange(b3)) {}
}
