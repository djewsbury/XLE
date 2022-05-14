// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../../Utility/IteratorUtils.h"

namespace RenderCore { namespace Assets
{
	static constexpr unsigned s_scaffoldCmdBegin_TransformationMachine = 0x500;
	static constexpr unsigned s_scaffoldCmdBegin_ModelMachine = 0x1000;
	static constexpr unsigned s_scaffoldCmdBegin_SkeletonMachine = 0x1500;
	static constexpr unsigned s_scaffoldCmdBegin_MaterialMachine = 0x2000;
	static constexpr unsigned s_scaffoldCmdBegin_ScaffoldMachine = 0x2500;
	static constexpr unsigned s_scaffoldCmdBegin_DrawableConstructor = 0x3000;

	enum class ScaffoldCommand : uint32_t
	{
		BeginSubModel = s_scaffoldCmdBegin_ScaffoldMachine,

		Geo,					// pointer to stream of GeoCommand
		Material,				// pointer to stream of MaterialCommand
		Skeleton,				// pointer to stream of TransformationCommand
		ShaderPatchCollection, 	// serialized ShaderPatchCollection
		ModelCommandStream,		// pointer to stream of ModelCommand

		MaterialNameDehash,
		DefaultPoseData,
		ModelRootData
	};

	// class IScaffoldNavigation;
	class ScaffoldCmdIterator
	{
	public:
		class Value
		{
		public:
			IteratorRange<const void*> RawData() const;
			uint32_t Cmd() const;
			uint32_t BlockSize() const;
			template<typename Type> const Type& As() const;
		private:
			Value(IteratorRange<const void*> block);
			Value();
			IteratorRange<const void*> _data;
			friend class ScaffoldCmdIterator;
		};

		ScaffoldCmdIterator& operator++();
		const Value& operator*() const;
		const Value* operator->() const;
		friend bool operator==(const ScaffoldCmdIterator&, const ScaffoldCmdIterator&);
		friend bool operator!=(const ScaffoldCmdIterator&, const ScaffoldCmdIterator&);

		ScaffoldCmdIterator(IteratorRange<const void*> data);
		ScaffoldCmdIterator();
		ScaffoldCmdIterator(nullptr_t);

	private:
		Value _value;

		bool IsEqual(const ScaffoldCmdIterator& other) const;
	};

	IteratorRange<ScaffoldCmdIterator> MakeScaffoldCmdRange(IteratorRange<const void*> data);

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	inline IteratorRange<const void*> ScaffoldCmdIterator::Value::RawData() const
	{
		const auto prefixSize = sizeof(uint32_t)*2;
		assert(_data.size() >= prefixSize);
		auto blockSize = *(const uint32_t*)PtrAdd(_data.begin(), sizeof(uint32_t));
		assert(_data.size() >= prefixSize+blockSize);
		return {PtrAdd(_data.begin(), prefixSize), PtrAdd(_data.begin(), prefixSize+blockSize)};
	}
	inline uint32_t ScaffoldCmdIterator::Value::Cmd() const 
	{
		assert(_data.size() >= sizeof(uint32_t));
		return *(const uint32_t*)_data.begin();
	}
	inline uint32_t ScaffoldCmdIterator::Value::BlockSize() const
	{
		const auto prefixSize = sizeof(uint32_t)*2;
		assert(_data.size() >= prefixSize);
		auto blockSize = *(const uint32_t*)PtrAdd(_data.begin(), sizeof(uint32_t));
		assert(_data.size() >= prefixSize+blockSize);
		return blockSize;
	}
	template<typename Type> const Type& ScaffoldCmdIterator::Value::As() const
	{
		auto rawData = RawData();
		assert(rawData.size() == sizeof(Type));
		return *(const Type*)rawData.begin();
	}

	inline ScaffoldCmdIterator::Value::Value(IteratorRange<const void*> block) : _data(block) {}
	inline ScaffoldCmdIterator::Value::Value() {}

	inline ScaffoldCmdIterator& ScaffoldCmdIterator::operator++()
	{
		assert(!_value._data.empty());
		const auto prefixSize = sizeof(uint32_t)*2;
		assert(_value._data.size() >= prefixSize+_value.BlockSize());
		_value._data.first = PtrAdd(_value._data.begin(), prefixSize+_value.BlockSize());
		return *this;
	}

	inline auto ScaffoldCmdIterator::operator*() const -> const Value& { return _value; }
	inline auto ScaffoldCmdIterator::operator->() const -> const Value* { return &_value; }
	inline bool ScaffoldCmdIterator::IsEqual(const ScaffoldCmdIterator& other) const
	{ 
		// assert(_navigation == other._navigation);
		return _value._data.begin() == other._value._data.begin();
	}
	inline bool operator==(const ScaffoldCmdIterator& lhs, const ScaffoldCmdIterator& rhs)
	{
		return lhs.IsEqual(rhs);
	}
	inline bool operator!=(const ScaffoldCmdIterator& lhs, const ScaffoldCmdIterator& rhs)
	{
		return !lhs.IsEqual(rhs);
	}

	inline ScaffoldCmdIterator::ScaffoldCmdIterator(IteratorRange<const void*> data)
	: _value(data)
	{}
	inline ScaffoldCmdIterator::ScaffoldCmdIterator() {}
	inline ScaffoldCmdIterator::ScaffoldCmdIterator(nullptr_t) {}

	inline IteratorRange<ScaffoldCmdIterator> MakeScaffoldCmdRange(IteratorRange<const void*> data)
	{
		return {
			ScaffoldCmdIterator(data),
			ScaffoldCmdIterator({data.end(), data.end()})
		};
	}

}}

