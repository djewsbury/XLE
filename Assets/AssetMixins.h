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
	namespace Internal
	{
		#define TEST_SUBST_MEMBER(Name, ...)																		\
			template<typename T> static constexpr auto Name##_(int) -> decltype(__VA_ARGS__, std::true_type{});		\
			template<typename...> static constexpr auto Name##_(...) -> std::false_type;							\
			static constexpr bool Name = decltype(Name##_<Type>(0))::value;											\
			/**/

		template<typename Type>
			struct FormatterAssetMixinTraits
		{
			TEST_SUBST_MEMBER(HasDeserializeKey, std::declval<T&>().TryDeserializeKey(
				std::declval< Formatters::TextInputFormatter<char>& >(),
				std::declval< StringSection<> >()
				));
		};

		#undef TEST_SUBST_MEMBER
	}

	template<typename ObjectType>
		class FormatterAssetMixin_NoDeserializeKey : public ObjectType
	{
	public:
		FormatterAssetMixin_NoDeserializeKey(Formatters::TextInputFormatter<char>& fmttr, const ::Assets::DirectorySearchRules& searchRules, const ::Assets::DependencyValidation& depVal);
		FormatterAssetMixin_NoDeserializeKey(ObjectType&&, ::Assets::DirectorySearchRules&& = {}, ::Assets::DependencyValidation&& = {});
		FormatterAssetMixin_NoDeserializeKey(const ObjectType&, const ::Assets::DirectorySearchRules& = {}, const ::Assets::DependencyValidation& = {});
		FormatterAssetMixin_NoDeserializeKey(Blob&&, ::Assets::DependencyValidation&& = {}, StringSection<> = {});
		FormatterAssetMixin_NoDeserializeKey() = default;

		const ::Assets::DependencyValidation& GetDependencyValidation() const { return _depVal; }
		const ::Assets::DirectorySearchRules& GetDirectorySearchRules() const { return _searchRules; }

	private:
		::Assets::DirectorySearchRules _searchRules;
		::Assets::DependencyValidation _depVal;

		Formatters::TextInputFormatter<char> AsFormatter(const Blob&);
	};

	template<typename ObjectType>
		class FormatterAssetMixin_DeserializeKey : public ObjectType
	{
	public:
		FormatterAssetMixin_DeserializeKey(Formatters::TextInputFormatter<char>& fmttr, const ::Assets::DirectorySearchRules& searchRules, const ::Assets::DependencyValidation& depVal);
		FormatterAssetMixin_DeserializeKey(ObjectType&&, ::Assets::DirectorySearchRules&& = {}, ::Assets::DependencyValidation&& = {});
		FormatterAssetMixin_DeserializeKey(const ObjectType&, const ::Assets::DirectorySearchRules& = {}, const ::Assets::DependencyValidation& = {});
		FormatterAssetMixin_DeserializeKey(Blob&&, ::Assets::DependencyValidation&& = {}, StringSection<> = {});
		FormatterAssetMixin_DeserializeKey() = default;

		const ::Assets::DependencyValidation& GetDependencyValidation() const { return _depVal; }
		const ::Assets::DirectorySearchRules& GetDirectorySearchRules() const { return _searchRules; }
		IteratorRange<const std::string*> GetInherited() const { return _inherit; }

	private:
		::Assets::DirectorySearchRules _searchRules;
		::Assets::DependencyValidation _depVal;
		std::vector<std::string> _inherit;

		Formatters::TextInputFormatter<char> AsFormatter(const Blob&);
	};

	template<typename ObjectType>
		using FormatterAssetMixin = std::conditional_t<Internal::FormatterAssetMixinTraits<ObjectType>::HasDeserializeKey, FormatterAssetMixin_DeserializeKey<ObjectType>, FormatterAssetMixin_NoDeserializeKey<ObjectType>>;

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

		static void ConstructToPromise(
			std::promise<std::shared_ptr<ResolvedAssetMixin<ObjectType, BaseAssetType>>>&&,
			IteratorRange<const ::Assets::PtrToMarkerPtr<BaseAssetType>*> initialBaseAssets);

		static void ConstructToPromise(
			std::promise<ResolvedAssetMixin<ObjectType, BaseAssetType>>&&,
			IteratorRange<const ::Assets::PtrToMarkerPtr<BaseAssetType>*> initialBaseAssets);
	private:
		::Assets::DependencyValidation _depVal;
	};

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	template<typename ObjectType>
		FormatterAssetMixin_NoDeserializeKey<ObjectType>::FormatterAssetMixin_NoDeserializeKey(Formatters::TextInputFormatter<char>& fmttr, const ::Assets::DirectorySearchRules& searchRules, const ::Assets::DependencyValidation& depVal)
	: ObjectType(fmttr)
	, _searchRules(searchRules)
	, _depVal(depVal)
	{}

	namespace Internal
	{
		inline Formatters::TextInputFormatter<char>& RemoveConst(const Formatters::TextInputFormatter<char>& f) { return const_cast<Formatters::TextInputFormatter<char>&>(f); }
		inline std::vector<std::string> DeserializeInheritList(Formatters::TextInputFormatter<utf8>& formatter)
		{
			std::vector<std::string> result; StringSection<> value;
			if (!formatter.TryBeginElement())
				Throw(Formatters::FormatException("Malformed inherit list", formatter.GetLocation()));
			while (formatter.TryStringValue(value)) result.push_back(value.AsString());
			if (!formatter.TryEndElement())
				Throw(Formatters::FormatException("Malformed inherit list", formatter.GetLocation()));
			return result;
		}
		void SkipValueOrElement(Formatters::TextInputFormatter<utf8>&);
	}

	template<typename ObjectType>
		FormatterAssetMixin_NoDeserializeKey<ObjectType>::FormatterAssetMixin_NoDeserializeKey(Blob&& blob, ::Assets::DependencyValidation&& depVal, StringSection<>)
	: ObjectType(Internal::RemoveConst(AsFormatter(blob)))
	, _depVal(std::move(depVal))
	{}

	template<typename ObjectType>
		FormatterAssetMixin_NoDeserializeKey<ObjectType>::FormatterAssetMixin_NoDeserializeKey(ObjectType&& moveFrom, ::Assets::DirectorySearchRules&& searchRules, ::Assets::DependencyValidation&& depVal)
	: ObjectType(std::move(moveFrom)), _searchRules(std::move(searchRules)), _depVal(std::move(depVal)) {}
	template<typename ObjectType>
		FormatterAssetMixin_NoDeserializeKey<ObjectType>::FormatterAssetMixin_NoDeserializeKey(const ObjectType& copyFrom, const ::Assets::DirectorySearchRules& searchRules, const ::Assets::DependencyValidation& depVal)
	: ObjectType(copyFrom), _searchRules(searchRules), _depVal(depVal) {}

	template<typename ObjectType>
		Formatters::TextInputFormatter<char> FormatterAssetMixin_NoDeserializeKey<ObjectType>::AsFormatter(const Blob& blob)
	{
		if (!blob)
			return Formatters::TextInputFormatter<char>{};
		return Formatters::TextInputFormatter<char>{ MakeIteratorRange(*blob).template Cast<const void*>() };
	}

	template<typename ObjectType>
		FormatterAssetMixin_DeserializeKey<ObjectType>::FormatterAssetMixin_DeserializeKey(Formatters::TextInputFormatter<char>& fmttr, const ::Assets::DirectorySearchRules& searchRules, const ::Assets::DependencyValidation& depVal)
	: _searchRules(searchRules)
	, _depVal(depVal)
	{
		StringSection<> keyname;
		while (fmttr.TryKeyedItem(keyname))
			if (XlEqString(keyname, "Inherit")) {
				_inherit = Internal::DeserializeInheritList(fmttr);
			} else if (!ObjectType::TryDeserializeKey(fmttr, keyname))
				Internal::SkipValueOrElement(fmttr);
	}

	template<typename ObjectType>
		FormatterAssetMixin_DeserializeKey<ObjectType>::FormatterAssetMixin_DeserializeKey(Blob&& blob, ::Assets::DependencyValidation&& depVal, StringSection<>)
	: _depVal(std::move(depVal))
	{
		if (blob) {
			Formatters::TextInputFormatter<char> fmttr { MakeIteratorRange(*blob).template Cast<const void*>() };
			StringSection<> keyname;
			while (fmttr.TryKeyedItem(keyname))
				if (XlEqString(keyname, "Inherit")) {
					_inherit = Internal::DeserializeInheritList(fmttr);
				} else if (!ObjectType::TryDeserializeKey(fmttr, keyname))
					Internal::SkipValueOrElement(fmttr);
		}
	}

	template<typename ObjectType>
		FormatterAssetMixin_DeserializeKey<ObjectType>::FormatterAssetMixin_DeserializeKey(ObjectType&& moveFrom, ::Assets::DirectorySearchRules&& searchRules, ::Assets::DependencyValidation&& depVal)
	: ObjectType(std::move(moveFrom)), _searchRules(std::move(searchRules)), _depVal(std::move(depVal)) {}
	template<typename ObjectType>
		FormatterAssetMixin_DeserializeKey<ObjectType>::FormatterAssetMixin_DeserializeKey(const ObjectType& copyFrom, const ::Assets::DirectorySearchRules& searchRules, const ::Assets::DependencyValidation& depVal)
	: ObjectType(copyFrom), _searchRules(searchRules), _depVal(depVal) {}

	template <typename ObjectType, typename BaseAssetType>
		void ResolvedAssetMixin<ObjectType, BaseAssetType>::ConstructToPromise(
			std::promise<std::shared_ptr<ResolvedAssetMixin<ObjectType, BaseAssetType>>>&& promise,
			StringSection<> initializer)
	{
		std::vector<::Assets::PtrToMarkerPtr<BaseAssetType>> initialFutures;
		auto i = initializer.begin();
		while (i != initializer.end()) {
			while (i != initializer.end() && *i == ';') ++i;
			auto i2 = i;
			while (i2 != initializer.end() && *i2 != ';') ++i2;
			if (i2==i) break;

			initialFutures.emplace_back(::Assets::GetAssetMarkerPtr<BaseAssetType>(MakeStringSection(i, i2)));
			i = i2;
		}
		assert(!initialFutures.empty());
		ConstructToPromise(std::move(promise), initialFutures);
	}

	template <typename ObjectType, typename BaseAssetType>
		void ResolvedAssetMixin<ObjectType, BaseAssetType>::ConstructToPromise(
			std::promise<ResolvedAssetMixin<ObjectType, BaseAssetType>>&& promise,
			StringSection<> initializer)
	{
		std::vector<::Assets::PtrToMarkerPtr<BaseAssetType>> initialFutures;
		auto i = initializer.begin();
		while (i != initializer.end()) {
			while (i != initializer.end() && *i == ';') ++i;
			auto i2 = i;
			while (i2 != initializer.end() && *i2 != ';') ++i2;
			if (i2==i) break;

			initialFutures.emplace_back(::Assets::GetAssetMarkerPtr<BaseAssetType>(MakeStringSection(i, i2)));
			i = i2;
		}
		assert(!initialFutures.empty());
		ConstructToPromise(std::move(promise), initialFutures);
	}

	template <typename ObjectType, typename BaseAssetType>
		void ResolvedAssetMixin<ObjectType, BaseAssetType>::ConstructToPromise(
			std::promise<std::shared_ptr<ResolvedAssetMixin<ObjectType, BaseAssetType>>>&& promise,
			IteratorRange<const ::Assets::PtrToMarkerPtr<BaseAssetType>*> initialBaseAssets)
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

		pendingTree->_subFutures.reserve(initialBaseAssets.size());
		unsigned siblingIdx = 0;
		for (auto initialBaseAsset:initialBaseAssets)
			pendingTree->_subFutures.emplace_back(typename PendingAssetTree::SubFutureIndexer{0, siblingIdx++}, std::move(initialBaseAsset));

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
								pendingTree->_subFutures.emplace_back(typename PendingAssetTree::SubFutureIndexer{newParentId, siblingIdx++}, ::Assets::GetAssetMarker<std::shared_ptr<BaseAssetType>>(fullResolvedName));
							} else {
								if (searchRules.GetBaseFile().IsEmpty())
									Throw(std::runtime_error("Cannot resolve reference within file because the base filename hasn't been recorded"));
								std::string fullResolvedName = Concatenate(searchRules.GetBaseFile(), ":", name);
								pendingTree->_subFutures.emplace_back(typename PendingAssetTree::SubFutureIndexer{newParentId, siblingIdx++}, ::Assets::GetAssetMarker<std::shared_ptr<BaseAssetType>>(fullResolvedName));
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
			IteratorRange<const ::Assets::PtrToMarkerPtr<BaseAssetType>*> initialBaseAssets)
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

		pendingTree->_subFutures.reserve(initialBaseAssets.size());
		unsigned siblingIdx = 0;
		for (auto initialBaseAsset:initialBaseAssets)
			pendingTree->_subFutures.emplace_back(typename PendingAssetTree::SubFutureIndexer{0, siblingIdx++}, std::move(initialBaseAsset));

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
								pendingTree->_subFutures.emplace_back(typename PendingAssetTree::SubFutureIndexer{newParentId, siblingIdx++}, ::Assets::GetAssetMarker<std::shared_ptr<BaseAssetType>>(fullResolvedName));
							} else {
								if (searchRules.GetBaseFile().IsEmpty())
									Throw(std::runtime_error("Cannot resolve reference within file because the base filename hasn't been recorded"));
								std::string fullResolvedName = Concatenate(searchRules.GetBaseFile(), ":", name);
								pendingTree->_subFutures.emplace_back(typename PendingAssetTree::SubFutureIndexer{newParentId, siblingIdx++}, ::Assets::GetAssetMarker<std::shared_ptr<BaseAssetType>>(fullResolvedName));
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
