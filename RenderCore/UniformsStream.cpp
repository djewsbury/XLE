// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "UniformsStream.h"
#include "Types.h"
#include "Format.h"
#include "IDevice.h"
#include "StateDesc.h"
#include "../Utility/MemoryUtils.h"
#include "../Utility/BitUtils.h"
#include "../Core/SelectConfiguration.h"
#include "../Core/Exceptions.h"
#include <stdexcept>

namespace RenderCore 
{
	ImmediateDataStream::ImmediateDataStream(IteratorRange<const void*> b0)
	: _immediateDatas({b0})
	{}
	ImmediateDataStream::ImmediateDataStream(IteratorRange<const void*> b0, IteratorRange<const void*> b1)
	: _immediateDatas({b0, b1})
	{}
	ImmediateDataStream::ImmediateDataStream(IteratorRange<const void*> b0, IteratorRange<const void*> b2, IteratorRange<const void*> b1)
	: _immediateDatas({b0, b1, b2})
	{}
	ImmediateDataStream::ImmediateDataStream(IteratorRange<const void*> b0, IteratorRange<const void*> b1, IteratorRange<const void*> b2, IteratorRange<const void*> b3)
	: _immediateDatas({b0, b1, b2, b3})
	{}

	ResourceViewStream::ResourceViewStream(const IResourceView& r0)
	: _resourceViews({&r0})
	{}
	ResourceViewStream::ResourceViewStream(const IResourceView& r0, const IResourceView& r1)
	: _resourceViews({&r0, &r1})
	{}
	ResourceViewStream::ResourceViewStream(const IResourceView& r0, const IResourceView& r1, const IResourceView& r2)
	: _resourceViews({&r0, &r1, &r2})
	{}
	ResourceViewStream::ResourceViewStream(const IResourceView& r0, const IResourceView& r1, const IResourceView& r2, const IResourceView& r3)
	: _resourceViews({&r0, &r1, &r2, &r3})
	{}

	void UniformsStreamInterface::BindResourceView(unsigned slot, uint64_t hashName, IteratorRange<const ConstantBufferElementDesc*> cbElements)
	{
		if (_resourceViewBindings.size() <= slot)
			_resourceViewBindings.resize(slot+1);

		_resourceViewBindings[slot] = hashName;
		_hash = 0;

		if (cbElements.size()) {
			#if defined(_DEBUG)
				auto i = std::find_if(_cbLayouts.begin(), _cbLayouts.end(), [hashName](auto& c) { return c.first == hashName; });
				assert(i == _cbLayouts.end());
			#endif
			ExplicitCBLayout lyt;
			lyt._elements = std::vector<ConstantBufferElementDesc>{cbElements.begin(), cbElements.end()};
			_cbLayouts.push_back(std::make_pair(hashName, std::move(lyt)));
		}
	}

	void UniformsStreamInterface::BindImmediateData(unsigned slot, uint64_t hashName, IteratorRange<const ConstantBufferElementDesc*> cbElements)
	{
		if (_immediateDataBindings.size() <= slot)
			_immediateDataBindings.resize(slot+1);

		_immediateDataBindings[slot] = hashName;
		_hash = 0;

		if (cbElements.size()) {
			#if defined(_DEBUG)
				auto i = std::find_if(_cbLayouts.begin(), _cbLayouts.end(), [hashName](auto& c) { return c.first == hashName; });
				assert(i == _cbLayouts.end());
			#endif
			ExplicitCBLayout lyt;
			lyt._elements = std::vector<ConstantBufferElementDesc>{cbElements.begin(), cbElements.end()};
			_cbLayouts.push_back(std::make_pair(hashName, std::move(lyt)));
		}
	}

	void UniformsStreamInterface::BindSampler(unsigned slot, uint64_t hashName)
	{
		if (_samplerBindings.size() <= slot)
			_samplerBindings.resize(slot+1);
		_samplerBindings[slot] = hashName;
		_hash = 0;
	}

	void UniformsStreamInterface::BindFixedDescriptorSet(unsigned slot, uint64_t hashName, const DescriptorSetSignature* signature)
	{
		if (_fixedDescriptorSetBindings.size() <= slot)
			_fixedDescriptorSetBindings.resize(slot+1);

		_fixedDescriptorSetBindings[slot] = hashName;
		_hash = 0;

		if (signature) {
			#if defined(_DEBUG)
				auto i = std::find_if(_descriptorSetLayouts.begin(), _descriptorSetLayouts.end(), [hashName](auto& c) { return c.first == hashName; });
				assert(i == _descriptorSetLayouts.end());
			#endif
			FixedDescriptorSetBinding lyt;
			lyt._signature = signature;
			_descriptorSetLayouts.push_back(std::make_pair(hashName, std::move(lyt)));
		}
	}

	IteratorRange<const ConstantBufferElementDesc*> UniformsStreamInterface::GetCBLayoutElements(uint64_t hashName) const
	{
		for (const auto&cb:_cbLayouts)
			if (cb.first == hashName)
				return MakeIteratorRange(cb.second._elements);
		return {};
	}

	const DescriptorSetSignature* UniformsStreamInterface::GetDescriptorSetSignature(uint64_t hashName) const
	{
		for (const auto&descSet:_descriptorSetLayouts)
			if (descSet.first == hashName)
				return descSet.second._signature;
		return nullptr;
	}

