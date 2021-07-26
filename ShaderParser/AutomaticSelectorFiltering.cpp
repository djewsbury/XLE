// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "AutomaticSelectorFiltering.h"
#include "../Assets/InitializerPack.h"
#include "../Assets/IFileSystem.h"
#include "../Assets/ICompileOperation.h"
#include "../Assets/AssetUtils.h"
#include "../Assets/IntermediateCompilers.h"
#include "../Assets/DepVal.h"
#include "../Assets/PreprocessorIncludeHandler.h"
#include "../ConsoleRig/GlobalServices.h"		// for GetLibVersionDesc
#include "../Utility/Streams/ConditionalPreprocessingTokenizer.h"
#include "../Utility/Streams/OutputStreamFormatter.h"
#include "../Utility/Streams/StreamTypes.h"
#include "../Utility/Streams/SerializationUtils.h"
#include "../Utility/Streams/PathUtils.h"
#include "../Utility/Conversion.h"
#include "../Utility/StringUtils.h"
#include "../Utility/FastParseValue.h"
#include "../Utility/ParameterBox.h"
#include "../Utility/ArithmeticUtils.h"
#include <stdexcept>
#include <set>
#include <sstream>

#include "../OSServices/Log.h"
#include <sstream>

namespace ShaderSourceParser
{
	static const auto ChunkType_Metrics = ConstHash64<'Metr', 'ics'>::Value;

	void SerializationOperator(
		Utility::OutputStreamFormatter& formatter,
		const SelectorFilteringRules& input)
	{
		auto e = formatter.BeginKeyedElement("TokenDictionary");
		for (const auto&t:input._tokenDictionary._tokenDefinitions)
			formatter.WriteSequencedValue(Concatenate(Utility::Internal::AsString(t._type), ":", t._value));
		formatter.EndElement(e);

		e = formatter.BeginKeyedElement("RelevanceTable");
		for (const auto&t:input._relevanceTable) {
			auto q = formatter.BeginKeyedElement(std::to_string(t.first));
			for (const auto&i:t.second)
				formatter.WriteSequencedValue(std::to_string(i));
			formatter.EndElement(q);
		}
		formatter.EndElement(e);
	}

	bool SelectorFilteringRules::IsRelevant(
		StringSection<> symbol, StringSection<> value,
		IteratorRange<const ParameterBox**> environment) const
	{
		bool passesRelevanceCheck = false;

		auto t = _tokenDictionary.TryGetToken(Utility::Internal::TokenDictionary::TokenType::Variable, symbol);
		if (t.has_value()) {
			auto i = _relevanceTable.find(t.value());
			if (i!=_relevanceTable.end()) {
				int relevanceCheck = _tokenDictionary.EvaluateExpression(i->second, environment);
				passesRelevanceCheck |= relevanceCheck != 0;
			}
		}

		if (!passesRelevanceCheck) {
			auto isDefinedT = _tokenDictionary.TryGetToken(Utility::Internal::TokenDictionary::TokenType::IsDefinedTest, symbol);
			if (isDefinedT.has_value()) {
				auto i = _relevanceTable.find(isDefinedT.value());
				if (i!=_relevanceTable.end()) {
					int relevanceCheck = _tokenDictionary.EvaluateExpression(i->second, environment);
					passesRelevanceCheck |= relevanceCheck != 0;
				}
			}
		}

		// Final check -- if we're setting to an integer value, and the shader has a defaulting
		// mechanism, then check if we're setting to the same value as the default
		if (passesRelevanceCheck && t.has_value() && !value.IsEmpty()) {
			int valueAsInt = 0;
			auto* end = FastParseValue(value, valueAsInt);
			if (end == value.end()) {
				auto i = _defaultSets.find(t.value());
				if (i!=_defaultSets.end()) {
					int defaultValue = _tokenDictionary.EvaluateExpression(i->second, environment);
					passesRelevanceCheck &= valueAsInt != defaultValue;
				}
			}
		}

		return passesRelevanceCheck;
	}

