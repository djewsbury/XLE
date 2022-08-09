// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "BinaryFormatter.h"
#include "../Assets/DepVal.h"
#include "../Assets/AssetUtils.h"
#include "../Utility/ParameterBox.h"
#include "../Utility/Streams/PreprocessorInterpreter.h"
#include "../Utility/Streams/ConditionalPreprocessingTokenizer.h"
#include "../Utility/StringFormat.h"
#include "../Utility/ArithmeticUtils.h"
#include "../Utility/StreamUtils.h"
#include <stack>

namespace Formatters
{

	template<typename Iterator>
		static bool Match(IteratorRange<Iterator> lhs, IteratorRange<Iterator> rhs)
	{
		if (lhs.size() != rhs.size()) return false;
		auto l = lhs.begin(), r = rhs.begin();
		while (l != lhs.end()) {
			if (*l != *r) return false;
			++l; ++r;
		}
		return true;
	}

	bool operator==(const EvaluationContext::EvaluatedType& lhs, const EvaluationContext::EvaluatedType& rhs)
	{
		if (lhs._blockDefinition != rhs._blockDefinition) return false;
		if (lhs._alias != rhs._alias) return false;
		if (lhs._paramTypeField != rhs._paramTypeField) return false;
		if (!Match(MakeIteratorRange(lhs._params), MakeIteratorRange(rhs._params))) return false;
		if (lhs._schemata != rhs._schemata) return false;
		return lhs._valueTypeDesc == rhs._valueTypeDesc; 
	}

	auto EvaluationContext::GetEvaluatedType(const std::shared_ptr<BinarySchemata>& schemata, StringSection<> baseName, BinarySchemata::BlockDefinitionId scope, IteratorRange<const int64_t*> parameters, unsigned typeBitField) -> EvaluatedTypeToken
	{
		if (parameters.empty()) {
			if (XlEqString(baseName, "void")) return GetEvaluatedType(ImpliedTyping::TypeCat::Void);
			if (XlEqString(baseName, "int8")) return GetEvaluatedType(ImpliedTyping::TypeCat::Int8);
			if (XlEqString(baseName, "uint8")) return GetEvaluatedType(ImpliedTyping::TypeCat::UInt8);
			if (XlEqString(baseName, "int16")) return GetEvaluatedType(ImpliedTyping::TypeCat::Int16);
			if (XlEqString(baseName, "uint16")) return GetEvaluatedType(ImpliedTyping::TypeCat::UInt16);
			if (XlEqString(baseName, "int32")) return GetEvaluatedType(ImpliedTyping::TypeCat::Int32);
			if (XlEqString(baseName, "uint32")) return GetEvaluatedType(ImpliedTyping::TypeCat::UInt32);
			if (XlEqString(baseName, "int64")) return GetEvaluatedType(ImpliedTyping::TypeCat::Int64);
			if (XlEqString(baseName, "uint64")) return GetEvaluatedType(ImpliedTyping::TypeCat::UInt64);
			if (XlEqString(baseName, "float16")) return GetEvaluatedType(ImpliedTyping::TypeCat::UInt16);
			if (XlEqString(baseName, "float32")) return GetEvaluatedType(ImpliedTyping::TypeCat::Float);
			if (XlEqString(baseName, "float64")) return GetEvaluatedType(ImpliedTyping::TypeCat::Double);
			if (XlEqString(baseName, "char")) return GetEvaluatedType(EvaluatedType{ImpliedTyping::TypeDesc{ImpliedTyping::TypeCat::UInt8, 1, ImpliedTyping::TypeHint::String}});
		}

		auto ai = schemata->FindAlias(baseName, scope);
		if (ai != BinarySchemata::AliasId_Invalid) {
			auto& alias = schemata->GetAlias(ai);
			auto aliasedType = GetEvaluatedType(schemata, alias._aliasedType, BinarySchemata::BlockDefinitionId_Invalid);		// todo -- how should scoping work for aliases?
			EvaluatedType type;
			type._alias = ai;
			type._params = {parameters.begin(), parameters.end()};
			type._paramTypeField = typeBitField;
			type._valueTypeDesc = _evaluatedTypes[aliasedType]._valueTypeDesc;
			type._schemata = schemata;
			return GetEvaluatedType(type);
		}

		auto i = schemata->FindBlockDefinition(baseName, scope);
		if (i == BinarySchemata::BlockDefinitionId_Invalid)
			Throw(std::runtime_error("Unknown type while looking up (" + baseName.AsString() + ")"));

		auto result = (EvaluatedTypeToken)_evaluatedTypes.size();
		EvaluatedType type;
		type._blockDefinition = i;
		type._params = {parameters.begin(), parameters.end()};
		type._paramTypeField = typeBitField;
		type._schemata = schemata;
		return GetEvaluatedType(type);
	}

	EvaluationContext::EvaluatedTypeToken EvaluationContext::GetEvaluatedType(ImpliedTyping::TypeCat typeCat) { return GetEvaluatedType(EvaluatedType{ImpliedTyping::TypeDesc{typeCat}}); }
	EvaluationContext::EvaluatedTypeToken EvaluationContext::GetEvaluatedType(const EvaluatedType& evalType)
	{
		auto existing = std::find(_evaluatedTypes.begin(), _evaluatedTypes.end(), evalType);
		if (existing != _evaluatedTypes.end())
			return (unsigned)std::distance(_evaluatedTypes.begin(), existing);
		_evaluatedTypes.push_back(evalType);
		return (unsigned)_evaluatedTypes.size()-1;
	}

	struct EvaluationContext::CachedSubEvals { std::vector<EvaluatedTypeToken> _subEvals; };