	uint64_t UniformsStreamInterface::GetHash() const
	{
		if (expect_evaluation(_hash==0, false)) {
			_hash = DefaultSeed64;
			// to prevent some oddities when the same hash value could be in either in _resourceViewBindings or _immediateDataBindings
			// we need to include the count of the first array we look through in the hash
			// Also note that we ignore _cbLayouts for this hash calculation
			_hash = HashCombine((uint64_t)_resourceViewBindings.size(), _hash);
			_hash = Hash64(AsPointer(_resourceViewBindings.begin()), AsPointer(_resourceViewBindings.end()), _hash);
			_hash = Hash64(AsPointer(_immediateDataBindings.begin()), AsPointer(_immediateDataBindings.end()), _hash);
			_hash = Hash64(AsPointer(_samplerBindings.begin()), AsPointer(_samplerBindings.end()), _hash);
			_hash = Hash64(AsPointer(_fixedDescriptorSetBindings.begin()), AsPointer(_fixedDescriptorSetBindings.end()), _hash);
		}

		return _hash;
	}

	UniformsStreamInterface::UniformsStreamInterface() : _hash(0) {}
	UniformsStreamInterface::~UniformsStreamInterface() {}

	uint64_t DescriptorSetSignature::GetHashIgnoreNames() const
	{
		auto res = Hash64(AsPointer(_slots.begin()), AsPointer(_slots.end()));
		for (unsigned c=0; c<_fixedSamplers.size(); ++c) {
			if (!_fixedSamplers[c]) continue;
			auto h = _fixedSamplers[c]->GetDesc().Hash();
			res = HashCombine(rotl64(h, c), res);
		}
		return res;
	}

	void PipelineLayoutInitializer::AppendDescriptorSet(
		const std::string& name,
		const DescriptorSetSignature& signature,
		PipelineType _pipelineType)
	{
		_descriptorSets.push_back({name, signature, _pipelineType});
	}

	void PipelineLayoutInitializer::AppendPushConstants(
		const std::string& name,
		IteratorRange<const ConstantBufferElementDesc*> elements,
		ShaderStage shaderStage)
	{
		PushConstantsBinding binding;
		binding._name = name;
		binding._cbSize = CalculateSize(elements);
		binding._cbElements = { elements.begin(), elements.end() };
		binding._shaderStage = shaderStage;
		_pushConstants.push_back(std::move(binding));
	}

	void PipelineLayoutInitializer::AppendPushConstants(
		const std::string& name,
		size_t bufferSize,
		ShaderStage shaderStage)
	{
		PushConstantsBinding binding;
		binding._name = name;
		binding._cbSize = bufferSize;
		binding._shaderStage = shaderStage;
		_pushConstants.push_back(std::move(binding));
	}

	PipelineLayoutInitializer::PipelineLayoutInitializer(
		IteratorRange<const DescriptorSetBinding*> descriptorSets,
		IteratorRange<const PushConstantsBinding*> pushConstants)
	: _descriptorSets(descriptorSets.begin(), descriptorSets.end())
	, _pushConstants(pushConstants.begin(), pushConstants.end())
	{}

	PipelineLayoutInitializer::PipelineLayoutInitializer() {}
	PipelineLayoutInitializer::~PipelineLayoutInitializer() {}

	unsigned CalculateSize(IteratorRange<const ConstantBufferElementDesc*> elements)
	{
		// here, we're expecting the offset values in the elements to always contain good data
		unsigned end = 0;
		for (const auto& e:elements)
			end = std::max(end, e._offset + BitsPerPixel(e._nativeFormat)/8);
		return CeilToMultiplePow2(end, 16);
	}

	void LegacyRegisterBindingDesc::AppendEntry(
		RegisterType type, RegisterQualifier qualifier,
		const Entry& entry)
	{
		std::vector<LegacyRegisterBindingDesc::Entry>* dest =  nullptr;
		switch (type) {
		case LegacyRegisterBindingDesc::RegisterType::Sampler: dest = &_samplerRegisters; break;
		case LegacyRegisterBindingDesc::RegisterType::ShaderResource:
			dest = (qualifier == LegacyRegisterBindingDesc::RegisterQualifier::Buffer) ? &_srvRegisters_boundToBuffer : &_srvRegisters;
			break;
		case LegacyRegisterBindingDesc::RegisterType::ConstantBuffer: dest = &_constantBufferRegisters; break;
		case LegacyRegisterBindingDesc::RegisterType::UnorderedAccess:
			dest = (qualifier == LegacyRegisterBindingDesc::RegisterQualifier::Buffer) ? &_uavRegisters_boundToBuffer : &_uavRegisters; 
			break;
		default: assert(0);
		}

		auto di = dest->begin();
		while (di!=dest->end() && di->_begin < entry._end) ++di;

		if (di != dest->begin() && (di-1)->_end > entry._begin)
			Throw(std::runtime_error("Register overlap found in ReadLegacyRegisterBinding"));

		dest->insert(di, entry);
	}

	auto LegacyRegisterBindingDesc::GetEntries(RegisterType type, RegisterQualifier qualifier) const -> IteratorRange<const Entry*>
	{
		switch (type) {
		case RegisterType::Sampler: return MakeIteratorRange(_samplerRegisters);
		case RegisterType::ShaderResource: return (qualifier == RegisterQualifier::Buffer) ? MakeIteratorRange(_srvRegisters_boundToBuffer) : MakeIteratorRange(_srvRegisters);
		case RegisterType::ConstantBuffer: return MakeIteratorRange(_constantBufferRegisters);
		case RegisterType::UnorderedAccess: return (qualifier == RegisterQualifier::Buffer) ? MakeIteratorRange(_uavRegisters_boundToBuffer) : MakeIteratorRange(_uavRegisters);
		default:
			assert(0);
			return {};
		}
	}

	LegacyRegisterBindingDesc::LegacyRegisterBindingDesc() {}
	LegacyRegisterBindingDesc::~LegacyRegisterBindingDesc() {}

}

