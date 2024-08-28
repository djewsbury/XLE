// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#if defined(__CLR_VER)
    #error This file cannot be included in C++/CLR projects
#endif

#include "Continuation.h"
#include "ContinuationUtil.h"
#include "AssetUtils.h"
#include "DepVal.h"
#include "../Formatters/TextFormatter.h"

namespace Assets
{

	template <typename ObjectType>
		class ContextImbuedAsset : public std::tuple<ObjectType, DirectorySearchRules, DependencyValidation, InheritList>
	{
	public:
		using std::tuple<ObjectType, DirectorySearchRules, DependencyValidation, InheritList>::tuple;

		operator ObjectType&() { return std::get<ObjectType>(*this); }
		operator const ObjectType&() const { return std::get<ObjectType>(*this); }
	};

	template <typename ObjectType>
		class ResolvedAssetMixin : public std::tuple<ObjectType, DependencyValidation>
	{
	public:
		using std::tuple<ObjectType, DependencyValidation>::tuple;

		operator ObjectType&() { return std::get<ObjectType>(*this); }
		operator const ObjectType&() const { return std::get<ObjectType>(*this); }
	};

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	template <typename DstType, typename UnresolvedType = ContextImbuedAsset<DstType>>
		void ResolveAssetToPromise(
			std::promise<ResolvedAssetMixin<DstType>>&& promise,
			StringSection<> initializer)
	{
		using PtrToBaseAssetMarker = std::shared_ptr<Marker<UnresolvedType>>;
		std::vector<PtrToBaseAssetMarker> initialFutures;
		auto i = initializer.begin();
		while (i != initializer.end()) {
			while (i != initializer.end() && *i == ';') ++i;
			auto i2 = i;
			while (i2 != initializer.end() && *i2 != ';') ++i2;
			if (i2==i) break;

			initialFutures.emplace_back(GetAssetMarker<UnresolvedType>(MakeStringSection(i, i2)));
			i = i2;
		}
		assert(!initialFutures.empty());
		ResolveAssetToPromise2<DstType, UnresolvedType>(std::move(promise), initialFutures);
	}

	template <typename DstType, typename UnresolvedType = ContextImbuedAsset<DstType>>
		void ResolveAssetToPromise2(
			std::promise<ResolvedAssetMixin<DstType>>&& promise,
			IteratorRange<const std::shared_ptr<Marker<UnresolvedType>>*> initialBaseAssets)
	{
		using PtrToBaseAssetMarker = std::shared_ptr<Marker<UnresolvedType>>;

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
			std::vector<std::pair<SubFutureIndexer, PtrToBaseAssetMarker>> _subFutures;
			std::vector<std::pair<LoadedSubMaterialsIndexer, UnresolvedType>> _loadedSubAssets;
			std::vector<DependencyValidation> _depVals;
		};
		auto pendingTree = std::make_shared<PendingAssetTree>();

		pendingTree->_subFutures.reserve(initialBaseAssets.size());
		unsigned siblingIdx = 0;
		for (auto initialBaseAsset:initialBaseAssets)
			pendingTree->_subFutures.emplace_back(typename PendingAssetTree::SubFutureIndexer{0, siblingIdx++}, std::move(initialBaseAsset));

		PollToPromise(
			std::move(promise),
			[pendingTree]() {
				for (;;) {
					std::vector<std::pair<typename PendingAssetTree::SubFutureIndexer, UnresolvedType>> subMaterials;
					std::vector<DependencyValidation> subDepVals;
					for (const auto& f:pendingTree->_subFutures) {
						Blob queriedLog;
						DependencyValidation queriedDepVal;
						UnresolvedType subMat;
						auto state = f.second->CheckStatusBkgrnd(subMat, queriedDepVal, queriedLog);
						if (state == AssetState::Pending)
							return PollStatus::Continue;

						// "invalid" is actually ok here. we include the dep val as normal, but ignore
						// the asset

						subDepVals.push_back(queriedDepVal);
						if (state == AssetState::Ready)
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
						auto& searchRules = std::get<DirectorySearchRules>(m.second);
						for (auto name:std::get<InheritList>(m.second)) {
							auto colon = FindLastOf(name.begin(), name.end(), ':');
							if (colon != name.end()) {
								ResChar resolvedFile[MaxPath];
								searchRules.ResolveFile(resolvedFile, MakeStringSection(name.begin(), colon));
								std::string fullResolvedName = std::string(resolvedFile) + std::string(colon, name.end());
								pendingTree->_subFutures.emplace_back(typename PendingAssetTree::SubFutureIndexer{newParentId, siblingIdx++}, GetAssetMarker<UnresolvedType>(fullResolvedName));
							} else {
								if (searchRules.GetBaseFile().IsEmpty())
									Throw(std::runtime_error("Cannot resolve reference within file because the base filename hasn't been recorded"));
								std::string fullResolvedName = Concatenate(searchRules.GetBaseFile(), ":", name);
								pendingTree->_subFutures.emplace_back(typename PendingAssetTree::SubFutureIndexer{newParentId, siblingIdx++}, GetAssetMarker<UnresolvedType>(fullResolvedName));
							}
						}
					}

					// if we still have sub-futures, need to roll around again
					// we'll do this immediately, just incase everything is already loaded
					if (pendingTree->_subFutures.empty()) break;
				}
				// survived the gauntlet -- everything is ready to dispatch now
				return PollStatus::Finish;
			},
			[pendingTree]() {
				// All of the assets in the tree are loaded; and we can just merge them together
				// into a final resolved material
				#if defined(_DEBUG)
					if (!pendingTree->_loadedSubAssets.empty())
						for (auto i=pendingTree->_loadedSubAssets.begin(); (i+1)!=pendingTree->_loadedSubAssets.end(); ++i)
							assert(i->first._parentId != (i+1)->first._parentId || i->first._siblingIdx < (i+1)->first._siblingIdx);        // double check ordering is as expected
				#endif

				// call Internal::InvokeAssetConstructor<DstType>() to ensure that ptr types are initialized
				DstType finalAsset { Internal::InvokeAssetConstructor<DstType>() };

				// have to call "MergeInWithFilenameResolve" for all (even the first), because it may resolve internal filenames, etc
				for (const auto& i:pendingTree->_loadedSubAssets)
					Internal::MaybeDeref(finalAsset).MergeInWithFilenameResolve(Internal::MaybeDeref(std::get<0>(i.second)), std::get<DirectorySearchRules>(i.second));

				VLA(DependencyValidationMarker, depVals, pendingTree->_depVals.size());
				for (unsigned c=0; c<pendingTree->_depVals.size(); c++) depVals[c] = pendingTree->_depVals[c];
				auto finalDepVal = GetDepValSys().MakeOrReuse(MakeIteratorRange(depVals, &depVals[pendingTree->_depVals.size()]));
				return ResolvedAssetMixin<DstType> { std::move(finalAsset), std::move(finalDepVal) };
			});
	}
}