	EvaluationContext::EvaluatedTypeToken EvaluationContext::GetEvaluatedType(
		const std::shared_ptr<BinarySchemata>& schemata, CachedSubEvals* cachedEvals,
		unsigned baseNameToken, BinarySchemata::BlockDefinitionId scope, IteratorRange<const unsigned*> paramTypeCodes, 
		const BlockDefinition& blockDef, 
		std::stack<unsigned>& typeStack, std::stack<int64_t>& valueStack, 
		IteratorRange<const int64_t*> parsingTemplateParams, uint32_t parsingTemplateParamsTypeField)
	{
		// First, try to match to template parameters
		for (unsigned c=0; c<blockDef._templateParameterNames.size(); ++c)
			if (blockDef._templateParameterNames[c] == baseNameToken && (blockDef._templateParameterTypeField & (1<<c))) {
				assert(parsingTemplateParamsTypeField & (1<<c));
				if (paramTypeCodes.size() != 0)
					Throw(std::runtime_error("Using partial templates as template parameters is unsupported"));
				return parsingTemplateParams[c];
			}

		auto paramCount = paramTypeCodes.size();
		if (__builtin_expect(paramCount, 0)) {
			// params end up in reverse order, so we have to reverse them as we're looking them up
			int64_t params[paramCount];
			unsigned typeBitField = 0;
			for (unsigned p=0; p<paramCount; ++p) {
				auto type = paramTypeCodes[p];
				if (type == (unsigned)TemplateParameterType::Typename) {
					params[paramCount-1-p] = typeStack.top();
					typeStack.pop();
					typeBitField |= 1 << (paramCount-1-p);
				} else if (type == (unsigned)TemplateParameterType::Expression) {
					params[paramCount-1-p] = valueStack.top();
					valueStack.pop();
				}
			}
			StringSection<> baseName = blockDef._tokenDictionary._tokenDefinitions[baseNameToken]._value;
			return GetEvaluatedType(schemata, baseName, scope, MakeIteratorRange(params, &params[paramCount]), typeBitField);
		} else {
			// check if it's already cached, to try to reduce the number of times we have to lookup the same value
			assert(baseNameToken < cachedEvals->_subEvals.size());
			if (cachedEvals && cachedEvals->_subEvals[baseNameToken] != ~0u)
				return cachedEvals->_subEvals[baseNameToken];

			StringSection<> baseName = blockDef._tokenDictionary._tokenDefinitions[baseNameToken]._value;
			auto result = GetEvaluatedType(schemata, baseName, scope);
			if (cachedEvals)
				cachedEvals->_subEvals[baseNameToken] = result;
			return result;
		}
	}

	const EvaluationContext::EvaluatedType& EvaluationContext::GetEvaluatedTypeDesc(EvaluatedTypeToken evalTypeId) const
	{
		assert(evalTypeId < _evaluatedTypes.size());
		return _evaluatedTypes[evalTypeId];
	}

	auto EvaluationContext::GetCachedEvals(BinarySchemata& schemata, BinarySchemata::BlockDefinitionId scope) -> CachedSubEvals&
	{
		auto hash = HashCombine(uint64_t(&schemata), scope);	// hack using pointer as hash
		auto i = LowerBound(_cachedSubEvals, hash);
		if (i != _cachedSubEvals.end() && i->first == hash)
			return *i->second;
		
		auto& def = schemata.GetBlockDefinition(scope);
		auto subEvals = std::make_unique<CachedSubEvals>();
		subEvals->_subEvals.resize(def._tokenDictionary._tokenDefinitions.size(), ~0u);
		i=_cachedSubEvals.insert(i, std::make_pair(hash, std::move(subEvals)));
		return *i->second;
	}

	std::optional<size_t> EvaluationContext::TryCalculateFixedSize(unsigned evalTypeId, IteratorRange<const uint64_t*> dynamicLocalVars)
	{
		if (_calculatedSizeStates.size() < _evaluatedTypes.size())
			_calculatedSizeStates.resize(_evaluatedTypes.size());

		if (_calculatedSizeStates[evalTypeId]._state == CalculatedSizeState::FixedSize) {
			return _calculatedSizeStates[evalTypeId]._fixedSize;
		} else if (_calculatedSizeStates[evalTypeId]._state == CalculatedSizeState::DynamicSize) {
			return {};
		}

		// Attempt to calculate the fixed size of a complex type. This will succeed as long as the size of the type
		// doesn't depend on the content of the data itself (for example, if there is any array lengths that vary 
		// based on previous members).
		// This mostly used for skipping large arrays (such as an array of vertices in a model file)
		if (_evaluatedTypes[evalTypeId]._blockDefinition == ~0u) {
			auto res = _evaluatedTypes[evalTypeId]._valueTypeDesc.GetSize();
			_calculatedSizeStates[evalTypeId]._state = CalculatedSizeState::FixedSize;
			_calculatedSizeStates[evalTypeId]._fixedSize = res;
			return res;
		}

		auto& def = _evaluatedTypes[evalTypeId]._schemata->GetBlockDefinition(_evaluatedTypes[evalTypeId]._blockDefinition);
		auto scope = _evaluatedTypes[evalTypeId]._blockDefinition;
		auto cmds = MakeIteratorRange(def._cmdList);
		auto& cachedEvals = GetCachedEvals(*_evaluatedTypes[evalTypeId]._schemata, scope);

		std::stack<unsigned> typeStack;
		std::stack<int64_t> valueStack;
		std::vector<Utility::Internal::Token> localVariables;
		size_t resultSize = 0;

		while (!cmds.empty()) {
			auto cmd = *cmds.first++;
			switch ((Cmd)cmd) {
			case Cmd::LookupType:
				{
					auto baseNameToken = *cmds.first++;
					auto paramCount = *cmds.first++;
					assert(cmds.size() >= paramCount);
					auto paramTypeCodes = MakeIteratorRange(cmds.first, cmds.first+paramCount);
					cmds.first += paramCount;

					typeStack.push(
						GetEvaluatedType(
							_evaluatedTypes[evalTypeId]._schemata, &cachedEvals,
							baseNameToken, scope, paramTypeCodes, def, 
							typeStack, valueStack, _evaluatedTypes[evalTypeId]._params, _evaluatedTypes[evalTypeId]._paramTypeField));
				}
				break;

			case Cmd::PopTypeStack:
				typeStack.pop();
				break;

			case Cmd::EvaluateExpression:
				{
					auto length = *cmds.first++;
					assert(cmds.size() >= length);
					auto range = MakeIteratorRange(cmds.begin(), cmds.begin()+length);
					cmds.first += length;

					TRY {
						bool usingDynamicVariable = false;
						const auto& evalType = _evaluatedTypes[evalTypeId];
						Utility::Internal::ExpressionEvaluator exprEval{def._tokenDictionary, range};
						while (auto nextStep = exprEval.GetNextStep()) {
							assert(nextStep._type == Utility::Internal::ExpressionEvaluator::StepType::LookupVariable);

							// ------------------------- previously evaluated members --------------------
							if (std::find(localVariables.begin(), localVariables.end(), nextStep._nameTokenIndex) != localVariables.end()) {
								usingDynamicVariable = true;
								static const unsigned dummy = 1;
								nextStep.SetQueryResult(dummy);	// we use 1 as a default stand-in
								continue;
							}

							// ------------------------- template variables --------------------
							bool foundTemplateParam = false;
							for (unsigned p=0; p<(unsigned)def._templateParameterNames.size(); ++p)
								if (def._templateParameterNames[p] == nextStep._nameTokenIndex) {
									assert(!(evalType._paramTypeField & (1<<p)));		// assert value, not type parameter
									nextStep.SetQueryResult(evalType._params[p]);
									foundTemplateParam = true;
									break;
								}
							if (foundTemplateParam) continue;

							uint64_t hash = Hash64(nextStep._name);
							
							if (std::find(dynamicLocalVars.begin(), dynamicLocalVars.end(), hash) != dynamicLocalVars.end()) {
								usingDynamicVariable = true;
								static const unsigned dummy = 1;
								nextStep.SetQueryResult(dummy);	// we use 1 as a default stand-in
								continue;
							}

							auto globalType = this->_globalState.GetParameterType(hash);
							if (globalType._type != ImpliedTyping::TypeCat::Void) {
								nextStep.SetQueryResult(globalType, this->_globalState.GetParameterRawValue(hash));
								continue;
							}
						}

						if (usingDynamicVariable) {
							_calculatedSizeStates[evalTypeId]._state = CalculatedSizeState::DynamicSize;
							return {};
						}

						auto result = exprEval.GetResult();
						int64_t resultValue = 0;
						if (!ImpliedTyping::Cast(MakeOpaqueIteratorRange(resultValue), ImpliedTyping::TypeOf<int64_t>(), result._data, result._type))
							Throw(std::runtime_error("Invalid expression or returned value that could not be cast to scalar integral in formatter expression evaluation"));
						valueStack.push(resultValue);
					} CATCH(const std::exception& e) {
						auto exprString = def._tokenDictionary.AsString(range);
						Throw(std::runtime_error(e.what() + std::string{", while evaluating ["} + exprString + "]"));
					} CATCH_END
					break;
				}

			case Cmd::InlineIndividualMember:
			case Cmd::InlineArrayMember:
				{
					auto type = typeStack.top();
					auto memberSize = TryCalculateFixedSize(type, dynamicLocalVars);	// todo -- needs our local variables as well
					if (!memberSize.has_value())
						return {};
					if ((Cmd)cmd == Cmd::InlineArrayMember) {
						resultSize += memberSize.value() * valueStack.top();
						valueStack.pop();
					} else {
						resultSize += memberSize.value();
					}
					auto nameToken = *cmds.first++;
					localVariables.push_back(nameToken);
					break;
				}

			case Cmd::IfFalseThenJump:
				{
					auto expressionEval = valueStack.top();
					valueStack.pop();

					auto jumpPt = *cmds.first++;
					if (jumpPt > def._cmdList.size())
						Throw(std::runtime_error("Jump point in conditional is invalid"));
					if (!expressionEval) {
						cmds.first = def._cmdList.begin() + jumpPt;
					} else
						cmds.first++;
					break;
				}

			default:
				Throw(std::runtime_error("Unexpected token in command stream"));
			}
		}

		_calculatedSizeStates[evalTypeId]._state = CalculatedSizeState::FixedSize;
		_calculatedSizeStates[evalTypeId]._fixedSize = resultSize;
		return resultSize;
	}

