// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../../Assets/ChunkFileContainer.h"
#include "../../Assets/AssetsCore.h"
#include "../../Assets/DepVal.h"
#include "../../Math/Matrix.h"
#include "../../Utility/IteratorUtils.h"

namespace Assets {  class IFileInterface; }
namespace std { template<typename T> class promise; }

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

		// IScaffoldNavigation* Navigation() const;

		// ScaffoldCmdIterator(IteratorRange<const void*> data, IScaffoldNavigation& navigation);
		ScaffoldCmdIterator(IteratorRange<const void*> data);
		ScaffoldCmdIterator();
		ScaffoldCmdIterator(nullptr_t);

	private:
		Value _value;
		// IScaffoldNavigation* _navigation = nullptr;

		bool IsEqual(const ScaffoldCmdIterator& other) const;
	};

	// IteratorRange<ScaffoldCmdIterator> MakeScaffoldCmdRange(IteratorRange<const void*> data, IScaffoldNavigation& navigation);
	IteratorRange<ScaffoldCmdIterator> MakeScaffoldCmdRange(IteratorRange<const void*> data);

#if 0
	class ScaffoldAsset
	{
	public:
		IteratorRange<ScaffoldCmdIterator> GetCmdStream() const;

		const ::Assets::DependencyValidation& GetDependencyValidation() const { return _depVal; }
		std::shared_ptr<::Assets::IFileInterface> OpenLargeBlocks() const;

		ScaffoldAsset();
		ScaffoldAsset(IteratorRange<::Assets::ArtifactRequestResult*> chunks, const ::Assets::DependencyValidation& depVal);
		~ScaffoldAsset();

		static const ::Assets::ArtifactRequest ChunkRequests[2];
	private:
		std::unique_ptr<uint8[], PODAlignedDeletor>		_rawMemoryBlock;
		size_t											_rawMemoryBlockSize = 0;
		::Assets::ArtifactReopenFunction				_largeBlocksReopen;
		::Assets::DependencyValidation					_depVal;
	};

	class ShaderPatchCollection;
#endif

#if 0
	class IScaffoldNavigation
	{
	public:
		using GeoId = unsigned;
		using MaterialId = uint64_t;
		using ShaderPatchCollectionId = uint64_t;

		virtual IteratorRange<ScaffoldCmdIterator> GetSubModel() = 0;
		virtual IteratorRange<ScaffoldCmdIterator> GetGeoMachine(GeoId) = 0;
		virtual IteratorRange<ScaffoldCmdIterator> GetMaterialMachine(MaterialId) = 0;
		virtual const ShaderPatchCollection* GetShaderPatchCollection(ShaderPatchCollectionId) = 0;

		enum class GeoBufferType { Vertex, Index, AnimatedVertex, SkeletonBinding };
		virtual const IteratorRange<const void*> GetGeometryBufferData(GeoId, GeoBufferType) = 0;		// or maybe async access?

		const std::string& GetInitializer() const { return _initializer; }

		virtual ~IScaffoldNavigation() = default;
	private:
		std::string _initializer;
	};

	class ScaffoldAsset;
	std::shared_ptr<IScaffoldNavigation> CreateSimpleScaffoldNavigation(std::shared_ptr<ScaffoldAsset> scaffoldAsset);
