// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "PredefinedPipelineLayout.h"
#include "PredefinedDescriptorSetLayout.h"
#include "PredefinedCBLayout.h"
#include "../UniformsStream.h"
#include "../ResourceUtils.h"
#include "../../Assets/DepVal.h"
#include "../../Assets/AssetUtils.h"
#include "../../Assets/PreprocessorIncludeHandler.h"
#include "../../Assets/Assets.h"
#include "../../Utility/StringUtils.h"
#include "../../Utility/StringFormat.h"
#include "../../Utility/FastParseValue.h"
#include "../../Utility/MemoryUtils.h"
#include "../../Utility/Streams/PreprocessorInterpreter.h"
#include "../../Utility/Streams/ConditionalPreprocessingTokenizer.h"
#include "../../Utility/Streams/PathUtils.h"

namespace RenderCore { namespace Assets
{

	auto PredefinedPipelineLayoutFile::ParsePipelineLayout(ConditionalProcessingTokenizer& iterator) -> std::shared_ptr<PipelineLayout>
	{
		auto result = std::make_shared<PipelineLayout>();
		for (;;) {
			auto next = iterator.PeekNextToken();
			if (next._value.IsEmpty())
				Throw(FormatException("Unexpected end of file while parsing layout at" , next._start));

			if (XlEqString(next._value, "}")) {
				break;
			}

			iterator.GetNextToken();	// skip over what we peeked

			if (XlEqString(next._value, "DescriptorSet") || XlEqString(next._value, "GraphicsDescriptorSet") || XlEqString(next._value, "ComputeDescriptorSet") || XlEqString(next._value, "AutoDescriptorSet")) {
				auto pipelineType = PipelineType::Graphics;
				if (XlEqString(next._value, "ComputeDescriptorSet"))
					pipelineType = PipelineType::Compute;
				bool isAuto = XlEqString(next._value, "AutoDescriptorSet");
					
				auto name = iterator.GetNextToken();
				auto semi = iterator.GetNextToken();
				if (name._value.IsEmpty() || !XlEqString(semi._value, ";"))
					Throw(FormatException("Expecting identifier name and then ;", name._start));

				// lookup this descriptor set in list of already registered descriptor sets
				auto i = _descriptorSets.find(name._value.AsString());
				if (i == _descriptorSets.end())
					Throw(FormatException(StringMeld<256>() << "Descriptor set with the name (" << name._value << ") has not been declared", name._start));

				result->_descriptorSets.push_back({name._value.AsString(), i->second, pipelineType, isAuto});
			} else if (XlEqString(next._value, "VSPushConstants") 
					|| XlEqString(next._value, "PSPushConstants") 
					|| XlEqString(next._value, "GSPushConstants")
					|| XlEqString(next._value, "CSPushConstants")) {

				auto name = iterator.GetNextToken();
				auto openBrace = iterator.GetNextToken();
				if (name._value.IsEmpty() || !XlEqString(openBrace._value, "{"))
					Throw(FormatException("Expecting identifier name and then {", name._start));

				auto newLayout = std::make_shared<PredefinedCBLayout>(iterator, _depVal);

				if (XlEqString(next._value, "VSPushConstants")) {
					if (result->_vsPushConstants.second)
						Throw(FormatException("Multiple VS push constant buffers declared. Only one is supported", next._start));
					result->_vsPushConstants = std::make_pair(name._value.AsString(), newLayout);
				} else if (XlEqString(next._value, "PSPushConstants")) {
					if (result->_psPushConstants.second)
						Throw(FormatException("Multiple PS push constant buffers declared. Only one is supported", next._start));
					result->_psPushConstants = std::make_pair(name._value.AsString(), newLayout);
				} else if (XlEqString(next._value, "CSPushConstants")) {
					if (result->_csPushConstants.second)
						Throw(FormatException("Multiple CS push constant buffers declared. Only one is supported", next._start));
					result->_csPushConstants = std::make_pair(name._value.AsString(), newLayout);
				} else {
					assert(XlEqString(next._value, "GSPushConstants"));
					if (result->_gsPushConstants.second)
						Throw(FormatException("Multiple GS push constant buffers declared. Only one is supported", next._start));
					result->_gsPushConstants = std::make_pair(name._value.AsString(), newLayout);
				}

				auto closeBrace = iterator.GetNextToken();
				auto semi = iterator.GetNextToken();
				if (!XlEqString(closeBrace._value, "}") || !XlEqString(semi._value, ";"))
					Throw(FormatException("Expecting } and then ;", closeBrace._start));
			} else
				Throw(FormatException("Expecting DescriptorSet, GraphicsDescriptorSet, ComputeDescriptorSet, AutoDescriptorSet, VSPushConstants, PSPushConstants, GSPushConstants or CSPushConstants", iterator.GetLocation()));
		}

		return result;
	}

