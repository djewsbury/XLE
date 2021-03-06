// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "UniformsStream.h"
#include "Types.h"
#include "Format.h"
#include "../Utility/MemoryUtils.h"
#include "../Utility/BitUtils.h"
#include "../Core/SelectConfiguration.h"
#include "../Core/Exceptions.h"
#include <stdexcept>

namespace RenderCore 
{

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
		FixedDescriptorSetBinding binding;
		binding._inputSlot = slot;
		binding._hashName = hashName;
		binding._signature = signature;
		_fixedDescriptorSetBindings.push_back(binding);
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


	static const char* s_descriptorTypeNames[] = {
		"Sampler",
		"Texture",
		"ConstantBuffer",
		"UnorderedAccessTexture",
		"UnorderedAccessBuffer"
	};

	const char* AsString(DescriptorType type)
	{
		if (unsigned(type) < dimof(s_descriptorTypeNames))
			return s_descriptorTypeNames[unsigned(type)];
		return "<<unknown>>";
	}

	DescriptorType AsDescriptorType(StringSection<> type)
	{
		for (unsigned c=0; c<dimof(s_descriptorTypeNames); ++c)
			if (XlEqString(type, s_descriptorTypeNames[c]))
				return (DescriptorType)c;
		return DescriptorType::Unknown;
	}

	uint64_t DescriptorSetSignature::GetHash() const
	{
		return Hash64(AsPointer(_slots.begin()), AsPointer(_slots.end()));
	}

	void PipelineLayoutInitializer::AppendDescriptorSet(
		const std::string& name,
		const DescriptorSetSignature& signature)
	{
		_descriptorSets.push_back({name, signature});
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

