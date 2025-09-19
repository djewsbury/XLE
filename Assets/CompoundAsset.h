#pragma once

#include "AssetHeapNew.h"
#include "AssetsCore.h"
#include "AssetMixins.h"
#include "DepVal.h"
#include "IFileSystem.h"
#include "AssetTraits.h"		// for RemoveSmartPtr
#include "Marker.h"
#include "AssetTraits.h"
#include "Assets.h"
#include "ConfigFileContainer.h"		// for AssetMixinTraits
#include "../Utility/MemoryUtils.h"
#include "../Utility/Streams/PathUtils.h"
#include "../Utility/StringUtils.h"
#include <future>
#include <stdexcept>
#include <variant>

namespace AssetsNew
{

	class CompoundAssetScaffold
	{
	public:
		struct Component
		{
			std::vector<StringSection<>> _inlineChunks;
			std::vector<StringSection<>> _externalReferences;
		};
		using ComponentTypeName = uint64_t;
		std::vector<std::pair<ComponentTypeName, Component>> _components;

		using EntityHashName = uint64_t;

		struct EntityBookkeeping
		{
			unsigned _componentTableIdx = ~0u;
			StringSection<> _name;
			unsigned _inheritBegin = ~0u, _inheritEnd = ~0u;
		};
		std::vector<std::pair<EntityHashName, EntityBookkeeping>> _entityLookup;
		std::vector<StringSection<>> _inheritLists;

		::Assets::Blob _blob;
		uint64_t _uniqueId;

		CompoundAssetScaffold(::Assets::Blob&& blob);
		~CompoundAssetScaffold();
	protected:
		void Deserialize(Formatters::TextInputFormatter<char>&);
	};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	struct ScaffoldAndEntityName
	{
		using ContextImbuedScaffold = ::Assets::ContextImbuedAsset<std::shared_ptr<CompoundAssetScaffold>>;
		ContextImbuedScaffold _scaffold;
		uint64_t _entityNameHash = ~0ull;
		DEBUG_ONLY(std::string _entityName;)

		ScaffoldAndEntityName() = default;
		ScaffoldAndEntityName(ContextImbuedScaffold&& scaffold, uint64_t entityNameHash DEBUG_ONLY(, std::string entityName={}))
		: _scaffold(std::move(scaffold)), _entityNameHash(entityNameHash) DEBUG_ONLY(, _entityName(std::move(entityName))) {}
		ScaffoldAndEntityName(const ContextImbuedScaffold& scaffold, uint64_t entityNameHash DEBUG_ONLY(, std::string entityName={}))
		: _scaffold(std::move(scaffold)), _entityNameHash(entityNameHash) DEBUG_ONLY(, _entityName(std::move(entityName))) {}

		const std::shared_ptr<CompoundAssetScaffold>& GetCompoundAssetScaffold() const { return std::get<std::shared_ptr<CompoundAssetScaffold>>(_scaffold); }
		const ::Assets::DirectorySearchRules& GetDirectorySearchRules() const { return std::get<::Assets::DirectorySearchRules>(_scaffold); }
		const ::Assets::DependencyValidation& GetDependencyValidation() const { return std::get<::Assets::DependencyValidation>(_scaffold); }
		const ::Assets::InheritList& GetInheritList() const { return std::get<::Assets::InheritList>(_scaffold); }
	};
	struct ContextAndIdentifier
	{
		std::string _identifier;
		::Assets::DirectorySearchRules _searchRules;		// these search rules are for resolving 'identifier' -- not for the references inside of that file
	};
	using ScaffoldIndexer = std::variant<std::monostate, ScaffoldAndEntityName, ContextAndIdentifier>;

	namespace Internal
	{
		T1(Type) auto MaybeSkipWrapper(Type& t) -> Type& { return t; }
		T1(Type) auto MaybeSkipWrapper(::Assets::AssetWrapper<Type>& t) -> Type& { return t.get(); }

		T1(Type) using RemoveWrapperType = std::remove_cvref_t<decltype(MaybeSkipWrapper(std::declval<Type&>()))>;
		T1(Type) static constexpr bool IsWrapperType = !std::is_same_v<std::remove_cvref_t<Type>, RemoveWrapperType<Type>>;
	}

	class CompoundAssetUtil : public std::enable_shared_from_this<CompoundAssetUtil>
	{
	public:
		using InheritList = std::vector<ScaffoldIndexer>;
		using ContextImbuedScaffold = ::Assets::ContextImbuedAsset<std::shared_ptr<CompoundAssetScaffold>>;

		T1(Type) using AssetWrapper = ::Assets::AssetWrapper<Type>;
		T1(Type) static constexpr bool s_useAssetWrapper = !::Assets::Internal::HasGetDependencyValidation<Type> && !::Assets::Internal::HasDerefGetDependencyValidation<Type> && !::Assets::Internal::HasStdGetDependencyValidation<Type>;
		T1(Type) using ConditionalWrapper = std::conditional_t<s_useAssetWrapper<Type>, AssetWrapper<Type>, Type>;

