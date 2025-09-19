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

namespace AssetsNew { class CompoundAssetScaffold; }

namespace RenderCore { namespace Assets
{
	class RawMaterial;
	using PtrToMarkerToMaterial = std::shared_ptr<::Assets::Marker<::Assets::AssetWrapper<RawMaterial>>>;
	using PtrToMarkerToMaterialSet = std::shared_ptr<::Assets::Marker<::Assets::ContextImbuedAsset<std::shared_ptr<::AssetsNew::CompoundAssetScaffold>>>>;

	::Assets::CompilerRegistration RegisterMaterialCompiler(
		::Assets::IIntermediateCompilers& intermediateCompilers);

	class MaterialSetConstruction
	{
	public:
		void SetBaseMaterials(PtrToMarkerToMaterialSet&&);
		void SetBaseMaterials(IteratorRange<const std::string*>);
		void SetBaseMaterials(std::string modelFileIdentifier);

		void AddOverride(StringSection<> application, RawMaterial&& mat);
		void AddOverride(StringSection<> application, PtrToMarkerToMaterial&&);
		void AddOverride(StringSection<> application, std::string materialFileIdentifier);
		void AddOverride(RawMaterial&& mat);
		void AddOverride(PtrToMarkerToMaterial&&);
		void AddOverride(PtrToMarkerToMaterialSet&&);
		void AddOverride(std::string materialFileIdentifier);

		struct Override
		{
			uint64_t _application = 0;		// 0 means it applies to all
			unsigned _overrideIdx = 0;
		};
		std::vector<std::pair<Override, RawMaterial>> _inlineMaterialOverrides;
		std::vector<std::pair<Override, std::string>> _materialFileOverrides;
		std::vector<std::pair<Override, PtrToMarkerToMaterial>> _futureMaterialOverrides;
		std::vector<std::pair<Override, PtrToMarkerToMaterialSet>> _futureMaterialSetOverrides;
		unsigned _nextOverrideIdx = 0;

		std::variant<std::monostate, PtrToMarkerToMaterialSet, std::vector<std::string>, std::string> _baseMaterials = std::monostate{};

		bool CanBeHashed() const;
		uint64_t GetHash() const;

		MaterialSetConstruction();
		~MaterialSetConstruction();

	private:
		bool _disableHash = false;
		mutable uint64_t _hash = 0;
	};

	class CompiledMaterialSet;
	void ConstructMaterialSet(
		std::promise<std::shared_ptr<CompiledMaterialSet>>&& promise,
		std::shared_ptr<MaterialSetConstruction> construction);

}}

