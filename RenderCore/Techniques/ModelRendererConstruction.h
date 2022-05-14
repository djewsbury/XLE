// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../../Assets/AssetsCore.h"
#include "../../Math/Matrix.h"
#include "../../Utility/StringUtils.h"
#include <memory>

namespace std { template<typename T> class promise; }

namespace RenderCore { namespace Assets
{
	class ModelScaffold;
	class MaterialScaffold;
	class SkeletonScaffold;
}}

namespace RenderCore { namespace Techniques
{
	class ModelRendererConstruction : public std::enable_shared_from_this<ModelRendererConstruction>
	{
	public:
		class Internal;
		class ElementConstructor
		{
		public:
			ElementConstructor& SetModelAndMaterialScaffolds(StringSection<> model, StringSection<> material);
			
			ElementConstructor& SetModelScaffold(const ::Assets::PtrToMarkerPtr<Assets::ModelScaffold>&);
			ElementConstructor& SetMaterialScaffold(const ::Assets::PtrToMarkerPtr<Assets::MaterialScaffold>&);
			
			ElementConstructor& SetModelScaffold(const std::shared_ptr<Assets::ModelScaffold>&);
			ElementConstructor& SetMaterialScaffold(const std::shared_ptr<Assets::MaterialScaffold>&);

			ElementConstructor& SetRootTransform(const Float4x4&);

			ElementConstructor& SetName(const std::string&);
		private:
			unsigned _elementId = ~0u;
			Internal* _internal = nullptr;
			ElementConstructor(unsigned elementId, Internal& internal) : _elementId(elementId), _internal(&internal) {}
			ElementConstructor() {}
			friend class ModelRendererConstruction;
		};

		ElementConstructor AddElement();

		class ElementIterator;
		ElementIterator begin() const;
		ElementIterator end() const;
		ElementIterator GetElement(unsigned idx) const;
		unsigned GetElementCount() const;

		void SetSkeletonScaffold(StringSection<>);
		void SetSkeletonScaffold(const ::Assets::PtrToMarkerPtr<Assets::SkeletonScaffold>&);
		void SetSkeletonScaffold(const std::shared_ptr<Assets::SkeletonScaffold>&);
		std::shared_ptr<Assets::SkeletonScaffold> GetSkeletonScaffold() const;

		uint64_t GetHash() const;

		void FulfillWhenNotPending(std::promise<std::shared_ptr<ModelRendererConstruction>>&& promise);
		::Assets::AssetState GetAssetState() const;

		ModelRendererConstruction();
		~ModelRendererConstruction();

		Internal& GetInternal() { return *_internal; }
		const Internal& GetInternal() const { return *_internal; }
	protected:
		std::unique_ptr<Internal> _internal;
	};

	class ModelRendererConstruction::Internal
	{
	public:
		using ElementId = unsigned;
		using ModelScaffoldMarker = ::Assets::PtrToMarkerPtr<Assets::ModelScaffold>;
		using ModelScaffoldPtr = std::shared_ptr<Assets::ModelScaffold>;
		using MaterialScaffoldMarker = ::Assets::PtrToMarkerPtr<Assets::MaterialScaffold>;
		using MaterialScaffoldPtr = std::shared_ptr<Assets::MaterialScaffold>;

		std::vector<std::pair<ElementId, ModelScaffoldMarker>> _modelScaffoldMarkers;
		std::vector<std::pair<ElementId, ModelScaffoldPtr>> _modelScaffoldPtrs;
		std::vector<std::pair<ElementId, MaterialScaffoldMarker>> _materialScaffoldMarkers;
		std::vector<std::pair<ElementId, MaterialScaffoldPtr>> _materialScaffoldPtrs;
		std::vector<std::pair<ElementId, std::string>> _names;
		unsigned _elementCount = 0;

		::Assets::PtrToMarkerPtr<Assets::SkeletonScaffold> _skeletonScaffoldMarker;
		std::shared_ptr<Assets::SkeletonScaffold> _skeletonScaffoldPtr;
		uint64_t _skeletonScaffoldHashValue = 0u;

		bool _sealed = false;

		std::vector<uint64_t> _elementHashValues;
		mutable uint64_t _hash = 0ull;
		bool _disableHash = false;
	};

	class ModelRendererConstruction::ElementIterator
	{
	public:
		class Value
		{
		public:
			std::shared_ptr<Assets::ModelScaffold> GetModelScaffold() const;
			std::shared_ptr<Assets::MaterialScaffold> GetMaterialScaffold() const;
			std::string GetModelScaffoldName() const;
			std::string GetMaterialScaffoldName() const;
			unsigned ElementId() const;
		private:
			Value();
			template<typename Type>
				using Iterator = typename std::vector<std::pair<unsigned, Type>>::iterator;
			Iterator<ModelRendererConstruction::Internal::ModelScaffoldMarker> _msmi;
			Iterator<ModelRendererConstruction::Internal::ModelScaffoldPtr> _mspi;
			Iterator<ModelRendererConstruction::Internal::MaterialScaffoldMarker> _matsmi;
			Iterator<ModelRendererConstruction::Internal::MaterialScaffoldPtr> _matspi;
			unsigned _elementId = 0;
			Internal* _internal = nullptr;
			friend class ElementIterator;
			friend class ModelRendererConstruction;
		};

		ElementIterator& operator++();
		const Value& operator*() const;
		const Value* operator->() const;
		friend bool operator==(const ElementIterator&, const ElementIterator&);
		friend bool operator!=(const ElementIterator&, const ElementIterator&);

	private:
		ElementIterator();
		Value _value;
		friend class ModelRendererConstruction;
		bool IsEqual(const ElementIterator& other) const;
	};

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	inline unsigned ModelRendererConstruction::ElementIterator::Value::ElementId() const { return _elementId; }

	inline auto ModelRendererConstruction::ElementIterator::operator*() const -> const Value& { return _value; }
	inline auto ModelRendererConstruction::ElementIterator::operator->() const -> const Value* { return &_value; }

	inline bool ModelRendererConstruction::ElementIterator::IsEqual(const ElementIterator& other) const
	{
		assert(_value._internal == other._value._internal);
		return _value._elementId == other._value._elementId;
	}

	inline bool operator==(const ModelRendererConstruction::ElementIterator& lhs, const ModelRendererConstruction::ElementIterator& rhs)
	{
		return lhs.IsEqual(rhs);
	}

	inline bool operator!=(const ModelRendererConstruction::ElementIterator& lhs, const ModelRendererConstruction::ElementIterator& rhs)
	{
		return !lhs.IsEqual(rhs);
	}

}}