		T1(Type) [[nodiscard]] std::shared_future<ConditionalWrapper<Type>>	GetAssetFuture(uint64_t componentTypeName, const ScaffoldIndexer& rootEntity);
		T1(Type) [[nodiscard]] std::shared_future<ConditionalWrapper<Type>>	GetCachedAssetFuture(uint64_t componentTypeName, const ScaffoldIndexer& rootEntity);

		T1(Type) [[nodiscard]] auto GetAssetFuture(uint64_t componentTypeName, const ContextImbuedScaffold& scaffold, uint64_t rootEntity) { return GetAssetFuture<Type>(componentTypeName, ScaffoldAndEntityName{scaffold, rootEntity}); }

			//
			// We cache in the AssetHeap
			//		* the results of previous GetCachedAssetFuture calls (ie, with a ScaffoldIndexer)
			//			(note that these are always cached as ResolvedAsset<Type> -- ie, with the context)
			//		* all ContextImbuedScaffold's used
			//
			// However, we don't cache:
			//		* the intermediate objects created while resolving inheritance (ie, the parameters passed to MergeInWithFilenameResolve)
			//			- this may cause some duplicate parsing, particular when specific entries are inherited by objects
			//
		explicit CompoundAssetUtil(std::shared_ptr<AssetHeap> =nullptr);

	protected:
		T1(Type) using UnresolvedAsset = std::tuple<Type, ::Assets::DirectorySearchRules, ::Assets::DependencyValidation, InheritList>;
		T1(Type) using ChooseUnresolvedAssetType = UnresolvedAsset<Internal::RemoveWrapperType<Type>>;
	
		T1(MaybeWrapperType) void BuildFuture(std::promise<MaybeWrapperType>&&, uint64_t componentTypeName, const ScaffoldIndexer& rootEntity);
		T1(MaybeWrapperType) void BuildWithInheritTree_MergeInWithFilenameResolve(std::promise<MaybeWrapperType>&& promise, uint64_t componentTypeName, std::shared_future<ChooseUnresolvedAssetType<MaybeWrapperType>> baseAsset);
		T1(MaybeWrapperType) void BuildWithInheritTree_TopMost(std::promise<MaybeWrapperType>&& promise, uint64_t componentTypeName, const ScaffoldIndexer& rootEntity);
		T1(Type) void BuildUnresolvedAssetFuture(std::promise<UnresolvedAsset<Type>>&&, uint64_t componentTypeName, const ScaffoldIndexer& indexer);

		T1(Type) UnresolvedAsset<Type> BuildUnresolvedAssetSync(uint64_t componentTypeName, const ContextAndIdentifier& indexer);
		T1(Type) UnresolvedAsset<Type> BuildUnresolvedAssetSync(uint64_t componentTypeName, const ScaffoldAndEntityName& indexer);

		T1(Type) UnresolvedAsset<Type> DeserializeComponent(StringSection<> chunk, const ::Assets::DirectorySearchRules& scaffoldSearchRules, const ::Assets::DependencyValidation& scaffoldDepVal);

		T1(MaybeWrapperType) static MaybeWrapperType AsResolvedAsset(UnresolvedAsset<Internal::RemoveWrapperType<MaybeWrapperType>>&&);

		T1(Type) static void FillInInheritList(InheritList& inherited, const CompoundAssetScaffold::EntityBookkeeping& bookkeeping, std::shared_ptr<CompoundAssetScaffold> scaffold, const ::Assets::DirectorySearchRules& scaffoldSearchRules, const ::Assets::DependencyValidation& scaffoldDepVal);

		template<typename Type>
			std::shared_future<Type> RemoveContextFromFuture(std::shared_future<AssetWrapper<Type>>&& input);

		static uint64_t MakeCacheKey(const ScaffoldIndexer& indexer);
		static bool NeedToIncorporatedInheritedAssets(const ScaffoldIndexer& rootEntity);
		ScaffoldIndexer FindFirstDeserializableSync(uint64_t componentTypeName, const ScaffoldAndEntityName& indexer);

		std::shared_ptr<AssetHeap> _assetHeap;
	};