	static const char* AsString(ImpliedTyping::TypeCat type)
	{
		switch(type) {
		case ImpliedTyping::TypeCat::Void: return "void";
		case ImpliedTyping::TypeCat::Bool: return "bool";
		case ImpliedTyping::TypeCat::Int8: return "int8";
		case ImpliedTyping::TypeCat::UInt8: return "uint8";
		case ImpliedTyping::TypeCat::Int16: return "int16";
		case ImpliedTyping::TypeCat::UInt16: return "uint16";
		case ImpliedTyping::TypeCat::Int32: return "int32";
		case ImpliedTyping::TypeCat::UInt32: return "uint32";
		case ImpliedTyping::TypeCat::Int64: return "int64";
		case ImpliedTyping::TypeCat::UInt64: return "uint64";
		case ImpliedTyping::TypeCat::Float: return "float32";
		case ImpliedTyping::TypeCat::Double: return "float64";
		default: return "<<unknown>>";
		}
	}

	std::ostream& EvaluationContext::SerializeEvaluatedType(std::ostream& str, unsigned typeId) const
	{
		const auto& type = _evaluatedTypes[typeId];
		if (type._blockDefinition != ~0u) {
			str << type._schemata->GetBlockDefinitionName(type._blockDefinition);
		} else if (type._alias != ~0u) {
			str << type._schemata->GetAliasName(type._alias);
		} else {
			assert(type._params.empty());
			str << AsString(type._valueTypeDesc._type);
			if (type._valueTypeDesc._arrayCount > 1)
				str << "[" << type._valueTypeDesc._arrayCount << "]";
		}
		if (!type._params.empty()) {
			str << "(";
			for (unsigned c=0; c<(unsigned)type._params.size(); ++c) {
				if (c != 0) str << ", ";
				if (type._paramTypeField & (1<<c)) {
					SerializeEvaluatedType(str, type._params[c]);
				} else {
					str << type._params[c];
				}
			}
			str << ")";
		}
		return str;
	}

	void EvaluationContext::SetGlobalParameter(StringSection<> name, int64_t value)
	{
		_globalState.SetParameter(name, value);
		_calculatedSizeStates.clear();		// global parameters can invalidate calculated sizes -- so we must clear and recalculate them all
	}

	ParameterBox& EvaluationContext::GetGlobalParameterBox()
	{
		_calculatedSizeStates.clear();		// global parameters can invalidate calculated sizes -- so we must clear and recalculate them all
		return _globalState;
	}

	EvaluationContext::EvaluationContext() = default;
	EvaluationContext::~EvaluationContext() = default;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	BinaryFormatter::BinaryFormatter(EvaluationContext& evalContext, IteratorRange<const void*> data)
	: _evalContext(&evalContext)
	, _dataIterator(data)
	{
		assert(_dataIterator.begin() <= _dataIterator.end());
		_queuedNext = Blob::None;
	}

	void BinaryFormatter::PushPattern(std::shared_ptr<BinarySchemata> schemata, BinarySchemata::BlockDefinitionId blockDefId, IteratorRange<const int64_t*> templateParams, uint32_t templateParamsTypeField)
	{
		_queuedNext = Blob::None;
		BlockContext newContext;
		newContext._definition = &schemata->GetBlockDefinition(blockDefId);
		newContext._scope = blockDefId;
		newContext._cachedEvals = &_evalContext->GetCachedEvals(*schemata, newContext._scope);
		newContext._parsingTemplateParams = {templateParams.begin(), templateParams.end()};
		newContext._parsingTemplateParamsTypeField = templateParamsTypeField;
		newContext._cmdsIterator = MakeIteratorRange(newContext._definition->_cmdList);
		newContext._parsingBlockName = schemata->GetBlockDefinitionName(blockDefId);
		newContext._schemata = std::move(schemata);
		_blockStack.emplace_back(std::move(newContext));
	}

