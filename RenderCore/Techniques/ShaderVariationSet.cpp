// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "ShaderVariationSet.h"
#include "ParsingContext.h"
#include "../Types.h"
#include "../FrameBufferDesc.h"
#include "../UniformsStream.h"
#include "../BufferView.h"
#include "../Metal/InputLayout.h"
#include "../Metal/Shader.h"
#include "../Metal/DeviceContext.h"
#include "../Metal/PipelineLayout.h"
#include "../../ShaderParser/AutomaticSelectorFiltering.h"
#include "../../ShaderParser/ShaderAnalysis.h"
#include "../../Assets/AssetsCore.h"
#include "../../Assets/Assets.h"
#include "../../Assets/AssetFutureContinuation.h"
#include "../../Utility/Streams/PreprocessorInterpreter.h"

namespace RenderCore { namespace Techniques
{
	static uint64_t Hash(IteratorRange<const ParameterBox* const*> shaderSelectors)
	{
		uint64_t inputHash = 0;
		const bool simpleHash = false;
		if (constant_expression<simpleHash>::result()) {
			for (unsigned c = 0; c < shaderSelectors.size(); ++c) {
				inputHash ^= shaderSelectors[c]->GetParameterNamesHash();
				inputHash ^= shaderSelectors[c]->GetHash() << (c * 6);    // we have to be careful of cases where the values in one box is very similar to the values in another
			}
		} else {
			inputHash = HashCombine(shaderSelectors[0]->GetHash(), shaderSelectors[0]->GetParameterNamesHash());
			for (unsigned c = 1; c < shaderSelectors.size(); ++c) {
				inputHash = HashCombine(shaderSelectors[c]->GetParameterNamesHash(), inputHash);
				inputHash = HashCombine(shaderSelectors[c]->GetHash(), inputHash);
			}
		}
		return inputHash;
	}

	static std::string MakeFilteredDefinesTable(
		IteratorRange<const ParameterBox* const*> selectors,
		const ShaderSourceParser::ManualSelectorFiltering& techniqueFiltering,
		IteratorRange<const ShaderSourceParser::SelectorFilteringRules**> automaticFiltering,
		const ShaderSourceParser::SelectorPreconfiguration* preconfiguration)
	{
		if (!preconfiguration) {
			return BuildFlatStringTable(ShaderSourceParser::FilterSelectors(selectors, techniqueFiltering, automaticFiltering));
		} else {
			// If we have a preconfiguration file, we need to run that over the selectors first. It will define or undefine
			// selectors based on it's script. Because it can also undefine, we need to be working with non-const parameter boxes
			// and so we might as well just merge together all of the param boxes here to make the next few steps a little
			// easier
			if (selectors.empty()) {
				ParameterBox empty;
				ParameterBox* p = &empty;
				return MakeFilteredDefinesTable({&p, &p+1}, techniqueFiltering, automaticFiltering, preconfiguration);
			}

			ParameterBox mergedSelectors = *selectors[0];
			for (auto i=selectors.begin()+1; i!=selectors.end(); ++i)
				mergedSelectors.MergeIn(**i);

			mergedSelectors = preconfiguration->Preconfigure(std::move(mergedSelectors));
			ParameterBox* p = &mergedSelectors;
			return MakeFilteredDefinesTable({&p, &p+1}, techniqueFiltering, automaticFiltering, nullptr);
		}
	}

