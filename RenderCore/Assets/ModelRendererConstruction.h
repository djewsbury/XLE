// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../../Assets/AssetsCore.h"
#include "../../Math/Matrix.h"
#include "../../Utility/StringUtils.h"
#include <memory>

namespace std { template<typename T> class promise; template<typename T> class shared_future; }
namespace Assets { class OperationContext; }

namespace RenderCore { namespace Assets
{
	class ModelScaffold;
	class MaterialScaffold;
	class SkeletonScaffold;

	class ModelRendererConstruction : public std::enable_shared_from_this<ModelRendererConstruction>
	{
	public:
		class Internal;
		class ElementConstructor
		{
		public:
			ElementConstructor& SetModelAndMaterialScaffolds(StringSection<> model, StringSection<> material);
			ElementConstructor& SetModelAndMaterialScaffolds(std::shared_ptr<::Assets::OperationContext> opContext, StringSection<> model, StringSection<> material);
			
			ElementConstructor& SetModelScaffold(std::shared_future<std::shared_ptr<Assets::ModelScaffold>>, std::string initializer={});
			ElementConstructor& SetMaterialScaffold(std::shared_future<std::shared_ptr<Assets::MaterialScaffold>>, std::string initializer={});
			
			ElementConstructor& SetModelScaffold(const std::shared_ptr<Assets::ModelScaffold>&, std::string initializer={});
			ElementConstructor& SetMaterialScaffold(const std::shared_ptr<Assets::MaterialScaffold>&, std::string initializer={});

			ElementConstructor& SetElementToObject(const Float4x4&);
			ElementConstructor& SetDeformerBindPoint(uint64_t);

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
		void SetSkeletonScaffold(std::shared_ptr<::Assets::OperationContext>, StringSection<>);
		void SetSkeletonScaffold(std::shared_future<std::shared_ptr<Assets::SkeletonScaffold>>, std::string initializer={});
		void SetSkeletonScaffold(const std::shared_ptr<Assets::SkeletonScaffold>&);		
		std::shared_ptr<Assets::SkeletonScaffold> GetSkeletonScaffold() const;

		uint64_t GetHash() const;

		void FulfillWhenNotPending(std::promise<std::shared_ptr<ModelRendererConstruction>>&& promise);
		::Assets::AssetState GetAssetState() const;
		bool IsInvalidated() const;
		static std::shared_ptr<ModelRendererConstruction> Reconstruct(const ModelRendererConstruction& src, std::shared_ptr<::Assets::OperationContext> opContext = nullptr);

		ModelRendererConstruction();
		~ModelRendererConstruction();

		Internal& GetInternal() { return *_internal; }
		const Internal& GetInternal() const { return *_internal; }
	protected:
		std::unique_ptr<Internal> _internal;
	};

	class ModelRendererConstruction::ElementIterator
	{
	public:
		class Value
		{
		public:
			std::shared_ptr<Assets::ModelScaffold> GetModelScaffold() const;
			std::shared_ptr<Assets::MaterialScaffold> GetMaterialScaffold() const;
			std::optional<Float4x4> GetElementToObject() const;
			std::string GetModelScaffoldName() const;
			std::string GetMaterialScaffoldName() const;
			std::string GetElementName() const;
			std::optional<uint64_t> GetDeformerBindPoint() const;
			unsigned ElementId() const;
		private:
			Value();
			template<typename Type>
				using Iterator = typename std::vector<std::pair<unsigned, Type>>::iterator;
			using ModelScaffoldMarker = std::shared_future<std::shared_ptr<Assets::ModelScaffold>>;
			using ModelScaffoldPtr = std::shared_ptr<Assets::ModelScaffold>;
			using MaterialScaffoldMarker = std::shared_future<std::shared_ptr<Assets::MaterialScaffold>>;
			using MaterialScaffoldPtr = std::shared_ptr<Assets::MaterialScaffold>;
			Iterator<ModelScaffoldMarker> _msmi;
			Iterator<ModelScaffoldPtr> _mspi;
			Iterator<MaterialScaffoldMarker> _matsmi;
			Iterator<MaterialScaffoldPtr> _matspi;
			Iterator<Float4x4> _etoi;
			Iterator<uint64_t> _dbpi;
			Iterator<std::string> _ni;
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
		void UpdateElementIdx();
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

