// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../../Assets/DepVal.h"
#include "../../Assets/AssetUtils.h"
#include <vector>
#include <string>

// for asset mixins
#include "../../Assets/Continuation.h"
#include "../../Assets/ContinuationUtil.h"
#include "../../Assets/ConfigFileContainer.h"

namespace Formatters { template<typename CharType> class TextInputFormatter; }

namespace RenderCore { namespace Assets { namespace GeoProc
{

	class ModelScaffoldConfiguration
	{
	public:
		using StringWildcardMatcher = std::string;

		struct RawGeoRules
		{
			std::optional<bool> _16BitNativeTypes;
			std::vector<uint64_t> _includeAttributes, _excludeAttributes;
		};
		std::vector<std::pair<StringWildcardMatcher, RawGeoRules>> _rawGeoRules;

		struct CommandStream
		{};
		std::vector<std::pair<uint64_t, CommandStream>> _commandStreams;

		struct SkeletonRules
		{
			std::vector<uint64_t> _animatableBones;
			std::vector<uint64_t> _outputBones;
		};
		std::vector<std::pair<StringWildcardMatcher, SkeletonRules>> _skeletonRules;

		std::optional<bool> _autoProcessTextures;

		std::vector<std::string> _inheritConfigurations;

		IteratorRange<const std::string*> GetInherited() const { return _inheritConfigurations; }
		void MergeInWithFilenameResolve(const ModelScaffoldConfiguration&, const ::Assets::DirectorySearchRules&);

		ModelScaffoldConfiguration(Formatters::TextInputFormatter<char>& fmttr);
		ModelScaffoldConfiguration();
		~ModelScaffoldConfiguration();

	private:
		void DeserializeRawGeoRules(Formatters::TextInputFormatter<char>& fmttr);
		void DeserializeCommandStreams(Formatters::TextInputFormatter<char>& fmttr);
		void DeserializeSkeletonRules(Formatters::TextInputFormatter<char>& fmttr);
	};

	template<typename AssetType>
		class AssetMixin : public AssetType
	{
	public:
		AssetMixin(Formatters::TextInputFormatter<char>& fmttr, const ::Assets::DirectorySearchRules& searchRules, const ::Assets::DependencyValidation& depVal);

		const ::Assets::DependencyValidation& GetDependencyValidation() const { return _depVal; }
		const ::Assets::DirectorySearchRules& GetDirectorySearchRules() const { return _searchRules; }
	private:
		::Assets::DirectorySearchRules _searchRules;
		::Assets::DependencyValidation _depVal;
	};

	template<typename AssetType>
		AssetMixin<AssetType>::AssetMixin(Formatters::TextInputFormatter<char>& fmttr, const ::Assets::DirectorySearchRules& searchRules, const ::Assets::DependencyValidation& depVal)
	: AssetType(fmttr)
	, _searchRules(searchRules)
	, _depVal(depVal)
	{}

	template <typename AssetType>
		class ResolvedAssetMixin : public AssetType
	{
	public:
		const ::Assets::DependencyValidation& GetDependencyValidation() const { return _depVal; }

		ResolvedAssetMixin() = default;

		static void ConstructToPromise(
			std::promise<std::shared_ptr<ResolvedAssetMixin<AssetType>>>&&,
			StringSection<> initializer);
	private:
		::Assets::DependencyValidation _depVal;
	};

