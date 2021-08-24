// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "BinarySchemata.h"
#include "../Assets/AssetUtils.h"
#include "../Utility/ImpliedTyping.h"
#include "../Utility/ParameterBox.h"
#include <stack>

namespace Formatters
{
	class EvaluationContext;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	class BinaryFormatter
	{
	public:
		enum class Blob { KeyedItem, ValueMember, BeginBlock, EndBlock, BeginArray, EndArray, None };
		using InteriorSection = StringSection<>;
		
		Blob PeekNext();
		bool TryKeyedItem(StringSection<>& name);
		bool TryPeekKeyedItem(StringSection<>& name);
		bool TryBeginBlock(unsigned& evaluatedTypeId);
		bool TryEndBlock();
		bool TryBeginArray(unsigned& count, unsigned& evaluatedTypeId);
		bool TryEndArray();
		bool TryValue(IteratorRange<const void*>& data, ImpliedTyping::TypeDesc& typeDesc, unsigned& evaluatedTypeId);

		IteratorRange<const void*> SkipArrayElements(unsigned count);
		IteratorRange<const void*> SkipNextBlob();

		void PushPattern(BinarySchemata::BlockDefinitionId blockDefId, IteratorRange<const int64_t*> templateParams = {}, uint32_t templateParamsTypeField = 0u);

		EvaluationContext& GetEvaluationContext() const { return *_evalContext; }
		IteratorRange<const void*> GetRemainingData() const { return _dataIterator; }

		BinaryFormatter(EvaluationContext& evalContext, IteratorRange<const void*> data);
	private:
		struct BlockContext
		{
			std::stack<int64_t> _valueStack;
			std::stack<unsigned> _typeStack;
			const BlockDefinition* _definition;
			IteratorRange<const unsigned*> _cmdsIterator;
			unsigned _pendingArrayMembers = 0;
			unsigned _pendingArrayType = ~0u;
			bool _pendingEndArray = false;

			std::string _parsingBlockName;
			IteratorRange<const int64_t*> _parsingTemplateParams;
			uint32_t _parsingTemplateParamsTypeField;

			ParameterBox _localEvalContext;
			std::vector<uint64_t> _nonIntegerLocalVariables;
		};
		std::stack<BlockContext> _blockStack;
		EvaluationContext* _evalContext = nullptr;
		IteratorRange<const void*> _dataIterator;

		Blob _queuedNext = Blob::None;
	};

	void SkipUntilEndBlock(BinaryFormatter&);
	unsigned RequireBeginBlock(BinaryFormatter&);
	void RequireEndBlock(BinaryFormatter&);
	StringSection<> RequireKeyedItem(BinaryFormatter&);
	std::pair<unsigned, unsigned> RequireBeginArray(BinaryFormatter&);
	void RequireEndArray(BinaryFormatter&);
	template<typename Type>
		Type RequireValue(BinaryFormatter&);
	std::ostream& SerializeBlock(std::ostream& str, BinaryFormatter& formatter, unsigned indent = 0);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	class EvaluationContext
	{
	public:
		struct EvaluatedType
		{
			ImpliedTyping::TypeDesc _valueTypeDesc { ImpliedTyping::TypeCat::Void };
			BinarySchemata::BlockDefinitionId _blockDefinition = ~0u;
			BinarySchemata::AliasId _alias = ~0u;
			std::vector<int64_t> _params;
			uint32_t _paramTypeField = 0;

			friend bool operator==(const EvaluatedType& lhs, const EvaluatedType& rhs);
		};

		using EvaluatedTypeToken = unsigned;
		EvaluatedTypeToken GetEvaluatedType(StringSection<> baseName, IteratorRange<const int64_t*> parameters = {}, unsigned typeBitField = 0);
		EvaluatedTypeToken GetEvaluatedType(ImpliedTyping::TypeCat typeCat);
		EvaluatedTypeToken GetEvaluatedType(const EvaluatedType& evalType);
		EvaluatedTypeToken GetEvaluatedType(
			unsigned baseNameToken, IteratorRange<const unsigned*> paramTypeCodes, 
			const BlockDefinition& blockDef, 
			std::stack<unsigned>& typeStack, std::stack<int64_t>& valueStack, 
			IteratorRange<const int64_t*> parsingTemplateParams, uint32_t parsingTemplateParamsTypeField);

		const EvaluatedType& GetEvaluatedTypeDesc(EvaluatedTypeToken evalTypeId) const;
		std::optional<size_t> TryCalculateFixedSize(EvaluatedTypeToken evalTypeId);

		std::ostream& SerializeEvaluatedType(std::ostream& str, unsigned typeId) const;

		void SetGlobalParameter(StringSection<> name, int64_t value);
		ParameterBox& GetGlobalParameterBox();
		std::optional<int64_t> GetGlobalParameter(uint64_t hash);

		const BinarySchemata& GetSchemata() const { return *_definitions; }

		EvaluationContext(BinarySchemata& schemata);
		~EvaluationContext();

	private:
		std::vector<EvaluatedType> _evaluatedTypes;
		BinarySchemata* _definitions = nullptr;
		ParameterBox _globalState;

		struct CalculatedSizeState
		{
			enum State { Uncalculated, DynamicSize, FixedSize };
			State _state = CalculatedSizeState::Uncalculated;
			size_t _fixedSize = 0;
		};
		mutable std::vector<CalculatedSizeState> _calculatedSizeStates;
	};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	class BinaryMemberToken;
	class BinaryMemberIterator;
	class BinaryBlockMatch
	{
	public:
		struct Member
		{
			IteratorRange<const void*> _data;
			ImpliedTyping::TypeDesc _typeDesc = { ImpliedTyping::TypeCat::Void };
			EvaluationContext::EvaluatedTypeToken _type;
			static constexpr unsigned RootParentMarker = ~0u;
			unsigned _parent = RootParentMarker;
			unsigned _arrayCount = 0;
			bool _isArray = false;
			std::string _stringName;
		};
		std::vector<std::pair<uint64_t, Member>> _members;