	void PredefinedPipelineLayoutFile::Parse(Utility::ConditionalProcessingTokenizer& tokenizer)
	{
		for (;;) {
			auto token = tokenizer.GetNextToken();
			if (token._value.IsEmpty())
				break;

			if (XlEqString(token._value, "DescriptorSet") || XlEqString(token._value, "PipelineLayout")) {

				auto conditions = tokenizer._preprocessorContext.GetCurrentConditionString();
				if (!conditions.empty())
					Throw(FormatException("Preprocessor conditions are not supported wrapping a descriptor set or pipeline layout entry", tokenizer.GetLocation()));

				auto name = tokenizer.GetNextToken();
				auto openBrace = tokenizer.GetNextToken();
				if (name._value.IsEmpty() || !XlEqString(openBrace._value, "{"))
					Throw(FormatException("Expecting identifier name and then {", name._start));

				if (XlEqString(token._value, "DescriptorSet")) {
					auto existing = _descriptorSets.find(name._value.AsString());
					if (existing != _descriptorSets.end())
						Throw(FormatException(StringMeld<256>() << "Descriptor set with name (" << name._value << ") declared multiple times", name._start));

					auto newLayout = std::make_shared<PredefinedDescriptorSetLayout>(tokenizer, _depVal);
					_descriptorSets.insert(std::make_pair(name._value.AsString(), newLayout));
				} else {
					assert(XlEqString(token._value, "PipelineLayout"));
					auto existing = _pipelineLayouts.find(name._value.AsString());
					if (existing != _pipelineLayouts.end())
						Throw(FormatException(StringMeld<256>() << "Pipeline layout with name (" << name._value << ") declared multiple times", name._start));

					auto newLayout = ParsePipelineLayout(tokenizer);
					_pipelineLayouts.insert(std::make_pair(name._value.AsString(), newLayout));
				}

				auto closeBrace = tokenizer.GetNextToken();
				auto semi = tokenizer.GetNextToken();
				if (!XlEqString(closeBrace._value, "}") || !XlEqString(semi._value, ";"))
					Throw(FormatException("Expecting } and then ;", closeBrace._start));

			} else {
				Throw(FormatException(StringMeld<256>() << "Expecting either 'DescriptorSet' or 'PipelineLayout' keyword, but got " << token._value, token._start));
			}
		}

		if (!tokenizer.Remaining().IsEmpty())
			Throw(FormatException("Additional tokens found, expecting end of file", tokenizer.GetLocation()));
	}

	PredefinedPipelineLayoutFile::PredefinedPipelineLayoutFile(
		StringSection<> inputData,
		const ::Assets::DirectorySearchRules& searchRules,
		const ::Assets::DependencyValidation& depVal)
	: _depVal(depVal)
	{
		::Assets::PreprocessorIncludeHandler includeHandler;
		ConditionalProcessingTokenizer tokenizer(inputData, searchRules.GetBaseFile(), &includeHandler);
		Parse(tokenizer);
	}

