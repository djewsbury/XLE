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

	enum class TemplateParameterType { Typename, Expression };

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

		BlockDefinitionId FindBlockDefinition(StringSection<> name, BlockDefinitionId scope=~0u) const;
		AliasId FindAlias(StringSection<> name, BlockDefinitionId scope=~0u) const;
		BitFieldId FindBitField(StringSection<> name, BlockDefinitionId scope=~0u) const;
		LiteralsId FindLiterals(StringSection<> name, BlockDefinitionId scope=~0u) const;

		const Alias& GetAlias(AliasId id) const { return _aliases[id]._def; }
		const BlockDefinition& GetBlockDefinition(BlockDefinitionId id) const { return _blockDefinitions[id]._def; }
		const BitFieldDefinition& GetBitFieldDecoder(BitFieldId id) const { return _bitFields[id]._def; }
		const ParameterBox& GetLiterals(LiteralsId id) const { return _literals[id]._def; }

		const std::string& GetAliasName(AliasId id) const { return _aliases[id]._name; }
		const std::string& GetBlockDefinitionName(BlockDefinitionId id) const { return _blockDefinitions[id]._name; }
		const std::string& GetBitFieldName(BitFieldId id) const { return _bitFields[id]._name; }
		const std::string& GetLiteralsName(LiteralsId id) const { return _literals[id]._name; }

		struct ConditionSymbol { unsigned _lineIdx; };
		ConditionSymbol GetConditionSymbol(unsigned idx) const;
		unsigned GetConditionSymbolCount() const { return (unsigned)_conditionSymbolLines.size(); }

		const ::Assets::DependencyValidation& GetDependencyValidation() const { return _depVal; }
		
	private:
		std::string ParseBlock(ConditionalProcessingTokenizer& tokenizer, BlockDefinitionId scope);
		std::string ParseLiterals(ConditionalProcessingTokenizer& tokenizer, BlockDefinitionId scope);
		std::string ParseAlias(ConditionalProcessingTokenizer& tokenizer, BlockDefinitionId scope);
		std::string ParseBitField(ConditionalProcessingTokenizer& tokenizer, BlockDefinitionId scope);
		std::string ParseTypeBaseName(ConditionalProcessingTokenizer& tokenizer, BlockDefinitionId scope);
		std::string ParseExpressionStr(ConditionalProcessingTokenizer& tokenizer);
		void ParseDecoder(ConditionalProcessingTokenizer& tokenizer, Alias& workingDefinition, BlockDefinitionId scope);
		void PushExpression(BlockDefinition& workingDefinition, ConditionalProcessingTokenizer& tokenizer);
		void PushComplexType(BlockDefinition& workingDefinition, ConditionalProcessingTokenizer& tokenizer, BlockDefinitionId scope, std::string baseName);
		void Parse(ConditionalProcessingTokenizer& tokenizer);
		std::string TryDeclaration(ConditionalProcessingTokenizer& tokenizer, BlockDefinitionId scope, const ConditionalProcessingTokenizer::Token& peekNext);
		bool TryCommand(BlockDefinition& workingDefinition, ConditionalProcessingTokenizer& tokenizer, BlockDefinitionId scope, const ConditionalProcessingTokenizer::Token& peekNext);
		size_t WriteJumpBlock(BlockDefinition& workingDefinition, ConditionalProcessingTokenizer& tokenizer, unsigned lineIdx);

		template<typename T> struct Def { std::string _name; BlockDefinitionId _scope = BlockDefinitionId_Invalid; T _def; };
		std::vector<Def<Alias>> _aliases;
		std::vector<Def<BlockDefinition>> _blockDefinitions;
		std::vector<Def<ParameterBox>> _literals;
		std::vector<Def<BitFieldDefinition>> _bitFields;
		std::vector<unsigned> _conditionSymbolLines;
		::Assets::DependencyValidation _depVal;
		unsigned _nextUnnamedSymbolIdx = 0;
	};

    enum class Cmd
	{
		LookupType,
		PopTypeStack,
		EvaluateExpression,
		InlineIndividualMember,
		InlineArrayMember,
		IfFalseThenJump,
		Throw
	};
}