	template<typename Type>
		auto CompoundAssetUtil::BuildUnresolvedAssetSync(uint64_t componentTypeName, const ScaffoldAndEntityName& indexer) -> UnresolvedAsset<Type>
	{
		static_assert(std::is_same_v<std::remove_cvref_t<Type>, Type>);		// ensure no unwanted decorations on the type
		auto scaffold = std::get<0>(indexer._scaffold);
		assert(scaffold);
		auto i = LowerBound(scaffold->_entityLookup, indexer._entityNameHash);
		auto i2 = LowerBound(scaffold->_components, componentTypeName);
		if (	i == scaffold->_entityLookup.end() || i->first != indexer._entityNameHash
			|| 	i2 == scaffold->_components.end() || i2->first != componentTypeName) {
			// missing
			return {};
		}

		auto entityIdx = i->second._componentTableIdx;
		if (i2->second._inlineChunks.size() > entityIdx && !i2->second._inlineChunks[entityIdx].IsEmpty()) {

			UnresolvedAsset<Type> result = DeserializeComponent<Type>(i2->second._inlineChunks[entityIdx], indexer.GetDirectorySearchRules(), indexer.GetDependencyValidation());
			FillInInheritList<Type>(std::get<InheritList>(result), i->second, scaffold, indexer.GetDirectorySearchRules(), indexer.GetDependencyValidation());
			return result;

		} else if (i2->second._externalReferences.size() > entityIdx && !i2->second._externalReferences[entityIdx].IsEmpty()) {

			UnresolvedAsset<Type> result = BuildUnresolvedAssetSync<Type>(componentTypeName, ContextAndIdentifier{ i2->second._externalReferences[entityIdx].AsString(), std::get<::Assets::DirectorySearchRules>(indexer._scaffold) });
			FillInInheritList<Type>(std::get<InheritList>(result), i->second, scaffold, indexer.GetDirectorySearchRules(), indexer.GetDependencyValidation());
			return result;

		} else {

			// missing (but we still need the inherit list)
			UnresolvedAsset<Type> result;
			FillInInheritList<Type>(std::get<InheritList>(result), i->second, scaffold, indexer.GetDirectorySearchRules(), indexer.GetDependencyValidation());
			return result;

		}
	}

	template<typename Type, typename... Params> ::AssetsNew::AssetHeap::Iterator<Type> StallWhilePending(AssetHeap& heap, Params&&... initialisers)
	{
		auto cacheKey = ::Assets::Internal::BuildParamHash(initialisers...);

		if (auto l = heap.Lookup<Type>(cacheKey)) {
			l.StallWhilePending();		// lookup again because StallWhilePending invalidates the iterator
			l = heap.Lookup<Type>(cacheKey);
			if (!l) Throw(std::runtime_error("Unexpected asset erasure in StallAndActualize"));
			if (l.GetDependencyValidation().GetValidationIndex() <= 0)
				return l;
		}

		// No existing asset, or asset is invalidated. Fall through

		auto lock = heap.WriteLock<Type>();

		// We have to check again for a valid object, incase another thread modified the heap
		// before we took our write lock
		if (auto l = heap.LookupAlreadyLocked<Type>(cacheKey)) {
			if (l.GetState() == ::Assets::AssetState::Pending) {
				// There's been an issue. Another thread has changed this entry unexpectedly. We need to restart from the top
				lock = {};
				return StallWhilePending<Type>(heap, std::forward<Params>(initialisers)...);
			}

			if (l.GetDependencyValidation().GetValidationIndex() <= 0)
				return l;		// another thread changed the entry, and it completed quickly
		}

		std::promise<Type> promise;
		heap.InsertAlreadyLocked<Type>(cacheKey, ::Assets::Internal::AsString(initialisers...), promise.get_future());

		lock = {};
		::Assets::AutoConstructToPromise(std::move(promise), std::forward<Params>(initialisers)...);

		auto l = heap.Lookup<Type>(cacheKey);
		assert(l);
		l.StallWhilePending();
		l = heap.Lookup<Type>(cacheKey);		// lookup again because StallWhilePending invalidates the iterator
		assert(l);

		// note that we return this even if it became invalidated during the construction
		// Also note that we can't return the result of l.Actualize(), because that gives a reference into the table, which is only valid while the iterator lock is held
		return l;
	}

