// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "PredefinedDescriptorSetLayout.h"
#include "PredefinedCBLayout.h"
#include "../UniformsStream.h"
#include "../IDevice.h"
#include "../ResourceUtils.h"
#include "../../Assets/DepVal.h"
#include "../../Assets/AssetUtils.h"
#include "../../Assets/PreprocessorIncludeHandler.h"
#include "../../Utility/StringUtils.h"
#include "../../Utility/StringFormat.h"
#include "../../Utility/FastParseValue.h"
#include "../../Utility/BitUtils.h"
#include "../../Utility/MemoryUtils.h"
#include "../../Utility/Streams/PreprocessorInterpreter.h"
#include "../../Utility/Streams/ConditionalPreprocessingTokenizer.h"

namespace RenderCore { namespace Assets 
{
	PredefinedCBLayout::NameAndType ParseStatement(ConditionalProcessingTokenizer& streamIterator, ParameterBox& defaults);
	void AppendElement(PredefinedCBLayout& cbLayout, const PredefinedCBLayout::NameAndType& input, unsigned cbIterator[PredefinedCBLayout::AlignmentRules_Max]);
	static SamplerDesc ParseFixedSampler(ConditionalProcessingTokenizer& streamIterator);

	void PredefinedDescriptorSetLayout::ParseSlot(ConditionalProcessingTokenizer& iterator, DescriptorType type)
	{
		PredefinedDescriptorSetLayout::ConditionalDescriptorSlot result;

		result._conditions = iterator._preprocessorContext.GetCurrentConditionString();

		auto layoutName = iterator.GetNextToken();
		if (layoutName._value.IsEmpty())
			Throw(FormatException("Expecting identifier after type keyword", layoutName._start));

		result._name = layoutName._value.AsString();
		result._type = type;

		auto token = iterator.GetNextToken();
		if (XlEqString(token._value, "[")) {
			auto countToken = iterator.GetNextToken();
			if (XlEqString(countToken._value, "]"))
				Throw(FormatException("Expecting expecting array count, but got empty array brackets", token._start));

			auto* parseEnd = FastParseValue(countToken._value, result._arrayElementCount);
			if (parseEnd != countToken._value.end())
				Throw(FormatException(StringMeld<256>() << "Expecting unsigned integer value for array count, but got " << countToken._value, token._start));

			auto closeBracket = iterator.GetNextToken();
			if (!XlEqString(closeBracket._value, "]"))
				Throw(FormatException(StringMeld<256>() << "Expecting expecting closing bracket for array, but got " << closeBracket._value, token._start));

			token = iterator.GetNextToken();
		}

		if (XlEqString(token._value, ":")) {
			token = iterator.GetNextToken();

			const char* parseEnd = FastParseValue(token._value, result._slotIdx);
			if (parseEnd != token._value.end())
				Throw(FormatException(StringMeld<256>() << "Expecting integer slot index value, but got " << token._value, token._start));

			token = iterator.GetNextToken();
		}

		if ((type == DescriptorType::UniformBuffer || type == DescriptorType::UniformBufferDynamicOffset) && XlEqString(token._value, "{")) {
			auto newLayout = std::make_shared<PredefinedCBLayout>();
			unsigned currentLayoutCBIterator[PredefinedCBLayout::AlignmentRules_Max] = { 0, 0, 0 };

			for (;;) {
				auto next = iterator.PeekNextToken();
				if (next._value.IsEmpty())
					Throw(FormatException(StringMeld<256>() << "Unexpected end of file while parsing layout for (" << result._name << ") at " << next._value, next._start));

				if (XlEqString(next._value, "}")) {
					iterator.GetNextToken();		// (advance over the })
					token = iterator.GetNextToken();
					break;
				}

				auto parsed = ParseStatement(iterator, newLayout->_defaults);
				AppendElement(*newLayout, parsed, currentLayoutCBIterator);
			}

			for (unsigned c=0; c<dimof(newLayout->_cbSizeByLanguage); ++c)
				newLayout->_cbSizeByLanguage[c] = CeilToMultiplePow2(currentLayoutCBIterator[c], 16);

			_constantBuffers.push_back(newLayout);
			result._cbIdx = (unsigned)_constantBuffers.size() - 1;
		} else if (type == DescriptorType::Sampler && XlEqString(token._value, "{")) {
			auto fixedSampler = ParseFixedSampler(iterator);

			token = iterator.GetNextToken();
			if (token._value.IsEmpty())
				Throw(FormatException(StringMeld<256>() << "Unexpected end of file while parsing fixed sampler for (" << result._name << ") at " << token._value, token._start));
			assert(XlEqString(token._value, "}"));
			token = iterator.GetNextToken();		

			_fixedSamplers.push_back(fixedSampler);
			result._fixedSamplerIdx = (unsigned)_fixedSamplers.size() - 1;
		}

		if (!XlEqString(token._value, ";"))
			Throw(FormatException(StringMeld<256>() << "Expecting ; after resource, but got " << token._value, token._start));

		_slots.push_back(result);
	}

