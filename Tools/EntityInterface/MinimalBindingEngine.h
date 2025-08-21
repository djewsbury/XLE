// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../../Assets/DepVal.h"
#include "../../OSServices/Log.h"
#include "../../Utility/ParameterBox.h"
#include "../../Utility/MemoryUtils.h"
#include "../../Utility/FastParseValue.h"
#include <set>
#include <stack>

#include <chrono>

namespace EntityInterface
{
	class MinimalBindingEngine;
	
	enum class MinimalBindingValueType { Constant, Model, ViewAttached };
	template<typename UnderlyingType_>
		class MinimalBindingValue
	{
	public:
		using UnderlyingType = UnderlyingType_;
		MinimalBindingValueType _type = MinimalBindingValueType::Constant;
		UnderlyingType_ _constantValue = {};
		uint64_t _id = ~0ull;
		MinimalBindingEngine* _container = nullptr;

		std::optional<UnderlyingType_> QueryNonLayout() const;
		std::optional<UnderlyingType_> Query() const;
		std::optional<std::string> TryQueryNonLayoutAsString() const;

		void Set(UnderlyingType_) const;			// must be constant to make capturing an a lambda more convenient -- since mutable lambdas are hard to convert to std::functions<>s
		bool TrySetFromString(StringSection<>) const;

		MinimalBindingValue() = default;
		MinimalBindingValue(UnderlyingType_ t);
		template<typename T>
			MinimalBindingValue(const MinimalBindingValue<T>& t);
		MinimalBindingValue(MinimalBindingValueType type, uint64_t id, MinimalBindingEngine& container);
	};

	class MinimalBindingEngine
	{
	public:
		template<typename Type>
			MinimalBindingValue<Type> QueryModel(uint64_t id);

		static XLE_CONSTEVAL_OR_CONSTEXPR uint64_t ValueId(const char* str, const size_t len)
		{
			uint64_t id = DefaultSeed64;
			auto i = 0;
			while (i!=len) {
				auto q = i;
				while (q!=len && str[q] != '/') ++q;

				uint64_t asInt = 0;
				auto parseEnd = FastParseValue(MakeStringSection(str+i, str+q), asInt);
				if (parseEnd == str+q) {
					id = HashCombine(asInt, id);
				} else {
					id = ConstHash64(str+i, q-i, id);
				}

				if (q != len) ++q;
				i = q;
			}
			return id;
		}

		////////////////////////////////////////////////////////////////////////////////////////////////

		void ToggleEnable(uint64_t id)
		{
			auto i = _enabledModelValues.find(id);
			if (i == _enabledModelValues.end()) _enabledModelValues.insert(id);
			else _enabledModelValues.erase(i);
		}

		bool IsEnabled(uint64_t id) const { return _enabledModelValues.find(id) != _enabledModelValues.end(); }

		////////////////////////////////////////////////////////////////////////////////////////////////

		template<typename Type>
			void InitializeViewAttachedValue(uint64_t id, Type value)
		{
			if (!_viewAttachedValues.HasParameter(id))
				_viewAttachedValues.SetParameter(id, MakeOpaqueIteratorRange(value), ImpliedTyping::TypeOf<Type>());
		}

		template<typename Type>
			Type GetViewAttachedValue(uint64_t id)
		{
			return _viewAttachedValues.GetParameter<Type>(id).value();
		}

		template<typename Type>
			std::optional<Type> TryGetViewAttachedValue(uint64_t id)
		{
			return _viewAttachedValues.GetParameter<Type>(id);
		}

		template<typename Type>
			void SetViewAttachedValue(uint64_t id, Type newValue)
		{
			_viewAttachedValues.SetParameter(id, MakeOpaqueIteratorRange(newValue), ImpliedTyping::TypeOf<Type>());
		}

		template<typename Type>
			MinimalBindingValue<Type> ViewAttachedValue(uint64_t id)
		{
			return { MinimalBindingValueType::ViewAttached, id, *this };
		}

		////////////////////////////////////////////////////////////////////////////////////////////////

		template<typename Type>
			MinimalBindingValue<Type> ModelValue(uint64_t id)
		{
			assert(_modelMirrorValues.HasParameter(id));
			return { MinimalBindingValueType::Model, id, *this };
		}

		template<typename Type>
			MinimalBindingValue<Type> ModelValue(uint64_t id, Type defaultValue)
		{
			if (!_modelMirrorValues.HasParameter(id))
				SetModelValue(id, MakeOpaqueIteratorRange(defaultValue), ImpliedTyping::TypeOf<Type>());
			return { MinimalBindingValueType::Model, id, *this };
		}

		void SetModelValue(uint64_t id, IteratorRange<const void*> data, const ImpliedTyping::TypeDesc& type)
		{
			_modelMirrorValues.SetParameter(id, data, type);
		}

		void SetModelValue(uint64_t id, StringSection<> str)
		{
			auto type = ImpliedTyping::TypeOf<char>();
			type._arrayCount = (uint32_t)str.size();
			type._typeHint = ImpliedTyping::TypeHint::String;
			_modelMirrorValues.SetParameter(id, {str.begin(), str.end()}, type);
		}

		std::optional<ImpliedTyping::VariantNonRetained> TryGetModelValue(uint64_t id)
		{
			if (_enabledModelValues.find(id) == _enabledModelValues.end())
				return {};

			auto type = _modelMirrorValues.GetParameterType(id);
			if (type._type == ImpliedTyping::TypeCat::Void)
				return {};
			auto data = _modelMirrorValues.GetParameterRawValue(id);
			return ImpliedTyping::VariantNonRetained { type, data };
		}