	void SelectorFilteringRules::MergeIn(const SelectorFilteringRules& source)
	{
		std::map<Utility::Internal::Token, Utility::Internal::ExpressionTokenList> translatedRelevance;
		for (const auto& e:source._relevanceTable) {
			translatedRelevance.insert(std::make_pair(
				_tokenDictionary.Translate(source._tokenDictionary, e.first),
				_tokenDictionary.Translate(source._tokenDictionary, e.second)));
		}

		_relevanceTable = Utility::Internal::MergeRelevanceTables(
			_relevanceTable, {},
			translatedRelevance, {});

		for (const auto& sideEffect:source._defaultSets) {
			auto key = _tokenDictionary.Translate(source._tokenDictionary, sideEffect.first);
			if (_defaultSets.find(key) != _defaultSets.end())
				continue;

			_defaultSets.insert(std::make_pair(
				key,
				_tokenDictionary.Translate(source._tokenDictionary, sideEffect.second)));
		}

		// Also need to merge in the dependency validation information
		if (_depVal) {
			auto newDepVal = ::Assets::GetDepValSys().Make();
			newDepVal.RegisterDependency(_depVal);
			newDepVal.RegisterDependency(source.GetDependencyValidation());
			_depVal = newDepVal;
		} else {
			_depVal = source.GetDependencyValidation();
		}
		
		RecalculateHash();
	}

	void SelectorFilteringRules::RecalculateHash() 
	{
		// note -- if there are 2 different SelectorFilteringRules with exactly the same contents, except that the
		//		tokens are ordered differently in the _tokenDictionary, they will generate different hash values
		//		We could avoid that if we created a table of tokens -> hash values and used that to generate the value
		//		hash... But it seems pretty unlikely that that would be important
		_hash = _tokenDictionary.CalculateHash();
		for (const auto& r:_relevanceTable)
			_hash = HashCombine(Hash64(AsPointer(r.second.begin()), AsPointer(r.second.end()), r.first), _hash);
		for (const auto& r:_defaultSets)
			_hash = HashCombine(Hash64(AsPointer(r.second.begin()), AsPointer(r.second.end()), r.first), _hash);
	}

	SelectorFilteringRules::SelectorFilteringRules(
		InputStreamFormatter<utf8>& formatter, 
		const ::Assets::DirectorySearchRules&,
		const ::Assets::DependencyValidation& depVal)
	: _depVal(depVal)
	{
		_tokenDictionary._tokenDefinitions.clear();	// empty starting/default tokens

		StringSection<> keyedItem;
		while (formatter.TryKeyedItem(keyedItem)) {
			RequireBeginElement(formatter);
			if (XlEqString(keyedItem, "TokenDictionary")) {
				StringSection<> value;
				while (formatter.TryValue(value)) {
					auto colon = std::find(value.begin(), value.end(), ':');
					if (colon == value.end())
						Throw(Utility::FormatException("Missing colon in token", formatter.GetLocation()));

					Utility::Internal::TokenDictionary::TokenDefinition token;
					token._type = Utility::Internal::AsTokenType({value.begin(), colon});
					token._value = std::string(colon+1, value.end());
					_tokenDictionary._tokenDefinitions.push_back(token);
				}
			} else if (XlEqString(keyedItem, "RelevanceTable")) {
				StringSection<> key;
				while (formatter.TryKeyedItem(key)) {
					Utility::Internal::ExpressionTokenList tokenList;
					RequireBeginElement(formatter);
					StringSection<> value;
					while (formatter.TryValue(value))
						tokenList.push_back(Conversion::Convert<unsigned>(value));
					RequireEndElement(formatter);
					_relevanceTable.insert(std::make_pair(
						Conversion::Convert<unsigned>(key),
						std::move(tokenList)));
				}
			} else {
				SkipElement(formatter);
			}
			RequireEndElement(formatter);
		}

		RecalculateHash();
	}

	SelectorFilteringRules::SelectorFilteringRules(
		const std::unordered_map<std::string, std::string>& relevanceStrings)
	{
		for (const auto&e:relevanceStrings) {
			auto key = Utility::Internal::AsExpressionTokenList(_tokenDictionary, e.first);
			if (key.size() != 1)
				Throw(std::runtime_error("Unexpected key in relevance strings: " + e.first));
			auto value = Utility::Internal::AsExpressionTokenList(
				_tokenDictionary, 
				e.second);
			_relevanceTable.insert(std::make_pair(key[0], std::move(value)));
		}
	}

	SelectorFilteringRules::SelectorFilteringRules() {}
	SelectorFilteringRules::~SelectorFilteringRules() {}

	static bool IsTrue(const Utility::Internal::ExpressionTokenList& expr)
    {
        return expr.size() == 1 && expr[0] == 1;
    }

	SelectorFilteringRules GenerateSelectorFilteringRules(StringSection<> sourceCode)
	{
		auto analysis = Utility::GeneratePreprocessorAnalysisFromString(sourceCode, {}, nullptr);
		SelectorFilteringRules filteringRules;

		filteringRules._tokenDictionary = analysis._tokenDictionary;
		filteringRules._relevanceTable = analysis._relevanceTable;

		for (const auto&s:analysis._sideEffects._substitutions) {
			if (s._type != Utility::Internal::PreprocessorSubstitutions::Type::DefaultDefine
				|| !IsTrue(s._condition))
				continue;

			filteringRules._defaultSets.insert(std::make_pair(
				filteringRules._tokenDictionary.GetToken(Utility::Internal::TokenDictionary::TokenType::Variable, s._symbol),
				filteringRules._tokenDictionary.Translate(analysis._sideEffects._dictionary, s._substitution)));
		}

		return filteringRules;
	}