	BinaryFormatter::Blob BinaryFormatter::PeekNext()
	{
		if (_blockStack.empty()) return Blob::None;
		if (_queuedNext != Blob::None) return _queuedNext;
		assert(_dataIterator.begin() <= _dataIterator.end());

		auto& workingBlock = _blockStack.back();
		auto& cmds = workingBlock._cmdsIterator;
		auto& def = *workingBlock._definition;

		if (workingBlock._pendingArrayMembers) {
			if (_evalContext->GetEvaluatedTypeDesc(workingBlock._pendingArrayType)._blockDefinition == ~0)
				return _queuedNext = Blob::ValueMember;
			return _queuedNext = Blob::BeginBlock;
		} else if (workingBlock._pendingEndArray) {
			return _queuedNext = Blob::EndArray;
		}

		while (!workingBlock._cmdsIterator.empty()) {
			switch ((Cmd)*cmds.first) {
			case Cmd::LookupType:
				{
					cmds.first++;
					auto baseNameToken = *cmds.first++;
					auto paramCount = *cmds.first++;
					assert(cmds.size() >= paramCount);
					auto paramTypeCodes = MakeIteratorRange(cmds.first, cmds.first+paramCount);
					cmds.first += paramCount;

					workingBlock._typeStack.push(
						_evalContext->GetEvaluatedType(
							workingBlock._schemata, (EvaluationContext::CachedSubEvals*)workingBlock._cachedEvals,
							baseNameToken, workingBlock._scope, paramTypeCodes, def, 
							workingBlock._typeStack, workingBlock._valueStack,
							workingBlock._parsingTemplateParams, workingBlock._parsingTemplateParamsTypeField));
					break;
				}

			case Cmd::PopTypeStack:
				cmds.first++;
				workingBlock._typeStack.pop();
				break;

			case Cmd::EvaluateExpression:
				{
					cmds.first++;
					auto length = *cmds.first++;
					assert(cmds.size() >= length);
					auto range = MakeIteratorRange(cmds.begin(), cmds.begin()+length);
					cmds.first += length;

					TRY {
						uint8_t stringParseOutputBuffer[1024];
						unsigned stringParseOutputIterator = 0;
						Utility::Internal::ExpressionEvaluator exprEval{def._tokenDictionary, range};
						while (auto nextStep = exprEval.GetNextStep()) {
							assert(nextStep._type == Utility::Internal::ExpressionEvaluator::StepType::LookupVariable);

							// Try to lookup the value in a number of places --
							// - previously evaluated variables
							// - template values
							// - context state

							// (we could just store this hash in the Token object)
							// unfortunately we can't look up by token index any more, because each eval context can have a different dictionary
							uint64_t hash = Hash64(nextStep._name);
							bool gotValue = false;

							// ------------------------- previously evaluated members --------------------
							for (auto block=_blockStack.rbegin(); block!=_blockStack.rend() && !gotValue; ++block) {
								auto localValue = std::find_if(block->_localEvalContext.begin(), block->_localEvalContext.end(), [hash](const auto& q) {return q.first==hash;});
								if (localValue != block->_localEvalContext.end()) {

									// If the value is a string; let's attempt to parse it before we send the results to the 
									if (localValue->second._type._typeHint == ImpliedTyping::TypeHint::String && (localValue->second._type._type == ImpliedTyping::TypeCat::UInt8 || localValue->second._type._type == ImpliedTyping::TypeCat::Int8)) {
										if (stringParseOutputIterator == dimof(stringParseOutputBuffer))
											Throw(std::runtime_error("Parsing buffer exceeded in expression evaluation in BinaryFormatter."));		// This occurs when we're parsing a lot of strings or large arrays from the source data. Consider an alternative approach, because the system isn't optimized for this
										auto parsedType = ImpliedTyping::ParseFullMatch(
											MakeStringSection((const char*)localValue->second._data.begin(), (const char*)localValue->second._data.end()),
											MakeIteratorRange(&stringParseOutputBuffer[stringParseOutputIterator], &stringParseOutputBuffer[dimof(stringParseOutputBuffer)]));
										if (parsedType._type != ImpliedTyping::TypeCat::Void) {
											nextStep.SetQueryResult(parsedType, MakeIteratorRange(&stringParseOutputBuffer[stringParseOutputIterator], &stringParseOutputBuffer[stringParseOutputIterator+parsedType.GetSize()]));
											stringParseOutputIterator += parsedType.GetSize();
											gotValue = true;
											break;
										}
									}
									
									nextStep.SetQueryResult(localValue->second._type, localValue->second._data);
									gotValue = true;
									break;
								}

								if (std::find(block->_nonIntegerLocalVariables.begin(), block->_nonIntegerLocalVariables.end(), hash) != block->_nonIntegerLocalVariables.end())
									Throw(std::runtime_error("Attempting to non-numeric local variable (" + nextStep._name.AsString() + ") in an expression. This isn't supported"));

								// ------------------------- template variables --------------------
								if (block == _blockStack.rbegin())	// (only for the immediately enclosing context)
									for (unsigned p=0; p<(unsigned)block->_definition->_templateParameterNames.size(); ++p)
										if (block->_definition->_templateParameterNames[p] == nextStep._nameTokenIndex) {
											assert(!(block->_parsingTemplateParamsTypeField & (1<<p)));		// assert value, not type parameter
											nextStep.SetQueryResult(block->_parsingTemplateParams[p]);
											gotValue = true;
											break;
										}
							}

							if (!gotValue) {
								auto globalType = this->_evalContext->GetGlobalParameterBox().GetParameterType(hash);
								if (globalType._type != ImpliedTyping::TypeCat::Void) {
									nextStep.SetQueryResult(globalType, this->_evalContext->GetGlobalParameterBox().GetParameterRawValue(hash));
									gotValue = true;
								}
							}
						}

						auto result = exprEval.GetResult();
						int64_t resultValue = 0;
						if (!ImpliedTyping::Cast(MakeOpaqueIteratorRange(resultValue), ImpliedTyping::TypeOf<int64_t>(), result._data, result._type))
							Throw(std::runtime_error("Invalid expression or returned value that could not be cast to scalar integral in formatter expression evaluation"));

						workingBlock._valueStack.push(resultValue);
					} CATCH(const std::exception& e) {
						auto exprString = def._tokenDictionary.AsString(range);
						Throw(std::runtime_error(e.what() + std::string{", while evaluating ["} + exprString + "]"));
					} CATCH_END
					break;
				}

			case Cmd::InlineIndividualMember:
			case Cmd::InlineArrayMember:
				return _queuedNext = Blob::KeyedItem;

			case Cmd::IfFalseThenJump:
				{
					cmds.first++;
					auto expressionEval = workingBlock._valueStack.top();
					workingBlock._valueStack.pop();

					auto jumpPt = *cmds.first++;
					if (jumpPt > def._cmdList.size())
						Throw(std::runtime_error("Jump point in conditional is invalid"));
					if (!expressionEval) {
						cmds.first = AsPointer(def._cmdList.begin() + jumpPt);
					} else
						_passedConditionSymbols.push_back(*cmds.first++);
					break;
				}

			default:
				Throw(std::runtime_error("Unexpected token in command stream"));
			}
		}

		assert(workingBlock._typeStack.empty());
		if (_blockStack.back()._terminateWithEndBlock) {
			return _queuedNext = Blob::EndBlock;
		} else {
			_blockStack.pop_back();
			return PeekNext();
		}
	}