	// We want to configure the descriptor set layout slot types using this file, which is
	// not exactly the same as the HLSL/GLSL object types
	//
	// Descriptor set slot types:
	//		* Sampler
	//		* Texture					-> VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE
	//		* ConstantBuffer			-> VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER
	//		* UnorderedAccessTexture	-> VK_DESCRIPTOR_TYPE_STORAGE_IMAGE
	//		* UnorderedAccessBuffer		-> VK_DESCRIPTOR_TYPE_STORAGE_BUFFER
	//
	// Vulkan types not accessible:
	//		* VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER
	//		* VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER
	//		* VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER
	//		* VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT
	//		* (other extension types)
	//
	// HLSL object types:
	//		* StructuredBuffer			-> UnorderedAccessBuffer
	//		* AppendStructuredBuffer	-> UnorderedAccessBuffer
	//		* Buffer					-> UnorderedAccessBuffer
	//		* ByteAddressBuffer			-> UnorderedAccessBuffer
	//		* ConsumeStructuredBuffer	-> UnorderedAccessBuffer
	//		* RWBuffer					-> UnorderedAccessBuffer
	//		* RWByteAddressBuffer		-> UnorderedAccessBuffer
	//		* RWTexture1D				-> UnorderedAccessTexture
	//		* RWTexture1DArray			-> UnorderedAccessTexture
	//		* RWTexture2D				-> UnorderedAccessTexture
	//		* RWTexture2DArray			-> UnorderedAccessTexture
	//		* RWTexture3D				-> UnorderedAccessTexture
	//		* Texture1D					-> Texture
	//		* Texture1DArray			-> Texture
	//		* Texture2D					-> Texture
	//		* Texture2DArray			-> Texture
	//		* Texture3D					-> Texture
	//		* TextureCube				-> Texture
	//		* TextureCubeArray			-> Texture
	//		* also tbuffer				-> (not supported)
	//		* cbuffer					-> ConstantBuffer
	//		* 5.1 also has RasterizerOrderedXXXX types
	//
	// In HLSL, we can add "<>" style template arguments after the object to specify pixel formats
	//
	// GLSL object types: ('g' prefix is replaced with nothing, 'i' or 'u')
	//		* uniform sampler
	//		* buffer
	//		* gimage1D
	//		* gimage2D
	//		* gimage3D
	//		* gimageCube
	//		* gimage1DArray
	//		* gimage2DArray
	//		* gimageCubeArray
	//		* gimageBuffer
	//		* gimage2DMS
	//		* gimage2DMSArray
	//
	// In GLSL, format information can be added in the layout() block. Also memory qualifiers (coherent,
	// volatile, restrict, readonly & writeonly) can preceed the type name. We can't tell if a buffer or
	// texture is a storage type or sampled/uniform type simply from the name.
	//
	// It seems like it would be too confusing to attempt to use the same names from either the HLSL or 
	// GLSL object types for the descriptor slot type names.
	// 		* for one, the object type names are more descriptive have the descriptor slot types, so
	//		  it would be unclear exactly qhich qualifier are important and what aren't (ie, is it clear 
	// 		  that you can use the same slot for a Texture2D and a Texture3D?)
	//		* also both HLSL and GLSL have pretty awkward type names that have evolved in a clunky way
	//
	// Really, buffers can only have 2 qualifiers: <uniform> or <unordered-access> and textures can have 
	// only 2 qualifiers: <sampled> and <unordered-access>. Can there only a few root types: buffers, textures 
	// (or images), samplers, texel buffers. So we should build our naming scheme around that scheme of
	// qualifier and root type
	//
	static std::pair<StringSection<>, DescriptorType> s_descriptorTypeNames[] = {
		std::make_pair("SampledTexture", 			DescriptorType::SampledTexture),
		std::make_pair("SampledImage", 				DescriptorType::SampledTexture),
		std::make_pair("UniformBuffer", 			DescriptorType::UniformBuffer),
		std::make_pair("ConstantBuffer", 			DescriptorType::UniformBuffer),
		std::make_pair("UnorderedAccessTexture", 	DescriptorType::UnorderedAccessTexture),
		std::make_pair("UnorderedAccessBuffer", 	DescriptorType::UnorderedAccessBuffer),
		std::make_pair("StorageImage", 				DescriptorType::UnorderedAccessTexture),
		std::make_pair("StorageBuffer", 			DescriptorType::UnorderedAccessBuffer),
		std::make_pair("StorageTexelBuffer", 			DescriptorType::UnorderedAccessTexelBuffer),
		std::make_pair("UnorderedAccessTexelBuffer", 	DescriptorType::UnorderedAccessTexelBuffer),
		std::make_pair("UniformTexelBuffer", 			DescriptorType::UniformTexelBuffer),
		std::make_pair("UniformBufferDynamic", 			DescriptorType::UniformBufferDynamicOffset),
		std::make_pair("UnordererdAccessBufferDynamic", DescriptorType::UnorderedAccessBufferDynamicOffset),
		std::make_pair("StorageBufferDynamic", 			DescriptorType::UnorderedAccessBufferDynamicOffset),
		std::make_pair("Sampler", 					DescriptorType::Sampler),
		std::make_pair("SubpassInput", 				DescriptorType::InputAttachment)
	};