#endif

	class ModelScaffold;
	class MaterialScaffold;
	class SkeletonScaffold;

	class RendererConstruction : public std::enable_shared_from_this<RendererConstruction>
	{
	public:
		class Internal;
		class ElementConstructor
		{
		public:
			ElementConstructor& SetModelAndMaterialScaffolds(StringSection<> model, StringSection<> material);
			
			ElementConstructor& SetModelScaffold(const ::Assets::PtrToMarkerPtr<ModelScaffold>&);
			ElementConstructor& SetMaterialScaffold(const ::Assets::PtrToMarkerPtr<MaterialScaffold>&);
			
			ElementConstructor& SetModelScaffold(const std::shared_ptr<ModelScaffold>&);
			ElementConstructor& SetMaterialScaffold(const std::shared_ptr<MaterialScaffold>&);

			ElementConstructor& SetRootTransform(const Float4x4&);

			ElementConstructor& SetName(const std::string&);
		private:
			unsigned _elementId = ~0u;
			Internal* _internal = nullptr;
			ElementConstructor(unsigned elementId, Internal& internal) : _elementId(elementId), _internal(&internal) {}
			ElementConstructor() {}
			friend class RendererConstruction;
		};

		ElementConstructor AddElement();

		class ElementIterator;
		ElementIterator begin() const;
		ElementIterator end() const;
		ElementIterator GetElement(unsigned idx) const;
		unsigned GetElementCount() const;

		void SetSkeletonScaffold(StringSection<>);
		void SetSkeletonScaffold(const ::Assets::PtrToMarkerPtr<SkeletonScaffold>&);
		void SetSkeletonScaffold(const std::shared_ptr<SkeletonScaffold>&);
		std::shared_ptr<SkeletonScaffold> GetSkeletonScaffold() const;

		uint64_t GetHash() const;

		void FulfillWhenNotPending(std::promise<std::shared_ptr<RendererConstruction>>&& promise);
		::Assets::AssetState GetAssetState() const;

		RendererConstruction();
		~RendererConstruction();

		Internal& GetInternal() { return *_internal; }
		const Internal& GetInternal() const { return *_internal; }
	protected:
		std::unique_ptr<Internal> _internal;
	};

	class RendererConstruction::Internal
	{
	public:
		using ElementId = unsigned;
		using ModelScaffoldMarker = ::Assets::PtrToMarkerPtr<ModelScaffold>;
		using ModelScaffoldPtr = std::shared_ptr<ModelScaffold>;
		using MaterialScaffoldMarker = ::Assets::PtrToMarkerPtr<MaterialScaffold>;
		using MaterialScaffoldPtr = std::shared_ptr<MaterialScaffold>;

		std::vector<std::pair<ElementId, ModelScaffoldMarker>> _modelScaffoldMarkers;
		std::vector<std::pair<ElementId, ModelScaffoldPtr>> _modelScaffoldPtrs;
		std::vector<std::pair<ElementId, MaterialScaffoldMarker>> _materialScaffoldMarkers;
		std::vector<std::pair<ElementId, MaterialScaffoldPtr>> _materialScaffoldPtrs;
		std::vector<std::pair<ElementId, std::string>> _names;
		unsigned _elementCount = 0;

		::Assets::PtrToMarkerPtr<SkeletonScaffold> _skeletonScaffoldMarker;
		std::shared_ptr<SkeletonScaffold> _skeletonScaffoldPtr;
		uint64_t _skeletonScaffoldHashValue = 0u;

		bool _sealed = false;

		std::vector<uint64_t> _elementHashValues;
		mutable uint64_t _hash = 0ull;
		bool _disableHash = false;
	};

	class RendererConstruction::ElementIterator
	{
	public:
		class Value
		{
		public:
			std::shared_ptr<ModelScaffold> GetModelScaffold() const;
			std::shared_ptr<MaterialScaffold> GetMaterialScaffold() const;
			std::string GetModelScaffoldName() const;
			std::string GetMaterialScaffoldName() const;
			unsigned ElementId() const;
		private:
			Value();
			template<typename Type>
				using Iterator = typename std::vector<std::pair<unsigned, Type>>::iterator;
			Iterator<RendererConstruction::Internal::ModelScaffoldMarker> _msmi;
			Iterator<RendererConstruction::Internal::ModelScaffoldPtr> _mspi;
			Iterator<RendererConstruction::Internal::MaterialScaffoldMarker> _matsmi;
			Iterator<RendererConstruction::Internal::MaterialScaffoldPtr> _matspi;
			unsigned _elementId = 0;
			Internal* _internal = nullptr;
			friend class ElementIterator;
			friend class RendererConstruction;
		};

		ElementIterator& operator++();
		const Value& operator*() const;
		const Value* operator->() const;
		friend bool operator==(const ElementIterator&, const ElementIterator&);
		friend bool operator!=(const ElementIterator&, const ElementIterator&);

	private:
		ElementIterator();
		Value _value;
		friend class RendererConstruction;
		bool IsEqual(const ElementIterator& other) const;
	};

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

	inline unsigned RendererConstruction::ElementIterator::Value::ElementId() const { return _elementId; }

	inline auto RendererConstruction::ElementIterator::operator*() const -> const Value& { return _value; }
	inline auto RendererConstruction::ElementIterator::operator->() const -> const Value* { return &_value; }

	inline bool RendererConstruction::ElementIterator::IsEqual(const ElementIterator& other) const
	{
		assert(_value._internal == other._value._internal);
		return _value._elementId == other._value._elementId;
	}

	inline bool operator==(const RendererConstruction::ElementIterator& lhs, const RendererConstruction::ElementIterator& rhs)
	{
		return lhs.IsEqual(rhs);
	}

	inline bool operator!=(const RendererConstruction::ElementIterator& lhs, const RendererConstruction::ElementIterator& rhs)
	{
		return !lhs.IsEqual(rhs);
	}

}}

