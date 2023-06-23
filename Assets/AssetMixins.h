// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "Continuation.h"
#include "ContinuationUtil.h"
#include "AssetUtils.h"
#include "DepVal.h"

namespace Assets
{

	template<typename ObjectType>
		class FormatterAssetMixin : public ObjectType
	{
	public:
		FormatterAssetMixin(Formatters::TextInputFormatter<char>& fmttr, const ::Assets::DirectorySearchRules& searchRules, const ::Assets::DependencyValidation& depVal);
		FormatterAssetMixin() = default;

		const ::Assets::DependencyValidation& GetDependencyValidation() const { return _depVal; }
		const ::Assets::DirectorySearchRules& GetDirectorySearchRules() const { return _searchRules; }
	private:
		::Assets::DirectorySearchRules _searchRules;
		::Assets::DependencyValidation _depVal;
	};

	template <typename ObjectType, typename BaseAssetType=FormatterAssetMixin<ObjectType>>
		class ResolvedAssetMixin : public ObjectType
	{
	public:
		const ::Assets::DependencyValidation& GetDependencyValidation() const { return _depVal; }

		ResolvedAssetMixin() = default;

		static void ConstructToPromise(
			std::promise<std::shared_ptr<ResolvedAssetMixin<ObjectType, BaseAssetType>>>&&,
			StringSection<> initializer);

		static void ConstructToPromise(
			std::promise<ResolvedAssetMixin<ObjectType, BaseAssetType>>&&,
			StringSection<> initializer);
	private:
		::Assets::DependencyValidation _depVal;
	};

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	template<typename ObjectType>
		FormatterAssetMixin<ObjectType>::FormatterAssetMixin(Formatters::TextInputFormatter<char>& fmttr, const ::Assets::DirectorySearchRules& searchRules, const ::Assets::DependencyValidation& depVal)
	: ObjectType(fmttr)
	, _searchRules(searchRules)
	, _depVal(depVal)
	{}

	template <typename ObjectType, typename BaseAssetType>
		void ResolvedAssetMixin<ObjectType, BaseAssetType>::ConstructToPromise(
			std::promise<std::shared_ptr<ResolvedAssetMixin<ObjectType, BaseAssetType>>>&& promise,
			StringSection<> initializer)
	{
		// We have to load an entire tree of AssetTypes and their inherited items.
		// We'll do this all with one future in such a way that we create a linear
		// list of all of the AssetTypes in the order that they need to be merged in
		// We do this in a kind of breadth first way, were we queue up all of the futures
		// for a given level together
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
			std::vector<std::pair<SubFutureIndexer, ::Assets::PtrToMarkerPtr<BaseAssetType>>> _subFutures;
			std::vector<std::pair<LoadedSubMaterialsIndexer, std::shared_ptr<BaseAssetType>>> _loadedSubAssets;
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
				::Assets::MakeAssetMarkerPtr<BaseAssetType>(MakeStringSection(i, i2)));
			i = i2;
		}
		assert(!pendingTree->_subFutures.empty());

		::Assets::PollToPromise(
			std::move(promise),
			[pendingTree]() {
				for (;;) {
					std::vector<std::pair<typename PendingAssetTree::SubFutureIndexer, std::shared_ptr<BaseAssetType>>> subMaterials;
					std::vector<::Assets::DependencyValidation> subDepVals;
					for (const auto& f:pendingTree->_subFutures) {
						::Assets::Blob queriedLog;
						::Assets::DependencyValidation queriedDepVal;
						std::shared_ptr<BaseAssetType> subMat;
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
								pendingTree->_subFutures.emplace_back(typename PendingAssetTree::SubFutureIndexer{newParentId, siblingIdx++}, ::Assets::MakeAssetMarker<std::shared_ptr<BaseAssetType>>(fullResolvedName));
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

				auto finalAsset = std::make_shared<ResolvedAssetMixin<ObjectType, BaseAssetType>>();
				// have to call "MergeInWithFilenameResolve" for all (even the first), because it may resolve internal filenames, etc
				for (const auto& i:pendingTree->_loadedSubAssets)
					finalAsset->MergeInWithFilenameResolve(*i.second, i.second->GetDirectorySearchRules());

				VLA(::Assets::DependencyValidationMarker, depVals, pendingTree->_depVals.size());
				for (unsigned c=0; c<pendingTree->_depVals.size(); c++) depVals[c] = pendingTree->_depVals[c];
				finalAsset->_depVal = ::Assets::GetDepValSys().MakeOrReuse(MakeIteratorRange(depVals, &depVals[pendingTree->_depVals.size()]));
				return finalAsset;
			});
	}
	
	template <typename ObjectType, typename BaseAssetType>
		void ResolvedAssetMixin<ObjectType, BaseAssetType>::ConstructToPromise(
			std::promise<ResolvedAssetMixin<ObjectType, BaseAssetType>>&& promise,
			StringSection<> initializer)
	{
		// We have to load an entire tree of AssetTypes and their inherited items.
		// We'll do this all with one future in such a way that we create a linear
		// list of all of the AssetTypes in the order that they need to be merged in
		// We do this in a kind of breadth first way, were we queue up all of the futures
		// for a given level together
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
			std::vector<std::pair<SubFutureIndexer, ::Assets::PtrToMarkerPtr<BaseAssetType>>> _subFutures;
			std::vector<std::pair<LoadedSubMaterialsIndexer, std::shared_ptr<BaseAssetType>>> _loadedSubAssets;
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
				::Assets::MakeAssetMarkerPtr<BaseAssetType>(MakeStringSection(i, i2)));
			i = i2;
		}
		assert(!pendingTree->_subFutures.empty());

		::Assets::PollToPromise(
			std::move(promise),
			[pendingTree]() {
				for (;;) {
					std::vector<std::pair<typename PendingAssetTree::SubFutureIndexer, std::shared_ptr<BaseAssetType>>> subMaterials;
					std::vector<::Assets::DependencyValidation> subDepVals;
					for (const auto& f:pendingTree->_subFutures) {
						::Assets::Blob queriedLog;
						::Assets::DependencyValidation queriedDepVal;
						std::shared_ptr<BaseAssetType> subMat;
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
								pendingTree->_subFutures.emplace_back(typename PendingAssetTree::SubFutureIndexer{newParentId, siblingIdx++}, ::Assets::MakeAssetMarker<std::shared_ptr<BaseAssetType>>(fullResolvedName));
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

				ResolvedAssetMixin<ObjectType, BaseAssetType> finalAsset;
				// have to call "MergeInWithFilenameResolve" for all (even the first), because it may resolve internal filenames, etc
				for (const auto& i:pendingTree->_loadedSubAssets)
					finalAsset.MergeInWithFilenameResolve(*i.second, i.second->GetDirectorySearchRules());

				VLA(::Assets::DependencyValidationMarker, depVals, pendingTree->_depVals.size());
				for (unsigned c=0; c<pendingTree->_depVals.size(); c++) depVals[c] = pendingTree->_depVals[c];
				finalAsset._depVal = ::Assets::GetDepValSys().MakeOrReuse(MakeIteratorRange(depVals, &depVals[pendingTree->_depVals.size()]));
				return finalAsset;
			});
	}
}