	bool BinaryFormatter::TryKeyedItem(StringSection<>& name)
	{
		if (_blockStack.empty()) return false;
		auto& workingBlock = _blockStack.back();
		auto& cmds = workingBlock._cmdsIterator;
		if (cmds.empty()) return false;

		if (PeekNext() != Blob::KeyedItem) return false;
		if (workingBlock._pendingArrayMembers || workingBlock._pendingEndArray) return false;

		const auto& evalType = _evalContext->GetEvaluatedTypeDesc(workingBlock._typeStack.top());
		if (cmds[0] == (unsigned)Cmd::InlineIndividualMember) {
			if (evalType._blockDefinition == ~0u) {
				_queuedNext = Blob::ValueMember;
			} else
				_queuedNext = Blob::BeginBlock;
		} else if (cmds[0] == (unsigned)Cmd::InlineArrayMember) {

			// Sometimes we can just compress the "array count" into the basic value description, as so...
			bool isCharType = false;
			if (evalType._alias != ~0u && workingBlock._schemata->GetAliasName(evalType._alias) == "char") isCharType = true;		// hack -- special case for "char" alias
			bool isCompressable = evalType._blockDefinition == ~0u && (evalType._alias == ~0u || isCharType) && evalType._valueTypeDesc._arrayCount <= 1;
			_queuedNext = isCompressable ? Blob::ValueMember : Blob::BeginArray;

		} else
			return false;

		auto nameToken = cmds[1];
		const auto& memberName = workingBlock._definition->_tokenDictionary._tokenDefinitions[nameToken]._value;
		name = memberName;
		return true;
	}

	bool BinaryFormatter::TryPeekKeyedItem(StringSection<>& name)
	{
		// TryKeyedItem only changes _queuedNext -- so we can efectively "peek"
		// at it by just changing _queuedNext back ....
		auto res = TryKeyedItem(name);
		if (!res) return false;
		_queuedNext = Blob::KeyedItem;
		return true;
	}

	bool BinaryFormatter::TryBeginBlock(unsigned& evaluatedTypeId)
	{
		if (_blockStack.empty()) return false;

		auto next = PeekNext();
		if (next != Blob::BeginBlock) return false;
		auto& workingBlock = _blockStack.back();
		auto& cmds = workingBlock._cmdsIterator;
		auto& def = *workingBlock._definition;
		if (!workingBlock._pendingArrayMembers) {
			if (workingBlock._pendingEndArray) return false;
			if (cmds.empty()) return false;
			if (*cmds.first != (unsigned)Cmd::InlineIndividualMember) return false;

			assert(!workingBlock._typeStack.empty());
			auto type = workingBlock._typeStack.top();
			const auto& evalType = _evalContext->GetEvaluatedTypeDesc(type);
			if (evalType._blockDefinition == ~0u) return false;

			BlockContext newContext;
			newContext._definition = &workingBlock._schemata->GetBlockDefinition(evalType._blockDefinition);
			newContext._scope = evalType._blockDefinition;
			newContext._cachedEvals = &_evalContext->GetCachedEvals(*workingBlock._schemata, newContext._scope);
			newContext._parsingBlockName = workingBlock._definition->_tokenDictionary._tokenDefinitions[cmds[1]]._value;
			newContext._parsingTemplateParams = evalType._params;
			newContext._parsingTemplateParamsTypeField = evalType._paramTypeField;
			newContext._cmdsIterator = MakeIteratorRange(newContext._definition->_cmdList);
			newContext._parsingBlockName = workingBlock._schemata->GetBlockDefinitionName(evalType._blockDefinition);
			newContext._schemata = workingBlock._schemata;
			newContext._terminateWithEndBlock = true;
			_blockStack.emplace_back(std::move(newContext));

			evaluatedTypeId = type;
			cmds.first+=2;
			_queuedNext = Blob::None;
		} else {
			const auto& evalType = _evalContext->GetEvaluatedTypeDesc(workingBlock._pendingArrayType);
			if (evalType._blockDefinition == ~0u) return false;

			BlockContext newContext;
			newContext._definition = &workingBlock._schemata->GetBlockDefinition(evalType._blockDefinition);
			newContext._scope = evalType._blockDefinition;
			newContext._cachedEvals = &_evalContext->GetCachedEvals(*workingBlock._schemata, newContext._scope);
			newContext._parsingTemplateParams = evalType._params;
			newContext._parsingTemplateParamsTypeField = evalType._paramTypeField;
			newContext._cmdsIterator = MakeIteratorRange(newContext._definition->_cmdList);
			newContext._parsingBlockName = workingBlock._schemata->GetBlockDefinitionName(evalType._blockDefinition);
			newContext._schemata = workingBlock._schemata;
			newContext._terminateWithEndBlock = true;
			_blockStack.emplace_back(std::move(newContext));

			evaluatedTypeId = workingBlock._pendingArrayType;
			--workingBlock._pendingArrayMembers;
			_queuedNext = Blob::None;
		}	
		return true;
	}

	bool BinaryFormatter::TryEndBlock()
	{
		if (_blockStack.size() <= 1) return false;
		if (_blockStack.back()._pendingArrayMembers || _blockStack.back()._pendingEndArray) return false;
		auto next = PeekNext();
		if (next != Blob::EndBlock) return false;
		assert(_blockStack.back()._terminateWithEndBlock);
		_blockStack.pop_back();
		_queuedNext = Blob::None;
		return true;
	}