	template<typename Type>
		auto CompoundAssetUtil::BuildUnresolvedAssetSync(uint64_t componentTypeName, const ContextAndIdentifier& indexer) -> UnresolvedAsset<Type>
	{
		// Assuming we are already in a background thread
		static_assert(std::is_same_v<std::remove_cvref_t<Type>, Type>);		// ensure no unwanted decorations on the type
		auto splitName = MakeFileNameSplitter(indexer._identifier);
		char resolvedFile[MaxPath];
		indexer._searchRules.ResolveFile(resolvedFile, splitName.AllExceptParameters());
		if (XlEqString(splitName.Extension(), "compound") || XlEqString(splitName.Extension(), "hlsl")) {

			// load via compound document path
			if (_assetHeap) {
				auto scaffoldWithContext = StallWhilePending<ContextImbuedScaffold>(*_assetHeap, resolvedFile);
				// copy actualize result, then unlock
				auto lookup = ScaffoldAndEntityName{scaffoldWithContext.Actualize(), Hash64(splitName.Parameters()) DEBUG_ONLY(, splitName.Parameters().AsString())};
				scaffoldWithContext = {};
				return BuildUnresolvedAssetSync<Type>(componentTypeName, std::move(lookup));
			} else {
				auto& scaffoldWithContext = ::Assets::ActualizeAsset<ContextImbuedScaffold>(resolvedFile);
				auto lookup = ScaffoldAndEntityName{scaffoldWithContext, Hash64(splitName.Parameters()) DEBUG_ONLY(, splitName.Parameters().AsString())};
				return BuildUnresolvedAssetSync<Type>(componentTypeName, std::move(lookup));
			}

		} else {

			if constexpr (::Assets::Internal::HasConstructor_SimpleFormatter<Type>) {

				UnresolvedAsset<Type> t;
				::Assets::InheritList srcInherited;
				if (_assetHeap) {
					auto iterator = StallWhilePending<::Assets::ContextImbuedAsset<Type>>(*_assetHeap, resolvedFile);
					auto& asset = iterator.Actualize();
					std::get<::Assets::DependencyValidation>(t) = std::get<::Assets::DependencyValidation>(asset);
					std::get<::Assets::DirectorySearchRules>(t) = std::get<::Assets::DirectorySearchRules>(asset);
					std::get<0>(t) = std::get<0>(asset);
					srcInherited = std::get<::Assets::InheritList>(asset);
				} else {
					auto& asset = ::Assets::ActualizeAsset<::Assets::ContextImbuedAsset<Type>>(resolvedFile);
					std::get<::Assets::DependencyValidation>(t) = std::get<::Assets::DependencyValidation>(asset);
					std::get<::Assets::DirectorySearchRules>(t) = std::get<::Assets::DirectorySearchRules>(asset);
					std::get<0>(t) = std::get<0>(asset);
					srcInherited = std::get<::Assets::InheritList>(asset);
				}

				if constexpr (::Assets::Internal::AssetMixinTraits<::Assets::Internal::RemoveSmartPtrType<Type>>::HasDeserializeKey) {
					auto& inherited = std::get<InheritList>(t);
					inherited.reserve(srcInherited.size());
					for (auto i:srcInherited)
						inherited.push_back(ContextAndIdentifier{i, std::get<::Assets::DirectorySearchRules>(t)});
				}
				
				return t;

			} else if constexpr (std::is_constructible_v<::Assets::Internal::RemoveSmartPtrType<Type>, Formatters::TextInputFormatter<>&, const ::Assets::DirectorySearchRules&, const ::Assets::DependencyValidation&, CompoundAssetUtil&>) {

				Throw(std::runtime_error("CompoundAssetUtil construction path only valid when loading from compound asset files"));

			} else {

				UnresolvedAsset<Type> t;
				if (_assetHeap) {
					auto iterator = StallWhilePending<ConditionalWrapper<Type>>(*_assetHeap, resolvedFile);
					auto& asset = iterator.Actualize();
					std::get<::Assets::DependencyValidation>(t) = ::Assets::Internal::GetDependencyValidation(asset);
					// std::get<::Assets::DirectorySearchRules>(t) = asset->GetDirectorySearchRules();
					std::get<0>(t) = asset;
				} else {
					auto asset = ::Assets::ActualizeAsset<ConditionalWrapper<Type>>(resolvedFile);
					UnresolvedAsset<Type> t;
					std::get<::Assets::DependencyValidation>(t) = ::Assets::Internal::GetDependencyValidation(asset);
					// std::get<::Assets::DirectorySearchRules>(t) = asset->GetDirectorySearchRules();
					std::get<0>(t) = asset;
				}

				return t;

			}

		}
	}

	template<typename Type>
		void CompoundAssetUtil::FillInInheritList(InheritList& inherited, const CompoundAssetScaffold::EntityBookkeeping& bookkeeping, std::shared_ptr<CompoundAssetScaffold> scaffold, const ::Assets::DirectorySearchRules& scaffoldSearchRules, const ::Assets::DependencyValidation& scaffoldDepVal)
	{
		if (bookkeeping._inheritBegin != bookkeeping._inheritEnd) {
			inherited.reserve(bookkeeping._inheritEnd - bookkeeping._inheritBegin);
			for (auto i:MakeIteratorRange(scaffold->_inheritLists.begin()+bookkeeping._inheritBegin, scaffold->_inheritLists.begin()+bookkeeping._inheritEnd)) {
				auto hashName = Hash64(i);
				auto q = LowerBound(scaffold->_entityLookup, hashName);
				if (q != scaffold->_entityLookup.end() && q->first == hashName) {
					inherited.emplace_back(ScaffoldAndEntityName{{scaffold, scaffoldSearchRules, scaffoldDepVal, {}}, hashName DEBUG_ONLY(,i.AsString())});
				} else {
					inherited.emplace_back(ContextAndIdentifier{i.AsString(), scaffoldSearchRules});
				}
			}
		}
	}

