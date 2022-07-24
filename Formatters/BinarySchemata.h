// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../Assets/AssetUtils.h"
#include "../Utility/Streams/PreprocessorInterpreter.h"
#include "../Utility/Streams/ConditionalPreprocessingTokenizer.h"
#include "../Utility/ParameterBox.h"

namespace Formatters
{
	struct BlockDefinition
	{
		Utility::Internal::TokenDictionary _tokenDictionary;
		std::vector<unsigned> _cmdList;
		std::vector<unsigned> _templateParameterNames;
		uint32_t _templateParameterTypeField = 0;
	};

	struct Alias
	{
		std::string _aliasedType;
		Utility::Internal::TokenDictionary _tokenDictionary;
		std::vector<unsigned> _templateParameterNames;
		uint32_t _templateParameterTypeField = 0;
		uint32_t _bitFieldDecoder = ~0u;
		uint32_t _enumDecoder = ~0u;
	};

	struct BitFieldDefinition
	{
		struct BitRange { unsigned _min = 0, _count = 0; std::string _name, _storageType; };
		std::vector<BitRange> _bitRanges;
	};

	class BinarySchemata
	{
	public:
		BinarySchemata(
			StringSection<> inputData,
			const ::Assets::DirectorySearchRules& searchRules,
			const ::Assets::DependencyValidation& depVal);
		BinarySchemata(
			Utility::IPreprocessorIncludeHandler::Result&& initialFile,
			Utility::IPreprocessorIncludeHandler* includeHandler);
		BinarySchemata();
		~BinarySchemata();

		using BlockDefinitionId = unsigned;
		using AliasId = unsigned;
		using BitFieldId = unsigned;
		using LiteralsId = unsigned;
		static constexpr BlockDefinitionId BlockDefinitionId_Invalid = ~0u;
		static constexpr AliasId AliasId_Invalid = ~0u;

		BlockDefinitionId FindBlockDefinition(StringSection<> name) const;
		AliasId FindAlias(StringSection<> name) const;
		BitFieldId FindBitField(StringSection<> name) const;
		LiteralsId FindLiterals(StringSection<> name) const;

		const Alias& GetAlias(AliasId id) const { return _aliases[id].second; }
		const BlockDefinition& GetBlockDefinition(BlockDefinitionId id) const { return _blockDefinitions[id].second; }
		const BitFieldDefinition& GetBitFieldDecoder(BitFieldId id) const { return _bitFields[id].second; }
		const ParameterBox& GetLiterals(LiteralsId id) const { return _literals[id].second; }

		struct ConditionSymbol { unsigned _lineIdx; };
		ConditionSymbol GetConditionSymbol(unsigned idx) const;
		unsigned GetConditionSymbolCount() const { return _conditionSymbolLines.size(); }
		
	private:
		void ParseBlock(ConditionalProcessingTokenizer& tokenizer);
		void ParseLiterals(ConditionalProcessingTokenizer& tokenizer);
		void ParseAlias(ConditionalProcessingTokenizer& tokenizer);
		void ParseBitField(ConditionalProcessingTokenizer& tokenizer);
		std::string ParseTypeBaseName(ConditionalProcessingTokenizer& tokenizer);
		std::string ParseExpressionStr(ConditionalProcessingTokenizer& tokenizer);
		void PushExpression(BlockDefinition& workingDefinition, ConditionalProcessingTokenizer& tokenizer);
		void PushComplexType(BlockDefinition& workingDefinition, ConditionalProcessingTokenizer& tokenizer);
		void Parse(ConditionalProcessingTokenizer& tokenizer);

		std::vector<unsigned> _conditionSymbolLines;
	};

    enum class Cmd
	{
		LookupType,
		PopTypeStack,
		EvaluateExpression,
		InlineIndividualMember,
		InlineArrayMember,
		IfFalseThenJump
	};

    enum class TemplateParameterType { Typename, Expression };
}