		BinaryMemberIterator begin_members() const;
		BinaryMemberIterator end_members() const;
		IteratorRange<BinaryMemberIterator> members() const;

		BinaryMemberToken Find(StringSection<>) const;
		BinaryMemberToken Find(unsigned) const;
		BinaryMemberToken operator[](StringSection<> name) const;
		BinaryMemberToken operator[](const char* name) const;
		BinaryMemberToken operator[](const std::string& name) const;
		BinaryMemberToken operator[](unsigned idx) const;

		void ParseBlock(BinaryFormatter& formatter, unsigned parentId = ~0u);
		void ParseValue(BinaryFormatter& formatter, const std::string& name, unsigned parentId = ~0u);

		BinaryBlockMatch(BinaryFormatter& formatter);
		BinaryBlockMatch(const EvaluationContext& evalContext);
		BinaryBlockMatch();

	private:
		const EvaluationContext* _evalContext;
	};

	class BinaryMemberIterator
	{
	public:
		BinaryMemberToken get() const;
		BinaryMemberToken operator*() const;
		friend bool operator==(const BinaryMemberIterator& lhs, const BinaryMemberIterator& rhs)
		{
			assert(lhs._evalContext == rhs._evalContext);
			assert(lhs._parent == rhs._parent);
			return lhs._i == rhs._i;
		}
		friend bool operator!=(const BinaryMemberIterator& lhs, const BinaryMemberIterator& rhs)
		{
			return !operator==(lhs, rhs);
		}
		void operator++() 
		{
			if (_i == _containingRange.end()) return;
			++_i;
			while (_i != _containingRange.end() && _i->second._parent != _parent) ++_i;
		}
		void operator++(int) { operator++(); }

		using UnderlyingIterator = std::vector<std::pair<uint64_t, BinaryBlockMatch::Member>>::const_iterator;
		BinaryMemberIterator(
			UnderlyingIterator i, 
			IteratorRange<UnderlyingIterator> containingRange,
			unsigned parent, const EvaluationContext& evalContext)
		: _i(i), _containingRange(containingRange), _parent(parent), _evalContext(&evalContext) {}
	private:
		UnderlyingIterator _i;
		IteratorRange<UnderlyingIterator> _containingRange;
		const EvaluationContext* _evalContext;
		unsigned _parent = ~0u;
	};

	class BinaryMemberToken
	{
	public:
		const EvaluationContext::EvaluatedType& GetType() const { return _evalContext->GetEvaluatedTypeDesc(_i->second._type); }
		EvaluationContext::EvaluatedTypeToken GetTypeToken() const { return _i->second._type; }
		const std::string& GetTypeBaseName() const;

		const std::string& GetStringName() const { return _i->second._stringName; }
		IteratorRange<const void*> GetData() const { return _i->second._data; }
		bool IsArray() const { return _i->second._isArray; }
		unsigned GetArrayCount() const { return _i->second._arrayCount; }
		const EvaluationContext& GetEvaluationContext() const { return *_evalContext; }

		template<typename Result>
			std::optional<Result> As()
		{
			if (_evalContext == nullptr) return {};
			Result result;
			if (!ImpliedTyping::Cast(MakeOpaqueIteratorRange(result), ImpliedTyping::TypeOf<Result>(), GetData(), _i->second._typeDesc))
				return {};
			return result;
		}

		template<>
			std::optional<std::string> As()
		{
			if (_evalContext == nullptr) return {};
			return ImpliedTyping::AsString(GetData(), _i->second._typeDesc);
		}