	auto UniqueShaderVariationSet::FilterSelectors(
		IteratorRange<const ParameterBox* const*> selectors,
		const ShaderSourceParser::ManualSelectorFiltering& techniqueFiltering,
		IteratorRange<const ShaderSourceParser::SelectorFilteringRules**> automaticFiltering,
		const ShaderSourceParser::SelectorPreconfiguration* preconfiguration) -> const FilteredSelectorSet&
	{
		auto inputHash = Hash(selectors);
		inputHash = HashCombine(techniqueFiltering.GetHash(), inputHash);
		for (const auto*f:automaticFiltering)
			inputHash = HashCombine(f->GetHash(), inputHash);
		if (preconfiguration)
			inputHash = HashCombine(preconfiguration->GetHash(), inputHash);

		auto i = LowerBound(_globalToFiltered, inputHash);
		if (i!=_globalToFiltered.cend() && i->first == inputHash) {
			return i->second;
		} else {
			FilteredSelectorSet result;
			result._selectors = MakeFilteredDefinesTable(selectors, techniqueFiltering, automaticFiltering, preconfiguration);
			result._hashValue = Hash64(result._selectors);
			i = _globalToFiltered.insert(i, {inputHash, result});
			return i->second;
		}
	}

	UniqueShaderVariationSet::UniqueShaderVariationSet()  {}
	UniqueShaderVariationSet::~UniqueShaderVariationSet() {}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	class TechniqueShaderVariationSet::Variation
	{
	public:
		::Assets::PtrToFuturePtr<Metal::ShaderProgram> _shaderFuture;
	};

	::Assets::PtrToFuturePtr<Metal::ShaderProgram> TechniqueShaderVariationSet::FindVariation(
		int techniqueIndex,
		const ParameterBox* shaderSelectors[SelectorStages::Max])
	{
		const auto& techEntry = _technique->GetEntry(techniqueIndex);
		auto& filteredSelectors = _variationSet.FilterSelectors(
			MakeIteratorRange(shaderSelectors, &shaderSelectors[SelectorStages::Max]),
			techEntry._selectorFiltering, {}, nullptr);

		auto i = LowerBound(_filteredSelectorsToVariation, filteredSelectors._hashValue);
		if (i != _filteredSelectorsToVariation.end() && i->first == filteredSelectors._hashValue) {
			return i->second._shaderFuture;
		} else {
			Variation variation;
			assert(!techEntry._vertexShaderName.empty());
			assert(!techEntry._pixelShaderName.empty());
			if (techEntry._geometryShaderName.empty()) {
				variation._shaderFuture = ::Assets::MakeAsset<Metal::ShaderProgram>(_pipelineLayout, techEntry._vertexShaderName, techEntry._pixelShaderName, filteredSelectors._selectors);
			} else {
				variation._shaderFuture = ::Assets::MakeAsset<Metal::ShaderProgram>(_pipelineLayout, techEntry._vertexShaderName, techEntry._geometryShaderName, techEntry._pixelShaderName, filteredSelectors._selectors);
			}
			i = _filteredSelectorsToVariation.insert(i, std::make_pair(filteredSelectors._hashValue, variation));
			return i->second._shaderFuture;
		}
	}

	TechniqueShaderVariationSet::TechniqueShaderVariationSet(
		const std::shared_ptr<Technique>& technique,
		const std::shared_ptr<ICompiledPipelineLayout>& pipelineLayout)
	: _technique(technique)
	, _pipelineLayout(pipelineLayout)
	{}

	TechniqueShaderVariationSet::~TechniqueShaderVariationSet(){}

	const ::Assets::DependencyValidation& TechniqueShaderVariationSet::GetDependencyValidation() const
	{
		return _technique->GetDependencyValidation();
	}

	void TechniqueShaderVariationSet::ConstructToFuture(
		::Assets::FuturePtr<TechniqueShaderVariationSet>& future,
		StringSection<::Assets::ResChar> modelScaffoldName,
		const std::shared_ptr<ICompiledPipelineLayout>& pipelineLayout)
	{
		auto scaffoldFuture = ::Assets::MakeAsset<Technique>(modelScaffoldName);
		::Assets::WhenAll(scaffoldFuture).ThenConstructToFuture<TechniqueShaderVariationSet>(
			future,
			[pipelineLayout](std::shared_ptr<Technique> technique) {
				return std::make_shared<TechniqueShaderVariationSet>(technique, pipelineLayout);
			});
	}
}}