	PredefinedPipelineLayoutFile::PredefinedPipelineLayoutFile(StringSection<> sourceFileName)
	{
		::Assets::PreprocessorIncludeHandler includeHandler;
		TRY {
			auto initialFile = includeHandler.OpenFile(sourceFileName, {});
			ConditionalProcessingTokenizer tokenizer(
				MakeStringSection((const char*)initialFile._fileContents.get(), (const char*)PtrAdd(initialFile._fileContents.get(), initialFile._fileContentsSize)),
				initialFile._filename,
				&includeHandler);
			Parse(tokenizer);
			_depVal = includeHandler.MakeDependencyValidation();
			for (auto& layout:_descriptorSets)		// have to assign the parsed layout's depvals, as well -- since we don't generate this until the end of parsing
				layout.second->_depVal = _depVal;
		} CATCH(const std::exception& e) {
			Throw(::Assets::Exceptions::ConstructionError(e, includeHandler.MakeDependencyValidation()));
		} CATCH(const ::Assets::Exceptions::ConstructionError& e) {
			Throw(::Assets::Exceptions::ConstructionError(e, includeHandler.MakeDependencyValidation()));
		} CATCH_END
	}

	PredefinedPipelineLayoutFile::PredefinedPipelineLayoutFile() {}
	PredefinedPipelineLayoutFile::~PredefinedPipelineLayoutFile() {}

	static DescriptorSetSignature BuildAutoDescriptorSet(
		IteratorRange<const DescriptorSetSignature**> autoSignatures,
		const PredefinedDescriptorSetLayout* predefinedLayout,
		SamplerPool* samplerPool)
	{
		DescriptorSetSignature result;

		size_t slotCount = 0;
		for (auto&sig:autoSignatures) slotCount = std::max(slotCount, sig->_slots.size());
		result._slots.resize(slotCount);
		result._slotNames.resize(slotCount, 0);

		char exceptionBuffer[256];
		for (auto&sig:autoSignatures) {
			for (unsigned slotIdx=0; slotIdx<sig->_slots.size(); ++slotIdx) {
				if (sig->_slots[slotIdx]._type == DescriptorType::Empty) continue;
				// For slots that are filled in, all signatures must agree. You may hit an exception here, if (for example), a slot is
				// used with one type in the pixel shader, but another type in the vertex shader. This doesn't work because with graphics
				// pipelines there is one descriptor set that is applied to be used by all shaders in the pipeline
				if (result._slots[slotIdx]._type != DescriptorType::Empty && result._slots[slotIdx]._type != sig->_slots[slotIdx]._type) {
					StringMeldInPlace(exceptionBuffer) << "Cannot build auto descriptor set because descriptor slot (" << slotIdx << ") do not agree. (" << AsString(sig->_slots[slotIdx]._type) << " vs " << AsString(result._slots[slotIdx]._type) << ")";
					Throw(std::runtime_error(exceptionBuffer));
				}
				if (result._slots[slotIdx]._type != DescriptorType::Empty && result._slots[slotIdx]._count != sig->_slots[slotIdx]._count) {
					StringMeldInPlace(exceptionBuffer) << "Cannot build auto descriptor set because array count for descriptor slot (" << slotIdx << ") do not agree). (" << sig->_slots[slotIdx]._count << " vs " << result._slots[slotIdx]._count << ")";
					Throw(std::runtime_error(exceptionBuffer));
				}
				result._slots[slotIdx] = sig->_slots[slotIdx];
				result._slotNames[slotIdx] = sig->_slotNames[slotIdx];
			}

			assert(sig->_fixedSamplers.empty());		// we ignore fixed samplers in signatures from reflection data
		}

		if (predefinedLayout) {
			for (unsigned s=0; s<result._slots.size(); ++s) {
				if (result._slots[s]._type == DescriptorType::Sampler) {
					// look for a fixed sampler in the predefined layout
					auto name = result._slotNames[s];
					auto i = std::find_if(predefinedLayout->_slots.begin(), predefinedLayout->_slots.end(), [name](const auto& c) { return Hash64(c._name) == name; });
					if (i != predefinedLayout->_slots.end() && i->_fixedSamplerIdx != ~0u) {
						if (result._fixedSamplers.size() <= s)
							result._fixedSamplers.resize(s+1);
						result._fixedSamplers[s] = samplerPool->GetSampler(predefinedLayout->_fixedSamplers[i->_fixedSamplerIdx]);
					}
				}
			}
		}

		return result;
	}