	bool BinaryFormatter::TryRawValue(IteratorRange<const void*>& resultData, ImpliedTyping::TypeDesc& resultTypeDesc, unsigned& evaluatedTypeId) 
	{
		if (_blockStack.empty()) return false;

		PeekNext();
		auto& workingBlock = _blockStack.back();
		auto& cmds = workingBlock._cmdsIterator;
		auto& def = *workingBlock._definition;
		if (!workingBlock._pendingArrayMembers) {
			if (workingBlock._pendingEndArray) return false;
			if (cmds.empty()) return false;
			if (cmds[0] != (unsigned)Cmd::InlineIndividualMember && cmds[0] != (unsigned)Cmd::InlineArrayMember) return false;

			auto type = workingBlock._typeStack.top();
			auto& evalType = _evalContext->GetEvaluatedTypeDesc(type);
			if (_evalContext->GetEvaluatedTypeDesc(type)._blockDefinition != ~0u) return false;
			auto finalTypeDesc = evalType._valueTypeDesc;

			if (cmds[0] == (unsigned)Cmd::InlineArrayMember) {
				bool isCharType = false;
				if (evalType._alias != ~0u && workingBlock._schemata->GetAliasName(evalType._alias) == "char") isCharType = true;		// hack -- special case for "char" alias
				bool isCompressable = evalType._blockDefinition == ~0u && (evalType._alias == ~0u || isCharType) && finalTypeDesc._arrayCount <= 1;
				if (!isCompressable) return false;
				auto arrayCount = workingBlock._valueStack.top();
				assert(arrayCount <= std::numeric_limits<decltype(finalTypeDesc._arrayCount)>::max());
				finalTypeDesc._arrayCount = arrayCount;
				if (isCharType) finalTypeDesc._typeHint = ImpliedTyping::TypeHint::String;
			}

			auto nameToken = cmds[1];
			auto size = finalTypeDesc.GetSize();
			if (size > _dataIterator.size())
				Throw(std::runtime_error("Binary Schemata reads past the end of data while reading block " + workingBlock._parsingBlockName + ", member: " + def._tokenDictionary._tokenDefinitions[nameToken]._value));
			resultData = MakeIteratorRange(_dataIterator.begin(), PtrAdd(_dataIterator.begin(), size));
			resultTypeDesc = finalTypeDesc;
			
			workingBlock._localEvalContext.emplace_back(Hash64(def._tokenDictionary._tokenDefinitions[nameToken]._value), ImpliedTyping::VariantNonRetained{resultTypeDesc, resultData});
			
			evaluatedTypeId = type;
			cmds.first+=2;
			if (cmds[0] == (unsigned)Cmd::InlineArrayMember)
				workingBlock._valueStack.pop();
			_dataIterator.first = PtrAdd(_dataIterator.first, size);
			_queuedNext = Blob::None;
			return true;
		} else {
			const auto& evalType = _evalContext->GetEvaluatedTypeDesc(workingBlock._pendingArrayType);
			if (evalType._blockDefinition != ~0u) return false;

			auto nameToken = cmds[1];
			auto size = evalType._valueTypeDesc.GetSize();
			if (size > _dataIterator.size())
				Throw(std::runtime_error("Binary Schemata reads past the end of data while reading array in block " + workingBlock._parsingBlockName + ", member: " + def._tokenDictionary._tokenDefinitions[nameToken]._value));
			resultData = MakeIteratorRange(_dataIterator.begin(), PtrAdd(_dataIterator.begin(), size));
			resultTypeDesc = evalType._valueTypeDesc;

			evaluatedTypeId = workingBlock._pendingArrayType;
			_dataIterator.first = PtrAdd(_dataIterator.first, size);
			--workingBlock._pendingArrayMembers;
			if (workingBlock._pendingArrayMembers) {
				_queuedNext = (evalType._blockDefinition == ~0u) ? Blob::KeyedItem : Blob::BeginBlock;
			} else
				_queuedNext = Blob::EndArray;
			return true;
		}
	}

	bool BinaryFormatter::TryBeginArray(unsigned& count, unsigned& evaluatedTypeId)
	{
		if (_blockStack.empty()) return false;

		PeekNext();
		auto& workingBlock = _blockStack.back();
		auto& cmds = workingBlock._cmdsIterator;
		auto& def = *workingBlock._definition;
		if (cmds.empty()) return false;
		if (*cmds.first != (unsigned)Cmd::InlineArrayMember) return false;

		evaluatedTypeId = workingBlock._typeStack.top();
		count = workingBlock._valueStack.top();

		workingBlock._pendingArrayMembers = count;
		workingBlock._pendingArrayType = evaluatedTypeId;
		workingBlock._pendingEndArray = true;
		const auto& evalType = _evalContext->GetEvaluatedTypeDesc(evaluatedTypeId);

		auto nameToken = cmds[1];
		cmds.first += 2;
		workingBlock._valueStack.pop();
		if (workingBlock._pendingArrayMembers) {
			_queuedNext = (evalType._blockDefinition == ~0u) ? Blob::KeyedItem : Blob::BeginBlock;
		} else {
			_queuedNext = Blob::EndArray;
		}

		if (evalType._valueTypeDesc._type != ImpliedTyping::TypeCat::Void) {
			auto arrayData = MakeIteratorRange(_dataIterator.begin(), PtrAdd(_dataIterator.begin(), evalType._valueTypeDesc.GetSize()));
			workingBlock._localEvalContext.emplace_back(Hash64(def._tokenDictionary._tokenDefinitions[nameToken]._value), ImpliedTyping::VariantNonRetained{evalType._valueTypeDesc, arrayData});
		}

		return true;
	}

	bool BinaryFormatter::TryEndArray()
	{
		if (_blockStack.empty()) return false;
		auto& workingBlock = _blockStack.back();
		if (!workingBlock._pendingEndArray) return false;
		if (workingBlock._pendingArrayMembers != 0) return false;

		workingBlock._pendingEndArray = false;
		_queuedNext = Blob::None;
		return true;
	}

	IteratorRange<const void*> BinaryFormatter::SkipArrayElements(unsigned count)
	{
		if (_blockStack.empty())
			Throw(std::runtime_error("SkipArrayElements called on uninitialized formatter"));
		auto& workingBlock = _blockStack.back();
		if (count > workingBlock._pendingArrayMembers)
			Throw(std::runtime_error("Attempting to skip more array elements than what are remaining in SkipArrayElement"));

		auto fixedSize = TryCalculateFixedSize(workingBlock._pendingArrayType);
		if (fixedSize.has_value()) {
			auto totalSize = count * fixedSize.value();
			if (totalSize > _dataIterator.size())
				Throw(std::runtime_error("Binary Schemata reads past the end of data while skipping array elements"));
			workingBlock._pendingArrayMembers -= count;
			auto result = MakeIteratorRange(_dataIterator.begin(), PtrAdd(_dataIterator.first, totalSize));
			_dataIterator.first = PtrAdd(_dataIterator.first, totalSize);
			return result;
		} else {
			// The sizes of the elements are dynamic; we need to read each element at a time and decide on 
			// the sizes individually
			IteratorRange<const void*> result;
			for (unsigned c=0; c<count; ++c) {
				auto d = SkipNextBlob();
				if (c == 0)
					result.first = d.begin();
				result.second = d.end();
			}
			return result;
		}
	}

	IteratorRange<const void*> BinaryFormatter::SkipNextBlob()
	{
		auto next = PeekNext();
		auto start = GetRemainingData();
		if (next == BinaryFormatter::Blob::BeginArray) {
			unsigned count = 0;
			unsigned evalTypeId = 0;
			TryBeginArray(count, evalTypeId);
			SkipArrayElements(count);
			if (!TryEndArray())
				Throw(std::runtime_error("Expecting end array after skipping array elements while skipping binary blob"));
			return {start.begin(), GetRemainingData().begin()};
		} else if (next == BinaryFormatter::Blob::BeginBlock) {
			unsigned evalBlockId;
			TryBeginBlock(evalBlockId);
			auto fixedSize = TryCalculateFixedSize(evalBlockId);
			if (fixedSize.has_value()) {
				if (fixedSize.value() > _dataIterator.size())
					Throw(std::runtime_error("Binary Schemata reads past the end of data while reading block " + _blockStack.back()._parsingBlockName));
				_dataIterator.first = PtrAdd(_dataIterator.first, fixedSize.value());
				_blockStack.pop_back();
				_queuedNext = Blob::None;
			} else {
				while (PeekNext() != BinaryFormatter::Blob::EndBlock)
					SkipNextBlob();
				TryEndBlock();
			}
			return {start.begin(), GetRemainingData().begin()};
		} else if (next == BinaryFormatter::Blob::ValueMember) {
			IteratorRange<const void*> data;
			ImpliedTyping::TypeDesc typeDesc; 
			unsigned evaluatedTypeId;
			TryRawValue(data, typeDesc, evaluatedTypeId);
			return data;
		} else if (next == BinaryFormatter::Blob::KeyedItem) {
			StringSection<> name;
			TryKeyedItem(name);
			auto skip = SkipNextBlob();
			return { start.begin(), skip.end() };
		} else
			Throw(std::runtime_error("Expecting array, block or member while skipping binary blob"));
	}