	template<typename Type>
		auto CompoundAssetUtil::DeserializeComponent(StringSection<> chunk, const ::Assets::DirectorySearchRules& scaffoldSearchRules, const ::Assets::DependencyValidation& scaffoldDepVal) -> UnresolvedAsset<Type>
	{
		using T = ::Assets::Internal::RemoveSmartPtrType<Type>;
		if constexpr (std::is_constructible_v<T, Formatters::TextInputFormatter<>&, const ::Assets::DirectorySearchRules&, const ::Assets::DependencyValidation&, CompoundAssetUtil&>) {

			Formatters::TextInputFormatter<> fmttr { chunk };
			UnresolvedAsset<Type> result {
				::Assets::Internal::InvokeAssetConstructor<Type>(fmttr, scaffoldSearchRules, scaffoldDepVal, std::ref(*this)),
				scaffoldSearchRules, scaffoldDepVal, InheritList{} };
			if constexpr (::Assets::Internal::HasGetDependencyValidation<T>)
				std::get<::Assets::DependencyValidation>(result) = ::Assets::Internal::MaybeDeref(std::get<0>(result)).GetDependencyValidation();
			return result;

		} else if constexpr (std::is_constructible_v<T, Formatters::TextInputFormatter<>&, const ::Assets::DirectorySearchRules&, const ::Assets::DependencyValidation&>) {

			Formatters::TextInputFormatter<> fmttr { chunk };
			UnresolvedAsset<Type> result {
				::Assets::Internal::InvokeAssetConstructor<Type>(fmttr, scaffoldSearchRules, scaffoldDepVal),
				scaffoldSearchRules, scaffoldDepVal, InheritList{} };
			return result;

		} else if constexpr (std::is_constructible_v<T, Formatters::TextInputFormatter<>&>) {

			Formatters::TextInputFormatter<> fmttr { chunk };
			UnresolvedAsset<Type> result {
				::Assets::Internal::InvokeAssetConstructor<Type>(fmttr),
				scaffoldSearchRules, scaffoldDepVal, InheritList{} };
			return result;

		} else if constexpr (std::is_constructible_v<T, StringSection<>, const ::Assets::DirectorySearchRules&, const ::Assets::DependencyValidation&>) {

			UnresolvedAsset<Type> result {
				::Assets::Internal::InvokeAssetConstructor<Type>(chunk, scaffoldSearchRules, scaffoldDepVal),
				scaffoldSearchRules, scaffoldDepVal, InheritList{} };
			return result;

		} else {

			assert(0);
			return {};

		}
	}

	template<typename Type>
		void CompoundAssetUtil::BuildUnresolvedAssetFuture(std::promise<UnresolvedAsset<Type>>&& promise, uint64_t componentTypeName, const ScaffoldIndexer& indexer)
	{
		::ConsoleRig::GlobalServices::GetInstance().GetShortTaskThreadPool().Enqueue(
			[promise=std::move(promise), indexer, weakThis=weak_from_this(), componentTypeName]() mutable {

				TRY {
					auto l = weakThis.lock();
					if (!l) Throw(std::runtime_error("CompoundAssetUtil expired before promise could be fulfilled"));
					if (auto* scaffoldAndEntity = std::get_if<ScaffoldAndEntityName>(&indexer)) {
						promise.set_value(l->BuildUnresolvedAssetSync<Type>(componentTypeName, *scaffoldAndEntity));
					} else if (auto* contextAndId = std::get_if<ContextAndIdentifier>(&indexer)) {
						promise.set_value(l->BuildUnresolvedAssetSync<Type>(componentTypeName, *contextAndId));
					} else {
						Throw(std::runtime_error("ScaffoldIndexer type unsupported"));
					}
				} CATCH (...) {
					promise.set_exception(std::current_exception());
				} CATCH_END

			});
	}

	namespace Internal
	{
		T1(MaybeWrapperType) MaybeWrapperType ConstructEmptyAsset(::Assets::DependencyValidation&& depVal)
		{
			if constexpr (IsWrapperType<MaybeWrapperType>) {
				return { ::Assets::Internal::InvokeAssetConstructor<Internal::RemoveWrapperType<MaybeWrapperType>>(), std::move(depVal) };
			} else {
				return ::Assets::Internal::InvokeAssetConstructor<MaybeWrapperType>(std::move(depVal));
			}
		}
	}