	template <typename AssetType>
		void ResolvedAssetMixin<AssetType>::ConstructToPromise(
			std::promise<std::shared_ptr<ResolvedAssetMixin<AssetType>>>&& promise,
			StringSection<> initializer)
	{
		// We have to load an entire tree of AssetTypes and their inherited items.
		// We'll do this all with one future in such a way that we create a linear
		// list of all of the AssetTypes in the order that they need to be merged in
		// We do this in a kind of breadth first way, were we queue up all of the futures
		// for a given level together
		using SrcAssetType = AssetMixin<AssetType>;

		class PendingAssetTree
		{
		public:
			unsigned _nextId = 1;
			struct SubFutureIndexer
			{
				unsigned _parentId;
				unsigned _siblingIdx;
			};
			struct LoadedSubMaterialsIndexer
			{
				unsigned _itemId;
				unsigned _parentId;
				unsigned _siblingIdx;
			};
			std::vector<std::pair<SubFutureIndexer, ::Assets::PtrToMarkerPtr<SrcAssetType>>> _subFutures;
			std::vector<std::pair<LoadedSubMaterialsIndexer, std::shared_ptr<SrcAssetType>>> _loadedSubAssets;
			std::vector<::Assets::DependencyValidation> _depVals;
		};
		auto pendingTree = std::make_shared<PendingAssetTree>();

		auto i = initializer.begin();
		unsigned siblingIdx = 0;
		while (i != initializer.end()) {
			while (i != initializer.end() && *i == ';') ++i;
			auto i2 = i;
			while (i2 != initializer.end() && *i2 != ';') ++i2;
			if (i2==i) break;

			pendingTree->_subFutures.emplace_back(
				typename PendingAssetTree::SubFutureIndexer{0, siblingIdx++},
				::Assets::MakeAssetMarkerPtr<SrcAssetType>(MakeStringSection(i, i2)));
			i = i2;
		}
		assert(!pendingTree->_subFutures.empty());

		::Assets::PollToPromise(
			std::move(promise),
			[pendingTree]() {
				for (;;) {
					std::vector<std::pair<typename PendingAssetTree::SubFutureIndexer, std::shared_ptr<SrcAssetType>>> subMaterials;
					std::vector<::Assets::DependencyValidation> subDepVals;
					for (const auto& f:pendingTree->_subFutures) {
						::Assets::Blob queriedLog;
						::Assets::DependencyValidation queriedDepVal;
						std::shared_ptr<SrcAssetType> subMat;
						auto state = f.second->CheckStatusBkgrnd(subMat, queriedDepVal, queriedLog);
						if (state == ::Assets::AssetState::Pending)
							return ::Assets::PollStatus::Continue;

						// "invalid" is actually ok here. we include the dep val as normal, but ignore
						// the asset

						subDepVals.push_back(queriedDepVal);
						if (state == ::Assets::AssetState::Ready)
							subMaterials.emplace_back(f.first, std::move(subMat));
					}
					pendingTree->_subFutures.clear();
					pendingTree->_depVals.insert(pendingTree->_depVals.end(), subDepVals.begin(), subDepVals.end());

					// merge these RawMats into _loadedSubAssets in the right places
					// also queue the next level of loads as we go
					// We want each subMaterial to go into _loadedSubAssets in the same order as 
					// in subMaterials, but immediately before their parent
					for (const auto&m:subMaterials) {
						unsigned newParentId = pendingTree->_nextId++;
						if (m.first._parentId == 0) {
							// ie, this is a root
							pendingTree->_loadedSubAssets.emplace_back(typename PendingAssetTree::LoadedSubMaterialsIndexer{newParentId, m.first._parentId, m.first._siblingIdx}, m.second);
						} else {
							// Insert just before the parent, after any siblings added this turn
							// This will give us the right ordering because we ensure that we complete all items in pendingTree->_subFutures (and therefor all siblings)
							// before we process any here
							auto parentI = std::find_if(
								pendingTree->_loadedSubAssets.begin(), pendingTree->_loadedSubAssets.end(),
								[s=m.first._parentId](const auto& c) { return c.first._itemId == s;});
							assert(parentI!=pendingTree->_loadedSubAssets.end());
							pendingTree->_loadedSubAssets.insert(parentI, {typename PendingAssetTree::LoadedSubMaterialsIndexer{newParentId, m.first._parentId, m.first._siblingIdx}, m.second});
						}

						unsigned siblingIdx = 0;
						auto& searchRules = m.second->GetDirectorySearchRules();
						for (auto name:m.second->GetInherited()) {
							auto colon = FindLastOf(name.begin(), name.end(), ':');
							if (colon != name.end()) {
								::Assets::ResChar resolvedFile[MaxPath];
								searchRules.ResolveFile(resolvedFile, MakeStringSection(name.begin(), colon));
								std::string fullResolvedName = std::string(resolvedFile) + std::string(colon, name.end());
								pendingTree->_subFutures.emplace_back(typename PendingAssetTree::SubFutureIndexer{newParentId, siblingIdx++}, ::Assets::MakeAssetMarker<std::shared_ptr<SrcAssetType>>(fullResolvedName));
							} else {
								Throw(std::runtime_error("TODO -- not handling inherit references within the same file"));
							}
						}
					}

					// if we still have sub-futures, need to roll around again
					// we'll do this immediately, just incase everything is already loaded
					if (pendingTree->_subFutures.empty()) break;
				}
				// survived the gauntlet -- everything is ready to dispatch now
				return ::Assets::PollStatus::Finish;
			},
			[pendingTree]() {
				// All of the assets in the tree are loaded; and we can just merge them together
				// into a final resolved material
				#if defined(_DEBUG)
					if (!pendingTree->_loadedSubAssets.empty())
						for (auto i=pendingTree->_loadedSubAssets.begin(); (i+1)!=pendingTree->_loadedSubAssets.end(); ++i)
							assert(i->first._parentId != (i+1)->first._parentId || i->first._siblingIdx < (i+1)->first._siblingIdx);        // double check ordering is as expected
				#endif

				auto finalAsset = std::make_shared<ResolvedAssetMixin<AssetType>>();
				// have to call "MergeInWithFilenameResolve" for all (even the first), because it may resolve internal filenames, etc
				for (const auto& i:pendingTree->_loadedSubAssets)
					finalAsset->MergeInWithFilenameResolve(*i.second, i.second->GetDirectorySearchRules());

				VLA(::Assets::DependencyValidationMarker, depVals, pendingTree->_depVals.size());
				for (unsigned c=0; c<pendingTree->_depVals.size(); c++) depVals[c] = pendingTree->_depVals[c];
				finalAsset->_depVal = ::Assets::GetDepValSys().MakeOrReuse(MakeIteratorRange(depVals, &depVals[pendingTree->_depVals.size()]));
				return finalAsset;
			});
	}
	
}}}