		BinaryMemberIterator begin_children() const
		{
			auto parentId = (unsigned)std::distance(_containingRange.begin(), _i);
			auto ci = _containingRange.begin();
			while (ci != _containingRange.end() && ci->second._parent != parentId) ++ci;
			return BinaryMemberIterator{ci, _containingRange, parentId, *_evalContext};
		}
		BinaryMemberIterator end_children() const
		{
			auto parentId = (unsigned)std::distance(_containingRange.begin(), _i);
			return BinaryMemberIterator{_containingRange.end(), _containingRange, parentId, *_evalContext};
		}
		IteratorRange<BinaryMemberIterator> children() const { return { begin_children(), end_children() }; }

		BinaryMemberToken Find(StringSection<> name) const
		{
			for (const auto&c:children())
				if (XlEqString(name, c.GetStringName()))
					return c;
			return {};
		}

		BinaryMemberToken Find(unsigned idx) const
		{
			unsigned c=0;
			auto end = end_children();
			for (auto i=begin_children(); i!=end; ++i, c++)
				if (c == idx) return *i;
			return {};
		}
		BinaryMemberToken operator[](StringSection<> name) const { return Find(name); }
		BinaryMemberToken operator[](const char* name) const { return Find(name); }
		BinaryMemberToken operator[](const std::string& name) const { return Find(name); }
		BinaryMemberToken operator[](unsigned idx) const { return Find(idx); }
		operator bool() { return _evalContext != nullptr; }

		using UnderlyingIterator = std::vector<std::pair<uint64_t, BinaryBlockMatch::Member>>::const_iterator;
		BinaryMemberToken(UnderlyingIterator i, IteratorRange<UnderlyingIterator> containingRange, const EvaluationContext& evalContext)
		: _i(i), _containingRange(containingRange), _evalContext(&evalContext) {}
		BinaryMemberToken() : _containingRange(UnderlyingIterator{}, UnderlyingIterator{}), _evalContext(nullptr) {}
	private:
		UnderlyingIterator _i;
		IteratorRange<UnderlyingIterator> _containingRange;
		const EvaluationContext* _evalContext;
	};

	inline BinaryMemberIterator BinaryBlockMatch::begin_members() const
	{
		auto parentId = ~0u;
		auto ci = _members.begin();
		while (ci != _members.end() && ci->second._parent != parentId) ++ci;
		return BinaryMemberIterator{ci, MakeIteratorRange(_members), parentId, *_evalContext};
	}
	inline BinaryMemberIterator BinaryBlockMatch::end_members() const
	{
		return BinaryMemberIterator{_members.end(), MakeIteratorRange(_members), ~0u, *_evalContext};
	}

	inline BinaryMemberToken BinaryBlockMatch::Find(StringSection<> name) const
	{
		for (const auto&c:members())
			if (XlEqString(name, c.GetStringName()))
				return c;
		return {};
	}

	inline BinaryMemberToken BinaryBlockMatch::Find(unsigned idx) const
	{
		unsigned c=0;
		for (auto i=begin_members(); i!=end_members(); ++i, c++)
			if (c == idx) return *i;
		return {};
	}

	inline BinaryMemberToken BinaryBlockMatch::operator[](StringSection<> name) const { return Find(name); }
	inline BinaryMemberToken BinaryBlockMatch::operator[](const char* name) const { return Find(name); }
	inline BinaryMemberToken BinaryBlockMatch::operator[](const std::string& name) const { return Find(name); }
	inline BinaryMemberToken BinaryBlockMatch::operator[](unsigned idx) const { return Find(idx); }
	inline IteratorRange<BinaryMemberIterator> BinaryBlockMatch::members() const { return {begin_members(), end_members()}; }

	inline BinaryMemberToken BinaryMemberIterator::get() const { return BinaryMemberToken{_i, _containingRange, *_evalContext}; }
	inline BinaryMemberToken BinaryMemberIterator::operator*() const { return get(); }

	template<typename Type>
		Type RequireValue(BinaryFormatter& formatter)
	{
		IteratorRange<const void*> data;
		ImpliedTyping::TypeDesc typeDesc;
		unsigned evaluatedTypeId;
		if (!formatter.TryValue(data, typeDesc, evaluatedTypeId))
			Throw(std::runtime_error("Unexpected blob while looking for value in binary formatter"));
		Type result;
		bool castSuccess = ImpliedTyping::Cast(
			MakeOpaqueIteratorRange(result), ImpliedTyping::TypeOf<Type>(),
			data, typeDesc);
		if (!castSuccess)
			Throw(std::runtime_error("Could not convert value to the required type in binary formatter"));
		return result;
	}
	
}