	template<typename MaybeWrapperType>
		void CompoundAssetUtil::BuildWithInheritTree_MergeInWithFilenameResolve(
			std::promise<MaybeWrapperType>&& promise,
			uint64_t componentTypeName, std::shared_future<ChooseUnresolvedAssetType<MaybeWrapperType>> baseAsset)
	{
		using Type = Internal::RemoveWrapperType<MaybeWrapperType>;
		static_assert(::Assets::Internal::AssetMixinTraits<::Assets::Internal::RemoveSmartPtrType<Type>>::HasMergeInWithFilenameResolve);

		// Starting with just the given root entity, expand out a tree of everything requires to be merged together to result in 
		// a final resolved asset
		// Note similar behaviour in ResolvedAssetMixin<>::ConstructToPromise

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
			std::vector<std::pair<SubFutureIndexer, std::shared_future<UnresolvedAsset<Type>>>> _subFutures;
			std::vector<std::pair<LoadedSubMaterialsIndexer, UnresolvedAsset<Type>>> _loadedSubAssets;
			std::vector<::Assets::DependencyValidation> _depVals;
		};
		auto pendingTree = std::make_shared<PendingAssetTree>();

		unsigned siblingIdx = 0;
		pendingTree->_subFutures.emplace_back(typename PendingAssetTree::SubFutureIndexer{0, siblingIdx++}, std::move(baseAsset));