		////////////////////////////////////////////////////////////////////////////////////////////////

		void InvalidateModel() { _modelDependencyValidation.IncreaseValidationIndex(); }
		void InvalidateLayout() { _layoutDependencyValidation.IncreaseValidationIndex(); }

		const ::Assets::DependencyValidation& GetModelDependencyValidation() const { return _modelDependencyValidation; }
		const ::Assets::DependencyValidation& GetLayoutDependencyValidation() const { return _layoutDependencyValidation; }

		MinimalBindingEngine()
		{
			auto& depValSys = ::Assets::GetDepValSys();
			_modelDependencyValidation = depValSys.Make();
			_layoutDependencyValidation = depValSys.Make();
		}
		~MinimalBindingEngine() = default;

	private:
		ParameterBox _viewAttachedValues;

		ParameterBox _modelMirrorValues;
		std::set<uint64_t> _layoutInvalidatingModelValues;
		std::set<uint64_t> _enabledModelValues;

		::Assets::DependencyValidation _modelDependencyValidation, _layoutDependencyValidation;

		template<typename T> friend class MinimalBindingValue;
	};

	namespace Literals
	{
		XLE_CONSTEVAL_OR_CONSTEXPR uint64_t operator""_mv(const char* str, const size_t len) never_throws { return MinimalBindingEngine::ValueId(str, len); }
	}

	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


	template<typename UnderlyingType>
		std::optional<UnderlyingType> MinimalBindingValue<UnderlyingType>::QueryNonLayout() const
	{
		switch (_type) {
		default:
			assert(0);
		case MinimalBindingValueType::Constant:
			return _constantValue;
		case MinimalBindingValueType::Model:
			assert(_container);
			return _container->_modelMirrorValues.GetParameter<UnderlyingType>(_id);
		case MinimalBindingValueType::ViewAttached:
			assert(_container);
			return _container->_viewAttachedValues.GetParameter<UnderlyingType>(_id);
		}
	}

	template<typename UnderlyingType>
		std::optional<UnderlyingType> MinimalBindingValue<UnderlyingType>::Query() const
	{
		if (_type == MinimalBindingValueType::Model) {
			// record this as a model value that invalidates the layout
			assert(_container);
			_container->_layoutInvalidatingModelValues.insert(_id);
		}
		return QueryNonLayout();
	}

	template<typename UnderlyingType>
		std::optional<std::string> MinimalBindingValue<UnderlyingType>::TryQueryNonLayoutAsString() const
	{
		switch (_type) {
		default:
			assert(0);
		case MinimalBindingValueType::Constant:
			return ImpliedTyping::AsString(_constantValue);
		case MinimalBindingValueType::Model:
			assert(_container);
			return _container->_modelMirrorValues.GetParameterAsString(_id);
		case MinimalBindingValueType::ViewAttached:
			assert(_container);
			return _container->_viewAttachedValues.GetParameterAsString(_id);
		}
	}

	template<typename UnderlyingType>
		void MinimalBindingValue<UnderlyingType>::Set(UnderlyingType newValue) const
	{
		switch (_type) {
		default:
		case MinimalBindingValueType::Constant:
			assert(0);
			break;

		case MinimalBindingValueType::Model:
			assert(_container);
			_container->_modelMirrorValues.SetParameter(_id, newValue);
			if (_container->_layoutInvalidatingModelValues.find(_id) != _container->_layoutInvalidatingModelValues.end())
				_container->_layoutDependencyValidation.IncreaseValidationIndex();
			_container->_modelDependencyValidation.IncreaseValidationIndex();
			break;

		case MinimalBindingValueType::ViewAttached:
			assert(_container);
			_container->_viewAttachedValues.SetParameter(_id, newValue);
			break;
		}
	}

	template<typename UnderlyingType>
		bool MinimalBindingValue<UnderlyingType>::TrySetFromString(StringSection<> editBoxResult) const
	{
		if (_type != MinimalBindingValueType::Model && _type != MinimalBindingValueType::ViewAttached) {
			assert(0);
			return false;
		}

		if (editBoxResult.IsEmpty()) return false;
		UnderlyingType newValue;
		const auto* parseEnd = FastParseValue(editBoxResult, newValue);
		if (parseEnd == editBoxResult.end()) {
			Set(newValue);
			return true;
		} else {
			Log(Debug) << "Failed to parse (" << editBoxResult << ") to type (" << typeid(UnderlyingType).name() << ")" << std::endl;
			return false;
		}
	}

	template<typename UnderlyingType>
		MinimalBindingValue<UnderlyingType>::MinimalBindingValue(UnderlyingType t)
	: _type(MinimalBindingValueType::Constant), _constantValue(t)
	{}

	template<typename UnderlyingType>
		template<typename T>
			MinimalBindingValue<UnderlyingType>::MinimalBindingValue(const MinimalBindingValue<T>& copyFrom)
	{
		_type = copyFrom._type;
		_id = copyFrom._id;
		_container = copyFrom._container;
		_constantValue = (UnderlyingType)copyFrom._constantValue;		// possible cast here
	}

	template<typename UnderlyingType>
		MinimalBindingValue<UnderlyingType>::MinimalBindingValue(MinimalBindingValueType type, uint64_t id, MinimalBindingEngine& container)
	: _type(type), _id(id), _container(&container)
	{}
	
}