	void PredefinedDescriptorSetLayout::Parse(ConditionalProcessingTokenizer& iterator)
	{
		//
		//  Parse through thes input data line by line.
		//  If we find lines beginning with preprocessor command, we should pass them through
		//  the PreprocessorParseContext
		//
		//  Note that we don't support line appending syntax (eg, back-slash and then a newline)
		//      -- that just requires a bunch of special cases, and doesn't seem like it's
		//      worth the hassle.
		//  Also preprocessor symbols must be at the start of the line, or at least preceeded only
		//  by whitespace (same as C/CPP)
		//

		for (;;) {
			auto token = iterator.PeekNextToken();
			if (token._value.IsEmpty() || XlEqString(token._value, "}"))
				break;

			iterator.GetNextToken();

			auto i = std::find_if(s_descriptorTypeNames, &s_descriptorTypeNames[dimof(s_descriptorTypeNames)],
				[&token](const auto& c) { return XlEqString(c.first, token._value); });
			if (i != &s_descriptorTypeNames[dimof(s_descriptorTypeNames)]) {
				ParseSlot(iterator, i->second);
			} else {
				StringMeld<4096> meld;
				meld << "Unknown identifier (" << token._value << "). Expecting one of the following: ";
				for (unsigned c=0; c<dimof(s_descriptorTypeNames); ++c) {
					if (c != 0) meld << ", ";
					meld << s_descriptorTypeNames[c].first;
				}
				Throw(FormatException(meld, token._start));
			}
		}

		// fill in slot indices where they weren't explicitly specified
		int lastAssignedSlot = -1;
		for (auto& slot:_slots) {
			if (slot._slotIdx == ~0u) {
				auto slotIdx = lastAssignedSlot+1;
				for (;;) {
					bool foundOverlap = false;
					for (auto& slot:_slots)
						if (slot._slotIdx == slotIdx) {
							foundOverlap = true;
							slotIdx++;
							break;
						}
					if (!foundOverlap) break;
				}
				slot._slotIdx = lastAssignedSlot = slotIdx;
			}
		}
	}

	PredefinedDescriptorSetLayout::PredefinedDescriptorSetLayout(
		ConditionalProcessingTokenizer& iterator,
		const ::Assets::DependencyValidation& depVal)
	: _depVal(depVal)
	{
		Parse(iterator);
	}

	PredefinedDescriptorSetLayout::PredefinedDescriptorSetLayout(
		StringSection<> inputData,
		const ::Assets::DirectorySearchRules& searchRules,
		const ::Assets::DependencyValidation& depVal)
	: _depVal(depVal)
	{
		::Assets::PreprocessorIncludeHandler includeHandler;
		ConditionalProcessingTokenizer iterator{inputData, searchRules.GetBaseFile(), &includeHandler};
		Parse(iterator);
		if (!iterator.Remaining().IsEmpty())
			Throw(FormatException("Additional tokens found, expecting end of file", iterator.GetLocation()));
	}

	uint64_t PredefinedDescriptorSetLayout::CalculateHash(uint64_t seed) const
	{
		uint64_t result = seed;
		for (const auto& slot:_slots) {
			result = Hash64(slot._name, result);
			result = HashCombine(result, slot._slotIdx);
			if (!slot._conditions.empty())
				result = Hash64(slot._conditions, result);
			result = HashCombine(uint64_t(slot._type) | (uint64_t(slot._arrayElementCount) << 16ull), result);
			if (slot._cbIdx != ~0u)
				result = HashCombine(_constantBuffers[slot._cbIdx]->CalculateHash(), result);
		}
		return result;
	}