		::Assets::PollToPromise(
			std::move(promise),
			[pendingTree, weakThis = weak_from_this(), componentTypeName](std::chrono::microseconds timeout) {
				auto timeoutTime = std::chrono::steady_clock::now() + timeout;
				
				auto l = weakThis.lock();
				if (!l) Throw(std::runtime_error("CompoundAssetUtil expired before promise could be fulfilled"));

				// check status of pending futures
				std::vector<std::pair<typename PendingAssetTree::SubFutureIndexer, UnresolvedAsset<Type>>> subMaterials;
				std::vector<::Assets::DependencyValidation> subDepVals;
				for (const auto& f:pendingTree->_subFutures) {
					if (f.second.wait_until(timeoutTime) == std::future_status::timeout)
						return ::Assets::PollStatus::Continue;

					// unlike ResolvedAssetMixin<>::ConstructToPromise, we don't tolerate invalid assets here
					auto queried = f.second.get();
					subDepVals.push_back(std::get<::Assets::DependencyValidation>(queried));
					subMaterials.emplace_back(f.first, std::move(queried));
				}
				pendingTree->_subFutures.clear();
				pendingTree->_depVals.insert(pendingTree->_depVals.end(), subDepVals.begin(), subDepVals.end());

				// merge these RawMats into _loadedSubAssets in the right places
				// also queue the next level of loads as we go
				// We want each subMaterial to go into _loadedSubAssets in the same order as 
				// in subMaterials, but immediately before their parent
				for (const auto&m:subMaterials) {
					unsigned newParentId = pendingTree->_nextId++;
					auto insertionPoint = pendingTree->_loadedSubAssets.end();
					if (m.first._parentId != 0) {
						// ie, not a root
						// Insert just before the parent, after any siblings added this turn
						// This will give us the right ordering because we ensure that we complete all items in pendingTree->_subFutures (and therefor all siblings)
						// before we process any here
						insertionPoint = std::find_if(
							pendingTree->_loadedSubAssets.begin(), pendingTree->_loadedSubAssets.end(),
							[s=m.first._parentId](const auto& c) { return c.first._itemId == s;});
						assert(insertionPoint!=pendingTree->_loadedSubAssets.end());
					}
					pendingTree->_loadedSubAssets.emplace(insertionPoint, typename PendingAssetTree::LoadedSubMaterialsIndexer{newParentId, m.first._parentId, m.first._siblingIdx}, m.second);

					// Find the assets that are inherited by the asset we just loaded, and queue those to load
					unsigned siblingIdx = 0;
					for (const auto& indexer:std::get<InheritList>(m.second)) {
						std::promise<UnresolvedAsset<Type>> subPromise;
						auto f = subPromise.get_future();
						l->BuildUnresolvedAssetFuture(std::move(subPromise), componentTypeName, indexer);
						pendingTree->_subFutures.emplace_back(typename PendingAssetTree::SubFutureIndexer{newParentId, siblingIdx++}, std::move(f));
					}
				}

					// if we still have sub-futures, need to roll around again
					// we'll do this immediately, just incase everything is already loaded
				return (pendingTree->_subFutures.empty()) ? ::Assets::PollStatus::Finish : ::Assets::PollStatus::Continue;
			},
			[pendingTree]() {
				// All of the assets in the tree are loaded; and we can just merge them together
				// into a final resolved material
				#if defined(_DEBUG)
					if (!pendingTree->_loadedSubAssets.empty())
						for (auto i=pendingTree->_loadedSubAssets.begin(); (i+1)!=pendingTree->_loadedSubAssets.end(); ++i)
							assert(i->first._parentId != (i+1)->first._parentId || i->first._siblingIdx < (i+1)->first._siblingIdx);        // double check ordering is as expected
				#endif

				VLA(::Assets::DependencyValidationMarker, depVals, pendingTree->_depVals.size());
				for (unsigned c=0; c<pendingTree->_depVals.size(); c++) depVals[c] = pendingTree->_depVals[c];
				auto finalDepVal = ::Assets::GetDepValSys().MakeOrReuse(MakeIteratorRange(depVals, &depVals[pendingTree->_depVals.size()]));

				auto finalAsset = Internal::ConstructEmptyAsset<MaybeWrapperType>(std::move(finalDepVal));
				// have to call "MergeInWithFilenameResolve" for all (even the first), because it may resolve internal filenames, etc
				for (const auto& i:pendingTree->_loadedSubAssets)
					if (!Assets::Internal::IsNullPointer(std::get<0>(i.second)))
						::Assets::Internal::MaybeDeref(Internal::MaybeSkipWrapper(finalAsset)).MergeInWithFilenameResolve(::Assets::Internal::MaybeDeref(std::get<0>(i.second)), std::get<::Assets::DirectorySearchRules>(i.second));
				
				return finalAsset;
			});

	}

	namespace Internal
	{
		std::optional<ScaffoldAndEntityName> TryMakeScaffoldAndEntityNameSync(StringSection<> str, const ::Assets::DirectorySearchRules& searchRules);	// will stall
	}

	template<typename MaybeWrapperType>
		auto CompoundAssetUtil::AsResolvedAsset(UnresolvedAsset<Internal::RemoveWrapperType<MaybeWrapperType>>&& in) -> MaybeWrapperType
	{
		using Type = Internal::RemoveWrapperType<MaybeWrapperType>;
		if constexpr (::Assets::Internal::AssetMixinTraits<::Assets::Internal::RemoveSmartPtrType<Type>>::HasMergeInWithFilenameResolve) {
			if constexpr (Internal::IsWrapperType<MaybeWrapperType>) {
				Type finalAsset = ::Assets::Internal::InvokeAssetConstructor<Type>();
				::Assets::Internal::MaybeDeref(finalAsset).MergeInWithFilenameResolve(::Assets::Internal::MaybeDeref(std::get<0>(std::move(in))), std::get<::Assets::DirectorySearchRules>(in));
				return { finalAsset, std::get<::Assets::DependencyValidation>(std::move(in)) };
			} else {
				Type finalAsset = ::Assets::Internal::InvokeAssetConstructor<Type>(std::get<::Assets::DependencyValidation>(std::move(in)));
				::Assets::Internal::MaybeDeref(finalAsset).MergeInWithFilenameResolve(::Assets::Internal::MaybeDeref(std::get<0>(std::move(in))), std::get<::Assets::DirectorySearchRules>(in));
				return finalAsset;
			}
		} else {
			if constexpr (Internal::IsWrapperType<MaybeWrapperType>) {
				return AssetWrapper<Type> { std::get<0>(std::move(in)), std::get<::Assets::DependencyValidation>(std::move(in)) };
			} else {
				return std::get<0>(std::move(in));
			}
		}
	}

	template<typename MaybeWrapperType>
		void CompoundAssetUtil::BuildWithInheritTree_TopMost(std::promise<MaybeWrapperType>&& promise, uint64_t componentTypeName, const ScaffoldIndexer& rootEntity)
	{
		using Type = Internal::RemoveWrapperType<MaybeWrapperType>;
		assert(rootEntity.index() != 0);
		::ConsoleRig::GlobalServices::GetInstance().GetShortTaskThreadPool().Enqueue(
			[promise=std::move(promise), indexer=rootEntity, weakThis=weak_from_this(), componentTypeName]() mutable {

				TRY {
					auto l = weakThis.lock();
					if (!l) Throw(std::runtime_error("CompoundAssetUtil expired before promise could be fulfilled"));

					if (auto* contextAndId = std::get_if<ContextAndIdentifier>(&indexer))
						if (auto transformed = Internal::TryMakeScaffoldAndEntityNameSync(contextAndId->_identifier, contextAndId->_searchRules)) {
							indexer = *transformed;
							assert(indexer.index() != 0);
						}

					if (auto* scaffoldAndEntity = std::get_if<ScaffoldAndEntityName>(&indexer)) {
						indexer = l->FindFirstDeserializableSync(componentTypeName, *scaffoldAndEntity);
						assert(indexer.index() != 0);
					}

					if (auto* scaffoldAndEntity = std::get_if<ScaffoldAndEntityName>(&indexer)) {
						promise.set_value(AsResolvedAsset<MaybeWrapperType>(l->BuildUnresolvedAssetSync<Type>(componentTypeName, *scaffoldAndEntity)));
					} else if (auto* contextAndId = std::get_if<ContextAndIdentifier>(&indexer)) {
						// inheritanceMechanism not supported in this case
						promise.set_value(AsResolvedAsset<MaybeWrapperType>(l->BuildUnresolvedAssetSync<Type>(componentTypeName, *contextAndId)));
					} else {
						Throw(std::runtime_error("ScaffoldIndexer type unsupported"));
					}

				} CATCH (...) {
					promise.set_exception(std::current_exception());
				} CATCH_END

			});
	}

	template<typename MaybeWrapperType>
		void CompoundAssetUtil::BuildFuture(std::promise<MaybeWrapperType>&& promise, uint64_t componentTypeName, const ScaffoldIndexer& rootEntity)
	{
		// first test if this is a scenario where we need to the inheritance handling structure
		using Type = Internal::RemoveWrapperType<MaybeWrapperType>;
		bool inheritanceMechanism = NeedToIncorporatedInheritedAssets(rootEntity);
		if (inheritanceMechanism) {

			if constexpr (::Assets::Internal::AssetMixinTraits<::Assets::Internal::RemoveSmartPtrType<Type>>::HasMergeInWithFilenameResolve) {

				std::promise<UnresolvedAsset<Type>> baseAssetPromise;
				auto baseAssetFuture = baseAssetPromise.get_future();
				BuildUnresolvedAssetFuture(std::move(baseAssetPromise), componentTypeName, rootEntity);
				BuildWithInheritTree_MergeInWithFilenameResolve<MaybeWrapperType>(std::move(promise), componentTypeName, std::move(baseAssetFuture));

			} else {

				// track down the first component in the inheritance tree that actually exists
				BuildWithInheritTree_TopMost(std::move(promise), componentTypeName, rootEntity);

			}

		} else {

			// or go direct from the unresolved asset to the resolved asset
			::ConsoleRig::GlobalServices::GetInstance().GetShortTaskThreadPool().Enqueue(
				[promise=std::move(promise), indexer=rootEntity, weakThis=weak_from_this(), componentTypeName, inheritanceMechanism]() mutable {

					TRY {
						auto l = weakThis.lock();
						if (!l) Throw(std::runtime_error("CompoundAssetUtil expired before promise could be fulfilled"));

						if (auto* scaffoldAndEntity = std::get_if<ScaffoldAndEntityName>(&indexer)) {

							promise.set_value(AsResolvedAsset<MaybeWrapperType>(l->BuildUnresolvedAssetSync<Type>(componentTypeName, *scaffoldAndEntity)));

						} else if (auto* contextAndId = std::get_if<ContextAndIdentifier>(&indexer)) {

							promise.set_value(AsResolvedAsset<MaybeWrapperType>(l->BuildUnresolvedAssetSync<Type>(componentTypeName, *contextAndId)));

						} else {
							Throw(std::runtime_error("ScaffoldIndexer type unsupported"));
						}

					} CATCH (...) {
						promise.set_exception(std::current_exception());
					} CATCH_END

				});

		}
	}

	template<typename Type>
		auto CompoundAssetUtil::GetAssetFuture(uint64_t componentTypeName, const ScaffoldIndexer& rootEntity) -> std::shared_future<ConditionalWrapper<Type>>
	{
		std::promise<ConditionalWrapper<Type>> p;
		auto resolvedFuture = p.get_future();
		BuildFuture<ConditionalWrapper<Type>>(std::move(p), componentTypeName, rootEntity);
		return resolvedFuture;
	}

	template<typename Type>
		auto CompoundAssetUtil::GetCachedAssetFuture(uint64_t componentTypeName, const ScaffoldIndexer& rootEntity) -> std::shared_future<ConditionalWrapper<Type>>
	{
		assert(_assetHeap);

		// Try to get from the cache first
		auto cacheKey = MakeCacheKey(rootEntity);
		cacheKey = HashCombine(cacheKey, componentTypeName);
		if (auto cached = _assetHeap->Lookup<ConditionalWrapper<Type>>(cacheKey); cached) {
			// this only checks valid state for foreground / visible completions
			bool invalidated = cached.GetDependencyValidation().GetValidationIndex() > 0;
			if (!invalidated)
				return cached.GetFuture();
		}

		std::promise<ConditionalWrapper<Type>> p;
		std::shared_future<ConditionalWrapper<Type>> resolvedFuture = p.get_future();
		BuildFuture<ConditionalWrapper<Type>>(std::move(p), componentTypeName, rootEntity);
		_assetHeap->Insert(cacheKey, {}, std::shared_future<ConditionalWrapper<Type>>{resolvedFuture});
		return resolvedFuture;
	}

	inline CompoundAssetUtil::CompoundAssetUtil(std::shared_ptr<AssetHeap> assetHeap) : _assetHeap(std::move(assetHeap))
	{}

	template<typename Type>
		inline std::shared_future<Type> RemoveContextFromFuture(std::shared_future<CompoundAssetUtil::AssetWrapper<Type>>&& input)
	{
		std::promise<Type> finalResult;
		auto f = finalResult.get_future();
		::Assets::WhenAll(std::move(input)).ThenConstructToPromise(
			std::move(finalResult), [](auto&& a) { return std::get<0>(std::move(a)); });
		return f;
	}

}