	std::ostream& SerializationOperator(std::ostream& str, const SelectorFilteringRules& rules)
	{
		str << "-------- Relevance Rules --------" << std::endl;
		for (const auto&r:rules._relevanceTable)
			str << "\t" << rules._tokenDictionary.AsString({r.first}) << " : " << rules._tokenDictionary.AsString(r.second) << std::endl;

		str << std::endl;
		str << "-------- Default Sets --------" << std::endl;
		for (const auto&r:rules._defaultSets) {
			if (!r.second.empty()) {
				str << "\t" << rules._tokenDictionary.AsString({r.first}) << " : " << rules._tokenDictionary.AsString(r.second) << std::endl;
			} else {
				str << "\t" << rules._tokenDictionary.AsString({r.first}) << " : <<no value>>" << std::endl;
			}
		}
		return str;
	}

	class ShaderSelectorFilteringCompileOperation : public ::Assets::ICompileOperation
	{
	public:
		virtual std::vector<TargetDesc>			GetTargets() const override
		{
			return {
				TargetDesc { SelectorFilteringRules::CompileProcessType, "filtering-rules" }
			};
		}

		virtual std::vector<SerializedArtifact>	SerializeTarget(unsigned idx) override
		{
			assert(idx == 0);
			if (_compilationException)
				std::rethrow_exception(_compilationException);
			return _artifacts;
		}

		virtual std::vector<::Assets::DependentFileState> GetDependencies() const override
		{
			return _depFileStates;
		}

		ShaderSelectorFilteringCompileOperation(::Assets::InitializerPack& initializer)
		{
			auto fn = initializer.GetInitializer<std::string>(0);

			::Assets::PreprocessorIncludeHandler handler;
			TRY {
				auto analysis = GeneratePreprocessorAnalysisFromFile(fn, &handler);

				SelectorFilteringRules filteringRules;
				filteringRules._tokenDictionary = analysis._tokenDictionary;
				filteringRules._relevanceTable = analysis._relevanceTable;

				for (auto& s:filteringRules._relevanceTable)
					filteringRules._tokenDictionary.Simplify(s.second);

				for (const auto&s:analysis._sideEffects._substitutions) {
					if (s._type != Utility::Internal::PreprocessorSubstitutions::Type::DefaultDefine
						|| !IsTrue(s._condition))
						continue;

					auto trans = filteringRules._tokenDictionary.Translate(analysis._sideEffects._dictionary, s._substitution);
					filteringRules._tokenDictionary.Simplify(trans);
					filteringRules._defaultSets.insert(std::make_pair(
						filteringRules._tokenDictionary.GetToken(Utility::Internal::TokenDictionary::TokenType::Variable, s._symbol),
						trans));
				}

				MemoryOutputStream<> memStream;
				OutputStreamFormatter fmttr(memStream);
				fmttr << filteringRules;

				{
					SerializedArtifact artifact;
					artifact._chunkTypeCode = SelectorFilteringRules::CompileProcessType;
					artifact._name = "filtering-rules";
					artifact._version = 1;
					artifact._data = ::Assets::AsBlob(memStream.AsString());
					_artifacts.push_back(std::move(artifact));
				}

				{
					SerializedArtifact artifact;
					artifact._chunkTypeCode = ChunkType_Metrics;
					artifact._name = "metrics";
					artifact._version = 1;
					std::stringstream str;
					str << filteringRules;
					artifact._data = ::Assets::AsBlob(str.str());
					_artifacts.push_back(std::move(artifact));
				}

				_depFileStates.insert(_depFileStates.begin(), handler._depFileStates.begin(), handler._depFileStates.end());
			} CATCH(...) {
				_depFileStates.clear();
				_depFileStates.insert(_depFileStates.begin(), handler._depFileStates.begin(), handler._depFileStates.end());
				_compilationException = std::current_exception();
			} CATCH_END
		}

		std::vector<::Assets::DependentFileState> _depFileStates;
		std::vector<SerializedArtifact> _artifacts;
		std::exception_ptr _compilationException;
	};

