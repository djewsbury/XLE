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

namespace RenderCore { namespace Assets
{
	class RawMaterial;
	template<typename ObjectType> class CompilableMaterialAssetMixin;

	::Assets::CompilerRegistration RegisterMaterialCompiler(
		::Assets::IIntermediateCompilers& intermediateCompilers);

	class MaterialScaffoldConstruction
	{
	public:
		void AddOverride(StringSection<> application, RawMaterial&& mat);
		void AddOverride(StringSection<> application, ::Assets::PtrToMarkerPtr<CompilableMaterialAssetMixin<RawMaterial>>&&);
		void AddOverride(StringSection<> application, std::string&& materialFileIdentifier);
		void AddOverride(RawMaterial&& mat);
		void AddOverride(::Assets::PtrToMarkerPtr<CompilableMaterialAssetMixin<RawMaterial>>&&);
		void AddOverride(std::string&& materialFileIdentifier);

		struct Override
		{
			uint64_t _application = 0;		// 0 means it applies to all
			unsigned _overrideIdx = 0;
		};
		std::vector<std::pair<Override, RawMaterial>> _inlineMaterialOverrides;
		std::vector<std::pair<Override, std::string>> _materialFileOverrides;
		std::vector<std::pair<Override, ::Assets::PtrToMarkerPtr<CompilableMaterialAssetMixin<RawMaterial>>>> _futureMaterialOverrides;
		unsigned _nextOverrideIdx = 0;

		bool CanBeHashed() const;
		uint64_t GetHash() const;

		MaterialScaffoldConstruction();
		~MaterialScaffoldConstruction();

	private:
		bool _disableHash = false;
		mutable uint64_t _hash = 0;
	};

	class ModelCompilationConfiguration;
	class MaterialScaffold;

	void ConstructMaterialScaffold(
		std::promise<std::shared_ptr<MaterialScaffold>>&& promise,
		std::shared_ptr<MaterialScaffoldConstruction> construction,
		std::string sourceModel, std::shared_ptr<ModelCompilationConfiguration> sourceModelConfiguration);

	void ConstructMaterialScaffold(
		std::promise<std::shared_ptr<MaterialScaffold>>&& promise,
		std::shared_ptr<MaterialScaffoldConstruction> construction,
		std::string sourceModel, std::shared_future<std::shared_ptr<::Assets::ResolvedAssetMixin<ModelCompilationConfiguration>>> sourceModelConfiguration);

	void ConstructMaterialScaffold(
		std::promise<std::shared_ptr<MaterialScaffold>>&& promise,
		std::shared_ptr<MaterialScaffoldConstruction> construction,
		IteratorRange<const std::string*> materialsToInstantiate);

}}