	PipelineLayoutInitializer PredefinedPipelineLayout::MakePipelineLayoutInitializerInternal(
		IteratorRange<const PipelineLayoutInitializer**> autoInitializers,
		ShaderLanguage language, SamplerPool* samplerPool) const
	{
		unsigned descSetCount = 0;
		if (autoInitializers.empty()) { 
			descSetCount = _descriptorSets.size();
		} else {
			for (auto& sig:autoInitializers) descSetCount = std::max(unsigned(sig->GetDescriptorSets().size()), descSetCount);
		}
		PipelineLayoutInitializer::DescriptorSetBinding descriptorSetBindings[descSetCount];
		std::vector<const DescriptorSetSignature*> descSetSigs;
		descSetSigs.reserve(autoInitializers.size());
		size_t c=0;
		for (; c<_descriptorSets.size() && c<descSetCount; ++c) {
			descriptorSetBindings[c]._name = _descriptorSets[c]._name;
			descriptorSetBindings[c]._pipelineType = _descriptorSets[c]._pipelineType;
			if (_descriptorSets[c]._isAuto) {
				if (autoInitializers.empty())
					Throw(std::runtime_error("Pipeline layout has auto descriptor sets and cannot be used without reflection information from the shader"));

				descSetSigs.clear();
				for (auto&sig:autoInitializers) if (c < sig->GetDescriptorSets().size()) descSetSigs.push_back(&sig->GetDescriptorSets()[c]._signature);
				if (!descSetSigs.empty()) {
					descriptorSetBindings[c]._signature = BuildAutoDescriptorSet(MakeIteratorRange(descSetSigs), _descriptorSets[c]._descSet.get(), samplerPool);
				} else {
					// shader doesn't actually use anything from this descriptor set, we'll just keep the signature blank
				}
			} else {
				descriptorSetBindings[c]._signature = _descriptorSets[c]._descSet->MakeDescriptorSetSignature(samplerPool);
			}
		}
		if (!autoInitializers.empty()) {
			// If the shader requires some descriptor sets that aren't in the predefined layout, we'll include those here
			for (; c<descSetCount; ++c) {
				descSetSigs.clear();
				for (auto&sig:autoInitializers) if (c < sig->GetDescriptorSets().size()) descSetSigs.push_back(&sig->GetDescriptorSets()[c]._signature);
				assert(!descSetSigs.empty());
				descriptorSetBindings[c]._signature = BuildAutoDescriptorSet(MakeIteratorRange(descSetSigs), nullptr, samplerPool);
			}

			// If there's at least one auto descriptor set, we'll reset all of the pipeline types to the pipeline type expected
			// by the auto initializer. The pipeline type must be consistant across the entire pipeline layout, after all
			std::optional<PipelineType> autoPipelineType;
			for (auto& sig:autoInitializers)
				for (auto& descSet:sig->GetDescriptorSets()) {
					if (autoPipelineType.has_value() && *autoPipelineType != descSet._pipelineType)
						Throw(std::runtime_error("Cannot build pipeline layout with auto descriptor sets because the pipeline types of the auto descriptor sets do not agree"));
					autoPipelineType = descSet._pipelineType;
				}
			if (autoPipelineType.has_value())
				for (unsigned c=0; c<descSetCount; ++c)
					descriptorSetBindings[c]._pipelineType = *autoPipelineType;
		}

		PipelineLayoutInitializer::PushConstantsBinding pushConstantBindings[3];
		unsigned pushConstantBindingsCount = 0;

		if (!autoInitializers.empty()) {
			// also just defer to the autoInitializer for push constant initializers
			for (auto& sig:autoInitializers)
				for (auto& c:sig->GetPushConstants()) {
					if (pushConstantBindingsCount >= dimof(pushConstantBindings))
						Throw(std::runtime_error("Too many push constant bindings from auto descriptor sets while building pipeline layout"));
					pushConstantBindings[pushConstantBindingsCount++] = c;
				}
		} else {
			if (_vsPushConstants.second) {
				auto& binding = pushConstantBindings[pushConstantBindingsCount++];
				binding._name = _vsPushConstants.first;
				binding._shaderStage = ShaderStage::Vertex;
				binding._cbSize = _vsPushConstants.second->GetSize_NoPostfix(language);		// don't align end to a vector boundary
				binding._cbElements = _vsPushConstants.second->MakeConstantBufferElements(language);
			}

			if (_psPushConstants.second) {
				auto& binding = pushConstantBindings[pushConstantBindingsCount++];
				binding._name = _psPushConstants.first;
				binding._shaderStage = ShaderStage::Pixel;
				binding._cbSize = _psPushConstants.second->GetSize_NoPostfix(language);		// don't align end to a vector boundary
				binding._cbElements = _psPushConstants.second->MakeConstantBufferElements(language);
			}

			if (_gsPushConstants.second) {
				auto& binding = pushConstantBindings[pushConstantBindingsCount++];
				binding._name = _gsPushConstants.first;
				binding._shaderStage = ShaderStage::Geometry;
				binding._cbSize = _gsPushConstants.second->GetSize_NoPostfix(language);		// don't align end to a vector boundary
				binding._cbElements = _gsPushConstants.second->MakeConstantBufferElements(language);
			}
			if (_csPushConstants.second) {
				auto& binding = pushConstantBindings[pushConstantBindingsCount++];
				binding._name = _csPushConstants.first;
				binding._shaderStage = ShaderStage::Compute;
				binding._cbSize = _csPushConstants.second->GetSize_NoPostfix(language);		// don't align end to a vector boundary
				binding._cbElements = _csPushConstants.second->MakeConstantBufferElements(language);
			}
			assert(pushConstantBindingsCount <= dimof(pushConstantBindings));
		}

		return PipelineLayoutInitializer {
			MakeIteratorRange(descriptorSetBindings, &descriptorSetBindings[descSetCount]),
			MakeIteratorRange(pushConstantBindings, &pushConstantBindings[pushConstantBindingsCount])};
	}

