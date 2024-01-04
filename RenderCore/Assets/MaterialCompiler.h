// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../../Assets/IntermediateCompilers.h"
#include "../../Assets/AssetMixins.h"
#include <memory>
#include <vector>
#include <string>
#include <future>
#include <variant>

namespace RenderCore { namespace Assets
{
	class RawMaterial;
	template<typename ObjectType> class CompilableMaterialAssetMixin;
	class RawMaterialSet_Internal;
	using RawMaterialSet = ::Assets::FormatterAssetMixin<RawMaterialSet_Internal>;

	::Assets::CompilerRegistration RegisterMaterialCompiler(
		::Assets::IIntermediateCompilers& intermediateCompilers);

	class MaterialScaffoldConstruction
	{
	public:
		void SetBaseMaterials(::Assets::PtrToMarkerPtr<RawMaterialSet>&&);
		void SetBaseMaterials(IteratorRange<const std::string*>);
		void SetBaseMaterials(std::string modelFileIdentifier);

		void AddOverride(StringSection<> application, RawMaterial&& mat);
		void AddOverride(StringSection<> application, ::Assets::PtrToMarkerPtr<CompilableMaterialAssetMixin<RawMaterial>>&&);
		void AddOverride(StringSection<> application, std::string materialFileIdentifier);
		void AddOverride(RawMaterial&& mat);
		void AddOverride(::Assets::PtrToMarkerPtr<CompilableMaterialAssetMixin<RawMaterial>>&&);
		void AddOverride(::Assets::PtrToMarkerPtr<RawMaterialSet>&&);
		void AddOverride(std::string materialFileIdentifier);

		struct Override
		{
			uint64_t _application = 0;		// 0 means it applies to all
			unsigned _overrideIdx = 0;
		};
		std::vector<std::pair<Override, RawMaterial>> _inlineMaterialOverrides;
		std::vector<std::pair<Override, std::string>> _materialFileOverrides;
		std::vector<std::pair<Override, ::Assets::PtrToMarkerPtr<CompilableMaterialAssetMixin<RawMaterial>>>> _futureMaterialOverrides;
		std::vector<std::pair<Override, ::Assets::PtrToMarkerPtr<RawMaterialSet>>> _futureMaterialSetOverrides;
		unsigned _nextOverrideIdx = 0;

		std::variant<std::monostate, ::Assets::PtrToMarkerPtr<RawMaterialSet>, std::vector<std::string>, std::string> _baseMaterials = std::monostate{};

		bool CanBeHashed() const;
		uint64_t GetHash() const;

		MaterialScaffoldConstruction();
		~MaterialScaffoldConstruction();

	private:
		bool _disableHash = false;
		mutable uint64_t _hash = 0;
	};

	class MaterialScaffold;
	void ConstructMaterialScaffold(
		std::promise<std::shared_ptr<MaterialScaffold>>&& promise,
		std::shared_ptr<MaterialScaffoldConstruction> construction);

#if 0
	void ConstructMaterialScaffold(
		std::promise<std::shared_ptr<MaterialScaffold>>&& promise,
		std::shared_ptr<MaterialScaffoldConstruction> construction,
		std::shared_ptr<RawMaterialSet> baseMaterials);

	void ConstructMaterialScaffold(
		std::promise<std::shared_ptr<MaterialScaffold>>&& promise,
		std::shared_ptr<MaterialScaffoldConstruction> construction,
		std::shared_future<std::shared_ptr<RawMaterialSet>> baseMaterials);

	void ConstructMaterialScaffold(
		std::promise<std::shared_ptr<MaterialScaffold>>&& promise,
		std::shared_ptr<MaterialScaffoldConstruction> construction,
		IteratorRange<const std::string*> materialsToInstantiate);
#endif

}}