	std::optional<size_t> BinaryFormatter::TryCalculateFixedSize(unsigned evalTypeId)
	{
		// we need to tell the eval context what local variables will be in scope for this type
		std::vector<uint64_t> localVars;
		for (const auto& b:_blockStack)
			for (const auto& q:b._localEvalContext)
				localVars.push_back(q.first);
		return _evalContext->TryCalculateFixedSize(evalTypeId, localVars);
	}

	///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	void BinaryBlockMatch::ParseValue(BinaryFormatter& formatter, const std::string& name, unsigned parentId)
	{
		unsigned evaluatedTypeId = ~0u;
		unsigned arrayCount = 0;
		IteratorRange<const void*> valueData;
		ImpliedTyping::TypeDesc valueTypeDesc;
		if (formatter.TryBeginBlock(evaluatedTypeId)) {
			Member parentMember;
			parentMember._data = { formatter.GetRemainingData().begin(), formatter.GetRemainingData().begin() };
			parentMember._type = evaluatedTypeId;
			parentMember._parent = parentId;
			parentMember._stringName = name;
			unsigned newParentId = (unsigned)_members.size();
			_members.push_back(std::make_pair(Hash64(name), std::move(parentMember)));
			ParseBlock(formatter, newParentId);
			if (!formatter.TryEndBlock())
				Throw(std::runtime_error("Expected end block"));
			_members[newParentId].second._data.second = formatter.GetRemainingData().begin();
		} else if (formatter.TryRawValue(valueData, valueTypeDesc, evaluatedTypeId)) {
			Member valueMember;
			valueMember._data = valueData;
			valueMember._type = evaluatedTypeId;
			valueMember._typeDesc = valueTypeDesc;
			valueMember._parent = parentId;
			valueMember._stringName = name;
			_members.push_back(std::make_pair(Hash64(name), std::move(valueMember)));
		} else if (formatter.TryBeginArray(arrayCount, evaluatedTypeId)) {
			Member parentMember;
			parentMember._data = { formatter.GetRemainingData().begin(), formatter.GetRemainingData().begin() };
			parentMember._type = evaluatedTypeId;
			parentMember._parent = parentId;
			parentMember._stringName = name;
			parentMember._isArray = true;
			parentMember._arrayCount = arrayCount;
			unsigned newParentId = (unsigned)_members.size();
			_members.push_back(std::make_pair(Hash64(name), std::move(parentMember)));
			for (unsigned c=0; c<arrayCount; ++c)
				ParseValue(formatter, (StringMeld<256>() << "<Element " << c << ">").AsString(), newParentId);
			_members[newParentId].second._data.second = formatter.GetRemainingData().begin();
			assert(formatter.PeekNext() == BinaryFormatter::Blob::EndArray);
			if (!formatter.TryEndArray())
				Throw(std::runtime_error("Expected end array"));
		} else
			Throw(std::runtime_error("Expected value type blob in SerializeValue"));
	}

	void BinaryBlockMatch::ParseBlock(BinaryFormatter& formatter, unsigned parentId)
	{
		for (;;) {
			auto next = formatter.PeekNext();
			switch (next) {
			case BinaryFormatter::Blob::KeyedItem:
				{
					StringSection<> name;
					formatter.TryKeyedItem(name);
					ParseValue(formatter, name.AsString(), parentId);
				}
				break;

			case BinaryFormatter::Blob::BeginBlock:
			case BinaryFormatter::Blob::BeginArray:
			case BinaryFormatter::Blob::EndArray:
			case BinaryFormatter::Blob::ValueMember:
				Throw(std::runtime_error("Unexpected blob in SerializeBlock"));
				break;

			case BinaryFormatter::Blob::EndBlock:
			case BinaryFormatter::Blob::None:
				return;
			}
		}
	}

	BinaryBlockMatch::BinaryBlockMatch(BinaryFormatter& formatter)
	: _evalContext(&formatter.GetEvaluationContext())
	{
		unsigned blockType = 0;
		bool startWithBeginBlock = formatter.TryBeginBlock(blockType);
		ParseBlock(formatter, Member::RootParentMarker);
		if (startWithBeginBlock && !formatter.TryEndBlock())
			Throw(std::runtime_error("Expecting end block in BinaryBlockMatch"));
	}

	BinaryBlockMatch::BinaryBlockMatch(const EvaluationContext& evalContext)
	: _evalContext(&evalContext)
	{
	}

	BinaryBlockMatch::BinaryBlockMatch() : _evalContext(nullptr) {}

	const std::string& BinaryMemberToken::GetTypeBaseName() const
	{
		const auto& type = GetType();
		if (type._alias != BinarySchemata::BlockDefinitionId_Invalid)
			return type._schemata->GetAliasName(type._alias);
		if (type._blockDefinition != BinarySchemata::BlockDefinitionId_Invalid)
			return type._schemata->GetBlockDefinitionName(type._blockDefinition);
		static std::string dummy;
		return dummy;
	}

	bool BinaryMemberToken::IsArray() const
	{
		return _i->second._isArray || (_i->second._typeDesc._type != ImpliedTyping::TypeCat::Void && _i->second._typeDesc._arrayCount > 1);
	}

	unsigned BinaryMemberToken::GetArrayCount() const
	{
		if (_i->second._isArray)
			return _i->second._arrayCount;
		if (_i->second._typeDesc._type != ImpliedTyping::TypeCat::Void && _i->second._typeDesc._arrayCount > 1)
			return _i->second._typeDesc._arrayCount;
		return 0;
	}

	void SkipUntilEndBlock(BinaryFormatter& formatter)
	{
		for (;;) {
			auto next = formatter.PeekNext();
			switch (next) {
			case BinaryFormatter::Blob::KeyedItem:
				formatter.SkipNextBlob();
				break;

			case BinaryFormatter::Blob::BeginBlock:
			case BinaryFormatter::Blob::BeginArray:
			case BinaryFormatter::Blob::EndArray:
			case BinaryFormatter::Blob::ValueMember:
				Throw(std::runtime_error("Unexpected blob in SerializeBlock"));

			case BinaryFormatter::Blob::EndBlock:
			case BinaryFormatter::Blob::None:
				return;
			}
		}
	}