	PipelineLayoutInitializer PredefinedPipelineLayout::MakePipelineLayoutInitializer(ShaderLanguage language, SamplerPool* samplerPool) const
	{
		return MakePipelineLayoutInitializerInternal({}, language, samplerPool);
	}

	PipelineLayoutInitializer PredefinedPipelineLayout::MakePipelineLayoutInitializerWithAutoMatching(
		const PipelineLayoutInitializer& autoInitializer,
		ShaderLanguage language, SamplerPool* samplerPool) const
	{
		const PipelineLayoutInitializer* inits[] = {&autoInitializer};
		return MakePipelineLayoutInitializerInternal(MakeIteratorRange(inits), language, samplerPool);
	}

	PipelineLayoutInitializer PredefinedPipelineLayout::MakePipelineLayoutInitializerWithAutoMatching(
		IteratorRange<const PipelineLayoutInitializer**> autoInitializers,
		ShaderLanguage language, SamplerPool* samplerPool) const
	{
		return MakePipelineLayoutInitializerInternal(autoInitializers, language, samplerPool);
	}

	const PredefinedDescriptorSetLayout* PredefinedPipelineLayout::FindDescriptorSet(StringSection<> name) const
	{
		for (const auto& d:_descriptorSets)
			if (XlEqString(name, d._name))
				return d._descSet.get();
		return nullptr;
	}