	PredefinedDescriptorSetLayout::PredefinedDescriptorSetLayout() {}
	PredefinedDescriptorSetLayout::~PredefinedDescriptorSetLayout() {}

	DescriptorSetSignature PredefinedDescriptorSetLayout::MakeDescriptorSetSignature(SamplerPool* samplerPool) const
	{
		int maxSlotIdx = -1;
		for (const auto&s:_slots) {
			assert(s._slotIdx != ~0u);
			maxSlotIdx = std::max(maxSlotIdx, int(s._slotIdx));
		}
		if (maxSlotIdx<0) return {};

		DescriptorSetSignature result;
		result._slots.resize(maxSlotIdx+1);
		result._slotNames.resize(maxSlotIdx+1, 0ull);

		for (const auto&s:_slots) {
			if (result._slotNames[s._slotIdx] == 0ull) {
				auto count = std::max(s._arrayElementCount, 1u);
				result._slots[s._slotIdx] = DescriptorSlot{s._type, count};
				result._slotNames[s._slotIdx] = Hash64(s._name);
			}
		}
		if (samplerPool && !_fixedSamplers.empty()) {
			result._fixedSamplers.resize(maxSlotIdx+1);
			for (unsigned c=0; c<_slots.size(); ++c) {
				if (_slots[c]._fixedSamplerIdx == ~0u) continue;
				result._fixedSamplers[_slots[c]._slotIdx] = samplerPool->GetSampler(_fixedSamplers[_slots[c]._fixedSamplerIdx]);
			}
		}
		return result;
	}

	SamplerDesc ParseFixedSampler(ConditionalProcessingTokenizer& iterator)
	{
		#if defined(_DEBUG)
			auto initialCondition = iterator._preprocessorContext.GetCurrentConditionString();
		#endif

		char exceptionBuffer[256];
		SamplerDesc result{};
		for (;;) {
			auto next = iterator.PeekNextToken();
			if (next._value.IsEmpty() || XlEqString(next._value, "}"))
				break;
			iterator.GetNextToken();

			#if defined(_DEBUG)
				assert(iterator._preprocessorContext.GetCurrentConditionString() == initialCondition); // can't support preprocessor conditions changing mid-sampler
			#endif

			if (XlEqString(next._value, "Filter")) {
				if (!XlEqString(iterator.GetNextToken()._value, "="))
					Throw(FormatException("Expecting '=' after field in sampler desc", iterator.GetLocation()));
				next = iterator.GetNextToken();
				auto filterMode = AsFilterMode(next._value);
				if (!filterMode)
					Throw(FormatException(StringMeldInPlace(exceptionBuffer) << "Unknown filter mode (" << next._value << ")", iterator.GetLocation()));
				result._filter = filterMode.value();
			} else if (XlEqString(next._value, "AddressU") || XlEqString(next._value, "AddressV") || XlEqString(next._value, "AddressW") ) {
				auto prop = next._value;
				if (!XlEqString(iterator.GetNextToken()._value, "="))
					Throw(FormatException("Expecting '=' after field in sampler desc", iterator.GetLocation()));
				next = iterator.GetNextToken();
				auto addressMode = AsAddressMode(next._value);
				if (!addressMode)
					Throw(FormatException(StringMeldInPlace(exceptionBuffer) << "Unknown address mode (" << next._value << ")", iterator.GetLocation()));
				if (XlEqString(prop, "AddressU")) result._addressU = addressMode.value();
				if (XlEqString(prop, "AddressV")) result._addressV = addressMode.value();
				else result._addressW = addressMode.value();
			} else if (XlEqString(next._value, "Comparison")) {
				if (!XlEqString(iterator.GetNextToken()._value, "="))
					Throw(FormatException("Expecting '=' after field in sampler desc", iterator.GetLocation()));
				next = iterator.GetNextToken();
				auto compareMode = AsCompareOp(next._value);
				if (!compareMode)
					Throw(FormatException(StringMeldInPlace(exceptionBuffer) << "Unknown comparison mode (" << next._value << ")", iterator.GetLocation()));
				result._comparison = compareMode.value();
			} else {
				auto flag = AsSamplerDescFlag(next._value);
				if (!flag)
					Throw(FormatException(StringMeldInPlace(exceptionBuffer) << "Unknown sampler field (" << next._value << ")", iterator.GetLocation()));
				result._flags |= flag.value();
			}

			next = iterator.PeekNextToken();
			if (next._value.IsEmpty() || XlEqString(next._value, "}"))
				break;
			if (!XlEqString(next._value, ","))
				Throw(FormatException(StringMeldInPlace(exceptionBuffer) << "Expecting comma between values in sampler declaration", iterator.GetLocation()));
			iterator.GetNextToken();
		}

		return result;
	}

}}