	unsigned RequireBeginBlock(BinaryFormatter& formatter)
	{
		unsigned res = ~0u;
		if (!formatter.TryBeginBlock(res))
			Throw(std::runtime_error("Unexpected blob while looking for begin block in binary formatter"));
		return res;
	}
	void RequireEndBlock(BinaryFormatter& formatter)
	{
		if (!formatter.TryEndBlock())
			Throw(std::runtime_error("Unexpected blob while looking for end block in binary formatter"));
	}

	StringSection<> RequireKeyedItem(BinaryFormatter& formatter)
	{
		StringSection<> result;
		if (!formatter.TryKeyedItem(result))
			Throw(std::runtime_error("Unexpected blob while looking for keyed item in binary formatter"));
		return result;
	}
	
	std::pair<unsigned, unsigned> RequireBeginArray(BinaryFormatter& formatter)
	{
		unsigned count = 0, typeId = ~0u;
		if (!formatter.TryBeginArray(count, typeId))
			Throw(std::runtime_error("Unexpected blob while looking for begin array in binary formatter"));
		return {count, typeId};
	}

	void RequireEndArray(BinaryFormatter& formatter)
	{
		if (!formatter.TryEndArray())
			Throw(std::runtime_error("Unexpected blob while looking for end array in binary formatter"));
	}

	static void SerializeValueWithDecoder(
		std::ostream& str,
		IteratorRange<const void*> data, const ImpliedTyping::TypeDesc& type,
		const Formatters::BitFieldDefinition& def)
	{
		bool first = true;
		uint64_t bits = 0;
		if (!ImpliedTyping::Cast(MakeOpaqueIteratorRange(bits), ImpliedTyping::TypeOf<uint64_t>(), data, type)) {
			str << "Could not interpret value (" << ImpliedTyping::AsString(data, type) << ") using bitfield decoder" << std::endl;
		} else {
			for (auto bitDef:def._bitRanges) {
				auto mask = ((1<<bitDef._count)-1) << bitDef._min;
				if (bits & mask) {
					if (!first) str << " | "; else first = false;
					str << bitDef._name;
					if (bitDef._count != 1)
						str << "(" << std::hex << (bits & mask) << bitDef._min << std::dec << ")";
				}
			}
		}
	}

	static void SerializeValueWithDecoder(
		std::ostream& str,
		IteratorRange<const void*> data, const ImpliedTyping::TypeDesc& type,
		const ParameterBox& enumLiterals)
	{
		uint64_t value = 0;
		if (!ImpliedTyping::Cast(MakeOpaqueIteratorRange(value), ImpliedTyping::TypeOf<uint64_t>(), data, type)) {
			str << "Could not interpret value (" << ImpliedTyping::AsString(data, type) << ") using enum decoder" << std::endl;
		} else {
			for (auto&v:enumLiterals) {
				uint64_t test = 0;
				if (ImpliedTyping::Cast(MakeOpaqueIteratorRange(test), ImpliedTyping::TypeOf<uint64_t>(), v.RawValue(), v.Type()) && value == test) {
					str << v.Name();
					return;
				}
			}
		}
		str << "Unknown enum value (" << value << ")" << std::endl; 
	}

	static std::ostream& SerializeValue(std::ostream& str, BinaryFormatter& formatter, StringSection<> name, unsigned indent = 0)
	{
		unsigned evaluatedTypeId;
		unsigned arrayCount = 0;
		IteratorRange<const void*> valueData;
		ImpliedTyping::TypeDesc valueTypeDesc;
		if (formatter.TryBeginBlock(evaluatedTypeId)) {
			str << StreamIndent{indent};
			formatter.GetEvaluationContext().SerializeEvaluatedType(str, evaluatedTypeId);
			str << " " << name << std::endl;
			SerializeBlock(str, formatter, indent+4);
			if (!formatter.TryEndBlock())
				Throw(std::runtime_error("Expected end block"));
		} else if (formatter.TryRawValue(valueData, valueTypeDesc, evaluatedTypeId)) {
			str << StreamIndent{indent};
			formatter.GetEvaluationContext().SerializeEvaluatedType(str, evaluatedTypeId);
			str << " " << name << " = ";
			bool serializedViaDecoder = false;
			auto& evalType = formatter.GetEvaluationContext().GetEvaluatedTypeDesc(evaluatedTypeId);
			if (evalType._alias != ~0u) {
				auto& schemata = *evalType._schemata;
				auto& alias = schemata.GetAlias(evalType._alias);
				if (alias._bitFieldDecoder != ~0u) {
					SerializeValueWithDecoder(str, valueData, valueTypeDesc, schemata.GetBitFieldDecoder(alias._bitFieldDecoder));
					serializedViaDecoder = true;
				} else if (alias._enumDecoder != ~0u) {
					SerializeValueWithDecoder(str, valueData, valueTypeDesc, schemata.GetLiterals(alias._enumDecoder));
					serializedViaDecoder = true;
				}
			}
			if (!serializedViaDecoder)
				str << ImpliedTyping::AsString(valueData, valueTypeDesc);
			str << std::endl;
		} else if (formatter.TryBeginArray(arrayCount, evaluatedTypeId)) {
			str << StreamIndent{indent};
			formatter.GetEvaluationContext().SerializeEvaluatedType(str, evaluatedTypeId);
			str << " " << name << "[" << arrayCount << "]" << std::endl;
			for (unsigned c=0; c<arrayCount; ++c)
				SerializeValue(str, formatter, StringMeld<256>() << "<Element " << c << ">", indent+4);
			assert(formatter.PeekNext() == BinaryFormatter::Blob::EndArray);
			if (!formatter.TryEndArray())
				Throw(std::runtime_error("Expected end array"));
		} else
			Throw(std::runtime_error("Expected value type blob in SerializeValue"));
		return str;
	}

	std::ostream& SerializeBlock(std::ostream& str, BinaryFormatter& formatter, unsigned indent)
	{
		for (;;) {
			auto next = formatter.PeekNext();
			switch (next) {
			case BinaryFormatter::Blob::KeyedItem:
				{
					StringSection<> name;
					formatter.TryKeyedItem(name);
					SerializeValue(str, formatter, name, indent);
				}
				break;

			case BinaryFormatter::Blob::BeginBlock:
			case BinaryFormatter::Blob::BeginArray:
			case BinaryFormatter::Blob::EndArray:
			case BinaryFormatter::Blob::ValueMember:
				Throw(std::runtime_error("Unexpected blob in SerializeBlock"));

			case BinaryFormatter::Blob::EndBlock:
			case BinaryFormatter::Blob::None:
				return str;
			}
		}
	}
}
