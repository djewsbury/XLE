// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../../Assets/IntermediateCompilers.h"
#include "../../Assets/ICompileOperation.h"
#include <memory>
#include <vector>
#include <string>
#include <future>
#include <variant>

namespace AssetsNew { class CompoundAssetScaffold; }
namespace Assets { struct SerializedArtifact; }

namespace RenderCore { namespace Assets
{
	class RawMaterial; class PredefinedDescriptorSetLayout;
	using FutureMaterial = std::shared_future<::Assets::AssetWrapper<RawMaterial>>;
	using FutureMaterialSet = std::shared_future<::Assets::ContextImbuedAsset<std::shared_ptr<::AssetsNew::CompoundAssetScaffold>>>;
	using FuturePredefinedDescriptorSet = std::shared_future<std::shared_ptr<PredefinedDescriptorSetLayout>>;

	::Assets::CompilerRegistration RegisterMaterialCompiler(
		::Assets::IIntermediateCompilers& intermediateCompilers);

	class MaterialSetConstruction
	{
	public:
		void SetBaseMaterials(FutureMaterialSet&&);
		void SetBaseMaterials(IteratorRange<const std::string*>);
		void SetBaseMaterials(std::string modelFileIdentifier);

		void AddOverride(StringSection<> application, RawMaterial&& mat);
		void AddOverride(StringSection<> application, FutureMaterial&&);
		void AddOverride(StringSection<> application, FuturePredefinedDescriptorSet&&);
		void AddOverride(StringSection<> application, std::string materialFileIdentifier);
		void AddOverride(RawMaterial&& mat);
		void AddOverride(FutureMaterial&&);
		void AddOverride(FuturePredefinedDescriptorSet&&);
		void AddOverride(FutureMaterialSet&&, std::string prefix = {});
		void AddOverride(std::string materialFileIdentifier);

		static const uint64_t s_applyToAll = ~0ull;
		struct Override
		{
			uint64_t _application = s_applyToAll;
			unsigned _overrideIdx = 0;
		};
		std::vector<std::pair<Override, RawMaterial>> _inlineMaterialOverrides;
		std::vector<std::pair<Override, std::string>> _materialFileOverrides;
		std::vector<std::pair<Override, FutureMaterial>> _futureMaterialOverrides;
		std::vector<std::pair<Override, std::pair<FutureMaterialSet, std::string>>> _futureMaterialSetOverrides;
		std::vector<std::pair<Override, FuturePredefinedDescriptorSet>> _futurePredefinedDescriptorSetOverrides;
		unsigned _nextOverrideIdx = 0;

		std::variant<std::monostate, FutureMaterialSet, std::vector<std::string>, std::string> _baseMaterials = std::monostate{};

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

	::Assets::PortableVector<::Assets::SerializedArtifact> SerializeCompiledMaterialSetToChunks(
		std::shared_ptr<MaterialSetConstruction> construction,
		std::vector<std::string> materialsToInstantiate);

}}

