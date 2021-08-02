// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "PredefinedPipelineLayout.h"
#include "PredefinedDescriptorSetLayout.h"
#include "PredefinedCBLayout.h"
#include "../UniformsStream.h"
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

			if (XlEqString(next._value, "DescriptorSet") || XlEqString(next._value, "GraphicsDescriptorSet") || XlEqString(next._value, "ComputeDescriptorSet")) {
				auto pipelineType = PipelineType::Graphics;
				if (XlEqString(next._value, "ComputeDescriptorSet"))
					pipelineType = PipelineType::Compute;
					
				auto name = iterator.GetNextToken();
				auto semi = iterator.GetNextToken();
				if (name._value.IsEmpty() || !XlEqString(semi._value, ";"))
					Throw(FormatException("Expecting identifier name and then ;", name._start));

				// lookup this descriptor set in list of already registered descriptor sets
				auto i = _descriptorSets.find(name._value.AsString());
				if (i == _descriptorSets.end())
					Throw(FormatException(StringMeld<256>() << "Descriptor set with the name (" << name._value << ") has not been declared", name._start));

				result->_descriptorSets.push_back({name._value.AsString(), i->second, pipelineType});
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
			}
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
		auto initialFile = includeHandler.OpenFile(sourceFileName, {});
		ConditionalProcessingTokenizer tokenizer(
			MakeStringSection((const char*)initialFile._fileContents.get(), (const char*)PtrAdd(initialFile._fileContents.get(), initialFile._fileContentsSize)),
			initialFile._filename,
			&includeHandler);
		Parse(tokenizer);
		_depVal = includeHandler.MakeDependencyValidation();
	}

	PredefinedPipelineLayoutFile::PredefinedPipelineLayoutFile() {}
	PredefinedPipelineLayoutFile::~PredefinedPipelineLayoutFile() {}

	PipelineLayoutInitializer PredefinedPipelineLayout::MakePipelineLayoutInitializer(ShaderLanguage language, SamplerPool* samplerPool) const
	{
		PipelineLayoutInitializer::DescriptorSetBinding descriptorSetBindings[_descriptorSets.size()];
		for (size_t c=0; c<_descriptorSets.size(); ++c) {
			descriptorSetBindings[c]._name = _descriptorSets[c]._name;
			descriptorSetBindings[c]._signature = _descriptorSets[c]._descSet->MakeDescriptorSetSignature(samplerPool);
			descriptorSetBindings[c]._pipelineType = _pipelineType;
		}

		PipelineLayoutInitializer::PushConstantsBinding pushConstantBindings[3];
		unsigned pushConstantBindingsCount = 0;
		if (_vsPushConstants.second) {
			auto& binding = pushConstantBindings[pushConstantBindingsCount++];
			binding._name = _vsPushConstants.first;
			binding._shaderStage = ShaderStage::Vertex;
			binding._cbSize = _vsPushConstants.second->GetSize(language);
			binding._cbElements = _vsPushConstants.second->MakeConstantBufferElements(language);
		}

		if (_psPushConstants.second) {
			auto& binding = pushConstantBindings[pushConstantBindingsCount++];
			binding._name = _psPushConstants.first;
			binding._shaderStage = ShaderStage::Pixel;
			binding._cbSize = _psPushConstants.second->GetSize(language);
			binding._cbElements = _psPushConstants.second->MakeConstantBufferElements(language);
		}

		if (_gsPushConstants.second) {
			auto& binding = pushConstantBindings[pushConstantBindingsCount++];
			binding._name = _gsPushConstants.first;
			binding._shaderStage = ShaderStage::Geometry;
			binding._cbSize = _gsPushConstants.second->GetSize(language);
			binding._cbElements = _gsPushConstants.second->MakeConstantBufferElements(language);
		}
		if (_csPushConstants.second) {
			auto& binding = pushConstantBindings[pushConstantBindingsCount++];
			binding._name = _csPushConstants.first;
			binding._shaderStage = ShaderStage::Compute;
			binding._cbSize = _csPushConstants.second->GetSize(language);
			binding._cbElements = _csPushConstants.second->MakeConstantBufferElements(language);
		}
		assert(pushConstantBindingsCount <= dimof(pushConstantBindings));

		return PipelineLayoutInitializer {
			MakeIteratorRange(descriptorSetBindings, &descriptorSetBindings[_descriptorSets.size()]),
			MakeIteratorRange(pushConstantBindings, &pushConstantBindings[pushConstantBindingsCount])};
	}

	const PredefinedDescriptorSetLayout* PredefinedPipelineLayout::FindDescriptorSet(StringSection<> name) const
	{
		for (const auto& d:_descriptorSets)
			if (XlEqString(name, d._name))
				return d._descSet.get();
		return nullptr;
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

		_pipelineType = PipelineType::Graphics;
		if (!i->second->_descriptorSets.empty()) {
			_pipelineType = i->second->_descriptorSets[0]._pipelineType;
			_descriptorSets.reserve(i->second->_descriptorSets.size());
			for (const auto& d:i->second->_descriptorSets) {
				if (d._pipelineType != _pipelineType)
					Throw(::Assets::Exceptions::ConstructionError(
						::Assets::Exceptions::ConstructionError::Reason::FormatNotUnderstood,
						srcFile.GetDependencyValidation(),
						"Mixing multiple pipeline types (compute/graphics) in pipeline layout"));
				_descriptorSets.push_back({d._name, d._descSet});
			}
		}
		_vsPushConstants = i->second->_vsPushConstants;
		_psPushConstants = i->second->_psPushConstants;
		_gsPushConstants = i->second->_gsPushConstants;
		_csPushConstants = i->second->_csPushConstants;
		_depVal = srcFile.GetDependencyValidation();
	}

	void PredefinedPipelineLayout::ConstructToFuture(
		::Assets::FuturePtr<PredefinedPipelineLayout>& future,
		StringSection<::Assets::ResChar> src)
	{
		auto split = MakeFileNameSplitter(src);
		if (split.Parameters().IsEmpty())
			Throw(std::runtime_error("Missing pipeline layout name when loading pipeline layout (expecting <filename>:<layout name>). For request: " + src.AsString()));
		auto fileFuture = ::Assets::MakeAsset<PredefinedPipelineLayoutFile>(split.AllExceptParameters());
		::Assets::WhenAll(fileFuture).ThenConstructToFuture(
			future,
			[layoutName=split.Parameters().AsString()](auto file) {
				return std::make_shared<PredefinedPipelineLayout>(*file, layoutName);
			});
		
	}

}}

