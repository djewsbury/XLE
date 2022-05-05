// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../../Assets/ChunkFileContainer.h"
#include "../../Assets/AssetsCore.h"
#include "../../Assets/DepVal.h"
#include "../../Utility/IteratorUtils.h"

namespace Assets {  class IFileInterface; }

namespace RenderCore { namespace Assets
{
	static constexpr unsigned s_scaffoldCmdBegin_TransformationMachine = 0x500;
	static constexpr unsigned s_scaffoldCmdBegin_ModelMachine = 0x1000;
	static constexpr unsigned s_scaffoldCmdBegin_SkeletonMachine = 0x1500;
	static constexpr unsigned s_scaffoldCmdBegin_MaterialMachine = 0x2000;

	class IScaffoldNavigation;
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

		ScaffoldCmdIterator operator++();
		const Value& operator*() const;
		const Value* operator->() const;
		friend bool operator==(const ScaffoldCmdIterator&, const ScaffoldCmdIterator&);
		friend bool operator!=(const ScaffoldCmdIterator&, const ScaffoldCmdIterator&);

		IScaffoldNavigation* Navigation() const;

		ScaffoldCmdIterator(IteratorRange<const void*> data, IScaffoldNavigation& navigation);
		ScaffoldCmdIterator(IteratorRange<const void*> data);
		ScaffoldCmdIterator();

	private:
		Value _value;
		IScaffoldNavigation* _navigation = nullptr;
	};

	class ScaffoldAsset
	{
	public:
		IteratorRange<ScaffoldCmdIterator> GetCmdStream() const;

		const ::Assets::DependencyValidation& GetDependencyValidation() const { return _depVal; }
		std::shared_ptr<::Assets::IFileInterface> OpenLargeBlocks() const;

		ScaffoldAsset();
		ScaffoldAsset(IteratorRange<::Assets::ArtifactRequestResult*> chunks, const ::Assets::DependencyValidation& depVal);
		~ScaffoldAsset();
	private:
		::Assets::DependencyValidation _depVal;
	};

	class ShaderPatchCollection;

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

}}