	bool PredefinedPipelineLayout::HasAutoDescriptorSets() const
	{
		for (const auto& descSet:_descriptorSets)
			if (descSet._isAuto) return true;
		return false;
	}

	uint64_t PredefinedPipelineLayout::CalculateHash(uint64_t seed) const
	{
		auto result = seed;
		for (const auto&ds:_descriptorSets) {
			result = Hash64(ds._name, result);
			result = ds._descSet->CalculateHash(result);
			result = rotl64(result, unsigned(ds._pipelineType));
			if (ds._isAuto)
				result = ~result;			
		}
		if (_vsPushConstants.second) {
			result = Hash64(_vsPushConstants.first, result);
			result = _vsPushConstants.second->CalculateHash(result);
		}
		if (_psPushConstants.second) {
			result = Hash64(_psPushConstants.first, result);
			result = _psPushConstants.second->CalculateHash(result);
		}
		if (_gsPushConstants.second) {
			result = Hash64(_gsPushConstants.first, result);
			result = _gsPushConstants.second->CalculateHash(result);
		}
		if (_csPushConstants.second) {
			result = Hash64(_csPushConstants.first, result);
			result = _csPushConstants.second->CalculateHash(result);
		}
		return result;
	}

	PredefinedPipelineLayout::PredefinedPipelineLayout(
		const PredefinedPipelineLayoutFile& srcFile,
		std::string name)
	{
		auto i = srcFile._pipelineLayouts.find(name);
		if (i == srcFile._pipelineLayouts.end())
			Throw(::Assets::Exceptions::ConstructionError(
				::Assets::Exceptions::ConstructionError::Reason::MissingFile,
				srcFile.GetDependencyValidation(),
				"No pipeline layout entry with the name (%s)", name.c_str()));

		if (!i->second->_descriptorSets.empty()) {
			_descriptorSets.reserve(i->second->_descriptorSets.size());
			std::optional<PipelineType> pipelineType;
			for (const auto& d:i->second->_descriptorSets) {
				if (!d._isAuto) {
					if (pipelineType && d._pipelineType != pipelineType.value())
						Throw(::Assets::Exceptions::ConstructionError(
							::Assets::Exceptions::ConstructionError::Reason::FormatNotUnderstood,
							srcFile.GetDependencyValidation(),
							"Mixing multiple pipeline types (compute/graphics) in pipeline layout"));
					pipelineType = d._pipelineType;
				}
				_descriptorSets.push_back(d);
			}
		}
		_vsPushConstants = i->second->_vsPushConstants;
		_psPushConstants = i->second->_psPushConstants;
		_gsPushConstants = i->second->_gsPushConstants;
		_csPushConstants = i->second->_csPushConstants;
		_depVal = srcFile.GetDependencyValidation();
	}

	void PredefinedPipelineLayout::ConstructToPromise(
		std::promise<std::shared_ptr<PredefinedPipelineLayout>>&& promise,
		StringSection<::Assets::ResChar> src)
	{
		auto split = MakeFileNameSplitter(src);
		if (split.Parameters().IsEmpty())
			Throw(std::runtime_error("Missing pipeline layout name when loading pipeline layout (expecting <filename>:<layout name>). For request: " + src.AsString()));
		auto fileFuture = ::Assets::MakeAssetPtr<PredefinedPipelineLayoutFile>(split.AllExceptParameters());
		::Assets::WhenAll(fileFuture).ThenConstructToPromise(
			std::move(promise),
			[layoutName=split.Parameters().AsString()](auto file) {
				return std::make_shared<PredefinedPipelineLayout>(*file, layoutName);
			});
		
	}

}}