	::Assets::CompilerRegistration RegisterShaderSelectorFilteringCompiler(
		::Assets::IIntermediateCompilers& intermediateCompilers)
	{
		::Assets::CompilerRegistration result{
			intermediateCompilers,
			"shader-selector-filtering-compiler",
			"shader-selector-filtering-compiler",
			ConsoleRig::GetLibVersionDesc(),
			{},
			[](auto initializers) {
				return std::make_shared<ShaderSelectorFilteringCompileOperation>(initializers);
			},
			[](::Assets::ArtifactTargetCode targetCode, const ::Assets::InitializerPack& initializers) {
				assert(targetCode == SelectorFilteringRules::CompileProcessType);
				::Assets::IIntermediateCompilers::SplitArchiveName result;
				auto fn = initializers.GetInitializer<std::string>(0);
				auto splitFN = MakeFileNameSplitter(fn);
				result._entryId = Hash64(fn);
				result._archive = "filtering";
				result._descriptiveName = fn;
				return result;
			}};

		uint64_t outputAssetTypes[] = { SelectorFilteringRules::CompileProcessType };
		intermediateCompilers.AssociateRequest(
			result.RegistrationId(),
			MakeIteratorRange(outputAssetTypes));
		return result;
	}

	ParameterBox SelectorPreconfiguration::Preconfigure(ParameterBox&& input) const
	{
		ParameterBox output = std::move(input);

		/*Log(Warning) << "Prior to filtering: " << std::endl;
		for (auto v:output)
			Log(Warning) << "\t" << v.Name() << " = " << v.ValueAsString() << std::endl;*/

		for (const auto&subst:_preconfigurationSideEffects._substitutions) {
			const ParameterBox* o = &output;
			auto conditionEval = _preconfigurationSideEffects._dictionary.EvaluateExpression(subst._condition, MakeIteratorRange(&o, &o+1));
			if (!conditionEval) continue;

			if (subst._type == Utility::Internal::PreprocessorSubstitutions::Type::Define || subst._type == Utility::Internal::PreprocessorSubstitutions::Type::DefaultDefine) {
				auto evaluated = _preconfigurationSideEffects._dictionary.EvaluateExpression(subst._substitution, MakeIteratorRange(&o, &o+1));
				output.SetParameter(subst._symbol, evaluated);
			} else {
				assert(subst._type == Utility::Internal::PreprocessorSubstitutions::Type::Undefine);
				output.RemoveParameter(MakeStringSection(subst._symbol));
			}
		}

		/*Log(Warning) << "After filtering: " << std::endl;
		for (auto v:output)
			Log(Warning) << "\t" << v.Name() << " = " << v.ValueAsString() << std::endl;*/

		return output;
	}

	SelectorPreconfiguration::SelectorPreconfiguration(StringSection<> filename)
	{
		::Assets::PreprocessorIncludeHandler handler;
		TRY {
			auto analysis = GeneratePreprocessorAnalysisFromFile(filename, &handler);
			_preconfigurationSideEffects = analysis._sideEffects;
			_depVal = handler.MakeDependencyValidation();

			_hash = _preconfigurationSideEffects._dictionary.CalculateHash();
			for (const auto& i:_preconfigurationSideEffects._substitutions) {
				_hash = Hash64(i._symbol, _hash);
				_hash = Hash64(AsPointer(i._condition.begin()), AsPointer(i._condition.end()), _hash);
				_hash = Hash64(AsPointer(i._substitution.begin()), AsPointer(i._substitution.end()), _hash);
				_hash = rotl64(_hash, (int8_t)i._type);
			}

			std::stringstream metrics;
			for (const auto& i:_preconfigurationSideEffects._substitutions) {
				metrics << i._symbol << " is ";
				if (i._type == Utility::Internal::PreprocessorSubstitutions::Type::Undefine) {
					metrics << "undefined";
				} else {
					if (i._type == Utility::Internal::PreprocessorSubstitutions::Type::Define) metrics << "defined to ";
					else if (i._type == Utility::Internal::PreprocessorSubstitutions::Type::DefaultDefine) metrics << "default defined to ";
					else metrics << "<<unknown operation>> ";
					metrics << _preconfigurationSideEffects._dictionary.AsString(i._substitution);
				}
				if (i._condition.empty() || (i._condition.size() == 1 && i._condition[0] == 1)) {
					// unconditional
				} else 
					metrics << ", if " << _preconfigurationSideEffects._dictionary.AsString(i._condition);
				metrics << std::endl;
			}

			// Log(Warning) << metrics.str() << std::endl;
		} CATCH (const std::exception& e) {
			Throw(::Assets::Exceptions::ConstructionError(e, handler.MakeDependencyValidation()));
		} CATCH_END
	}

	SelectorPreconfiguration::~SelectorPreconfiguration()
	{

	}	
}
