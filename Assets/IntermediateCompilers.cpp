// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "IntermediateCompilers.h"
#include "AssetsCore.h"
#include "ICompileOperation.h"
#include "DepVal.h"
#include "AssetUtils.h"
#include "IntermediatesStore.h"
#include "IArtifact.h"
#include "InitializerPack.h"
#include "CompilerLibrary.h"
#include "ArchiveCache.h"
#include "IFileSystem.h"
#include "OperationContext.h"
#include "../OSServices/AttachableLibrary.h"
#include "../ConsoleRig/GlobalServices.h"
#include "../ConsoleRig/Plugins.h"
#include "../OSServices/Log.h"
#include "../OSServices/RawFS.h"		// for OSServices::GetProcessPath()
#include "../Utility/Threading/Mutex.h"
#include "../Utility/Streams/PathUtils.h"
#include "../Utility/StringFormat.h"
#include "../Utility/Threading/CompletionThreadPool.h"
#include "wildcards.hpp"
#include <set>
#include <unordered_map>
#include <atomic>

using namespace Utility::Literals;

namespace Assets 
{
	static const auto ChunkType_Log = ConstHash64Legacy<'Log'>::Value;

	struct ExtensionAndDelegate
	{
		std::string _name;
		OSServices::LibVersionDesc _srcVersion = {nullptr, nullptr};
		IIntermediateCompilers::CompileOperationDelegate _delegate;
		IIntermediateCompilers::CompileOperationDelegate2 _delegateWithConduit;
		IIntermediateCompilers::ArchiveNameDelegate _archiveNameDelegate;
		DependencyValidation _compilerLibraryDepVal;
		IIntermediatesStore::CompileProductsGroupId _storeGroupId = 0;
		std::atomic<bool> _shuttingDown = false;
		std::atomic<unsigned> _activeOperationCount = 0;
	};

	struct DelegateAssociation
	{
		std::vector<CompileRequestCode> _compileRequestCodes;
		std::string _matchPattern;
	};

///////////////////////////////////////////////////////////////////////////////////////////////////

	class IntermediateCompilers : public IIntermediateCompilers
    {
    public:
        virtual std::shared_ptr<IIntermediateCompileMarker> Prepare(CompileRequestCode, InitializerPack&&) override;
        virtual void StallOnPendingOperations(bool cancelAll) override;
		
		virtual RegisteredCompilerId RegisterCompiler(
			const std::string& name,
			const std::string& shortName,
			OSServices::LibVersionDesc srcVersion,
			const DependencyValidation& compilerDepVal,
			CompileOperationDelegate&& delegate,
			ArchiveNameDelegate&& archiveNameDelegate
			) override;

		virtual RegisteredCompilerId RegisterCompiler(
			const std::string& name,
			const std::string& shortName,
			OSServices::LibVersionDesc srcVersion,
			const DependencyValidation& compilerDepVal,
			CompileOperationDelegate2&& delegate,
			ArchiveNameDelegate&& archiveNameDelegate
			) override;

		virtual void DeregisterCompiler(RegisteredCompilerId id) override;

		virtual bool HasAssociatedCompiler(CompileRequestCode targetCode, StringSection<> firstInitializer) override;

		virtual void AssociateRequest(
			RegisteredCompilerId compiler,
			IteratorRange<const uint64_t*> outputAssetTypes,
			const std::string& matchPattern
			) override;

		virtual void AssociateExtensions(RegisteredCompilerId associatedCompiler, const std::string& commaSeparatedExtensions) override;
		virtual std::vector<std::pair<std::string, std::string>> GetExtensionsForTargetCode(CompileRequestCode typeCode) override;
		virtual std::vector<uint64_t> GetTargetCodesForExtension(StringSection<>) override;

		virtual void FlushCachedMarkers() override;

		IntermediateCompilers(std::shared_ptr<IIntermediatesStore> store);
        ~IntermediateCompilers();
    protected:
		class Marker;

        Threading::Mutex _delegatesLock;
		std::vector<std::pair<RegisteredCompilerId, std::shared_ptr<ExtensionAndDelegate>>> _delegates;
		std::unordered_multimap<RegisteredCompilerId, std::string> _extensionsAndDelegatesMap;
		std::unordered_multimap<RegisteredCompilerId, DelegateAssociation> _requestAssociations;
		std::unordered_map<uint64_t, std::shared_ptr<Marker>> _markers;
		std::shared_ptr<IIntermediatesStore> _store;
		RegisteredCompilerId _nextCompilerId = 1;
    };

    class IntermediateCompilers::Marker : public IIntermediateCompileMarker
    {
    public:
		using IdentifiersList = IteratorRange<const StringSection<>*>;
        std::pair<std::shared_ptr<IArtifactCollection>, ArtifactCollectionFuture> GetArtifact(CompileRequestCode, OperationContext*) override;
		ArtifactCollectionFuture InvokeCompile(CompileRequestCode targetCode, OperationContext*) override;
		std::string GetCompilerDescription() const override;
		RegisteredCompilerId GetRegisteredCompilerId() { return _registeredCompilerId; }
		void StallForActiveFuture();
		void AttachConduit(VariantFunctions&& conduit) override { ScopedLock(_conduitLock); _conduit = std::move(conduit); }

        Marker(
            InitializerPack&& requestName,
            std::shared_ptr<ExtensionAndDelegate> delegate,
			RegisteredCompilerId registeredCompilerId,
			std::shared_ptr<IIntermediatesStore> intermediateStore);
        ~Marker();
    private:
        std::weak_ptr<ExtensionAndDelegate> _delegate;
		std::shared_ptr<IIntermediatesStore> _intermediateStore;
        InitializerPack _initializers;
		RegisteredCompilerId _registeredCompilerId;
		VariantFunctions _conduit;
		Threading::Mutex _conduitLock;

		Threading::Mutex _activeFutureLock;
		std::weak_ptr<std::shared_future<ArtifactCollectionFuture::ArtifactCollectionSet>> _activeFuture;

		static void PerformCompile(
			const ExtensionAndDelegate& delegate,
			const InitializerPack& initializers,
			const VariantFunctions& conduit,
			OperationContextHelper&& contextHelper,
			std::promise<ArtifactCollectionFuture::ArtifactCollectionSet>&& compileMarker,
			IIntermediatesStore* destinationStore);
		std::shared_future<ArtifactCollectionFuture::ArtifactCollectionSet> InvokeCompileInternal(OperationContextHelper&&);
    };

    std::pair<std::shared_ptr<IArtifactCollection>, ArtifactCollectionFuture> IntermediateCompilers::Marker::GetArtifact(CompileRequestCode targetCode, OperationContext* opContext)
    {
		auto d = _delegate.lock();
		if (!d) Throw(std::runtime_error("Compiler delegate has expired"));

		// do everything in a lock, to avoid issues with _activeFuture
		ScopedLock(_activeFutureLock);

		// if multiple threads request the same compile at the same time, ensure that we return the same future
		// this will happen because a single compile operation can return multiple artifacts, which are required for
		// different assets/systems
		if (auto f = _activeFuture.lock())
			return { nullptr, ArtifactCollectionFuture{std::move(f), targetCode} };

		if (_intermediateStore) {
			std::shared_ptr<IArtifactCollection> existingCollection;
			if (d->_archiveNameDelegate) {
				auto archiveEntry = d->_archiveNameDelegate(targetCode, _initializers);
				if (!archiveEntry._archive.empty())
					existingCollection = _intermediateStore->RetrieveCompileProducts(archiveEntry._archive, archiveEntry._entryId, d->_storeGroupId);
			} else {
				StringMeld<MaxPath> nameWithTargetCode;
				nameWithTargetCode << _initializers.ArchivableName() << "-" << std::hex << targetCode;
				existingCollection = _intermediateStore->RetrieveCompileProducts(nameWithTargetCode.AsStringSection(), d->_storeGroupId);
			}

			if (existingCollection)
				return {std::move(existingCollection), ArtifactCollectionFuture{}};

			if (!_intermediateStore->AllowStore())
				// cannot be constructed because a valid object does not exist in the store, and compiling and storing new things is not allowed by the store
				Throw(std::runtime_error("Compilation is not allowed by the intermediate store"));
		}

		OperationContextHelper contextHelper;
		if (opContext)
			contextHelper = opContext->Begin(Concatenate("Compiling (", _initializers.ArchivableName(), ") with compiler (", GetCompilerDescription(), ")"));

		auto invokedCompile = InvokeCompileInternal(std::move(contextHelper));
		// awkward ptr setup so we can track references on _activeFuture
		auto newFuture = std::make_shared<std::shared_future<ArtifactCollectionFuture::ArtifactCollectionSet>>(std::move(invokedCompile));
		_activeFuture = newFuture;
		ArtifactCollectionFuture result{std::move(newFuture), targetCode};
		DEBUG_ONLY(result.SetDebugLabel(_initializers.ArchivableName()));
		return { nullptr, std::move(result) };
    }

	ArtifactCollectionFuture IntermediateCompilers::Marker::InvokeCompile(CompileRequestCode targetCode, OperationContext* opContext)
	{
		ScopedLock(_activeFutureLock);
		if (auto f = _activeFuture.lock())
			return ArtifactCollectionFuture{std::move(f), targetCode};

		OperationContextHelper contextHelper;
		if (opContext)
			contextHelper = opContext->Begin(Concatenate("Compiling (", _initializers.ArchivableName(), ") with compiler (", GetCompilerDescription(), ")"));

		auto invokedCompile = InvokeCompileInternal(std::move(contextHelper));
		auto newFuture = std::make_shared<std::shared_future<ArtifactCollectionFuture::ArtifactCollectionSet>>(std::move(invokedCompile));
		_activeFuture = newFuture;
		ArtifactCollectionFuture result{std::move(newFuture), targetCode};
		DEBUG_ONLY(result.SetDebugLabel(_initializers.ArchivableName()));
		return result;
	}

	static DependencyValidation AsSingleDepVal(IteratorRange<const DependencyValidation*> depVals)
	{
		VLA(DependencyValidationMarker, markers, depVals.size());
		for (unsigned c=0; c<depVals.size(); ++c) markers[c] = depVals[c];
		return GetDepValSys().MakeOrReuse(MakeIteratorRange(markers, &markers[depVals.size()]));
	}

	void IntermediateCompilers::Marker::PerformCompile(
		const ExtensionAndDelegate& delegate,
		const InitializerPack& initializers,
		const VariantFunctions& conduit,
		OperationContextHelper&& opHelper,
		std::promise<ArtifactCollectionFuture::ArtifactCollectionSet>&& promise,
		IIntermediatesStore* destinationStore)
    {
		assert(!initializers.IsEmpty());

        TRY
        {
			std::shared_ptr<ICompileOperation> compileOperation;
			if (delegate._delegateWithConduit) {
				compileOperation = delegate._delegateWithConduit(initializers, std::move(opHelper), conduit);
			} else
            	compileOperation = delegate._delegate(initializers);
			if (!compileOperation)
				Throw(std::runtime_error("Compiler library returned null to compile request on " + initializers.ArchivableName()));

			std::vector<std::pair<CompileRequestCode, std::shared_ptr<IArtifactCollection>>> finalCollections;

			std::vector<DependencyValidation> compilerDepVals;
			compilerDepVals.push_back(delegate._compilerLibraryDepVal);

			#if defined(_DEBUG)
				std::set<std::string> compileProductNamesWritten;
			#endif

			// ICompileOperations can have multiple "targets", and then those targets can have multiple
			// chunks within them. Each target should generally maps onto a single "asset" 
			// (with separate "state" values, etc). But an asset can be constructed from multiple chunks

			// note that there's a problem here -- if we've compiled a particular operation and it produced
			// a specific target; and then later on we compile again but this time the operation does not
			// produce that same output target, then the target remains in the cache and will not be removed

			auto targets = compileOperation->GetTargets();
			finalCollections.reserve(targets.size());
			for (unsigned t=0; t<targets.size(); ++t) {
				const auto& target = targets[t];

				SerializedTarget serializedTarget;
				AssetState state = AssetState::Pending;
				std::vector<DependencyValidation> targetDependencies = compilerDepVals;
				targetDependencies.push_back(compileOperation->GetDependencyValidation());

				TRY {
					serializedTarget = compileOperation->SerializeTarget(t);
					state = AssetState::Ready;

					// If we produced no artifacts, or if we produced only one and it's a "log" -- then we consider
					// this target to be invalid
					if (serializedTarget._artifacts.empty() || (serializedTarget._artifacts.size() == 1 && serializedTarget._artifacts[0]._chunkTypeCode == ChunkType_Log))
						state = AssetState::Invalid;
				} CATCH(const Exceptions::ExceptionWithDepVal& e) {
					serializedTarget._artifacts.push_back({ChunkType_Log, 0, "compiler-exception", AsBlob(e)});
					serializedTarget._depVal = e.GetDependencyValidation();
					state = AssetState::Invalid;
				} CATCH(const std::exception& e) {
					serializedTarget._artifacts.push_back({ChunkType_Log, 0, "compiler-exception", AsBlob(e)});
					state = AssetState::Invalid;
				} CATCH(...) {
					serializedTarget._artifacts.push_back({ChunkType_Log, 0, "compiler-exception", AsBlob("Unknown exception thrown from compiler")});
					state = AssetState::Invalid;
				} CATCH_END

				// additional files may have been accessed during the SerializeTarget() method -- we can incorporate their dep vals here
				if (serializedTarget._depVal)
					targetDependencies.push_back(serializedTarget._depVal);

				std::shared_ptr<IArtifactCollection> artifactCollection;

				// Write out the intermediate file that lists the products of this compile operation
				if (destinationStore) {
					bool storedInArchive = false;
					if (delegate._archiveNameDelegate) {
						auto archiveEntry = delegate._archiveNameDelegate(target._targetCode, initializers);
						if (!archiveEntry._archive.empty()) {
							destinationStore->StoreCompileProducts(
								archiveEntry._archive,
								archiveEntry._entryId,
								archiveEntry._descriptiveName,
								delegate._storeGroupId,
								MakeIteratorRange(serializedTarget._artifacts),
								state,
								MakeIteratorRange(targetDependencies));
							storedInArchive = true;
						}
					}

					if (!storedInArchive) {
						StringMeld<MaxPath> nameWithTargetCode;
						unsigned targetsWithThisCodeCount = 0;
						for (auto& t:targets) if (t._targetCode == target._targetCode) targetsWithThisCodeCount++;
						if (targetsWithThisCodeCount == 1) {
							nameWithTargetCode << initializers.ArchivableName() << "-" << std::hex << target._targetCode;
						} else
							nameWithTargetCode << initializers.ArchivableName() << "-" << target._name << "-" << std::hex << target._targetCode;
						#if defined(_DEBUG)
							auto str = nameWithTargetCode.AsString();
							// If you hit the following assert, it means that multiple targets from this compile operation would be written
							// to the same output file. That probably means that there are multiple targets with the name target code and name
							assert(compileProductNamesWritten.find(str) == compileProductNamesWritten.end());
							compileProductNamesWritten.insert(str);
						#endif
						artifactCollection = destinationStore->StoreCompileProducts(
							nameWithTargetCode.AsStringSection(),
							delegate._storeGroupId,
							MakeIteratorRange(serializedTarget._artifacts),
							state,
							MakeIteratorRange(targetDependencies));
					}
				}

				if (!artifactCollection)
					artifactCollection = std::make_shared<BlobArtifactCollection>(
						MakeIteratorRange(serializedTarget._artifacts), state,
						AsSingleDepVal(targetDependencies));

				finalCollections.push_back(std::make_pair(target._targetCode, artifactCollection));
			}
       
			promise.set_value(std::move(finalCollections));

		} CATCH(const Exceptions::ExceptionWithDepVal& e) {
			promise.set_exception(std::make_exception_ptr(Exceptions::ConstructionError(e, delegate._compilerLibraryDepVal)));
		} CATCH(const std::exception& e) {
			promise.set_exception(std::make_exception_ptr(Exceptions::ConstructionError(e, delegate._compilerLibraryDepVal)));
		} CATCH(...) {
			promise.set_exception(std::make_exception_ptr(Exceptions::ConstructionError(delegate._compilerLibraryDepVal, "unknown exception")));
		} CATCH_END
    }

	std::string IntermediateCompilers::Marker::GetCompilerDescription() const
	{
		if (auto d = _delegate.lock()) return d->_name;
		return {};
	}

    std::shared_future<ArtifactCollectionFuture::ArtifactCollectionSet> IntermediateCompilers::Marker::InvokeCompileInternal(OperationContextHelper&& opContextHelper)
    {
		std::promise<ArtifactCollectionFuture::ArtifactCollectionSet> promise;
		std::shared_future<ArtifactCollectionFuture::ArtifactCollectionSet> result = promise.get_future();

		VariantFunctions conduit;
		{
			ScopedLock(_conduitLock);
			conduit = std::move(_conduit);
		}

		if (opContextHelper)
			opContextHelper.EndWithFuture(result);

		// Unfortunately we have to copy _initializers here, because we 
		// must allow for this marker to be reused (and both InvokeCompile 
		// and GetExistingAsset use _initializers)
		ConsoleRig::GlobalServices::GetInstance().GetLongTaskThreadPool().Enqueue(
			[weakDelegate=std::weak_ptr<ExtensionAndDelegate>{_delegate}, store=_intermediateStore, inits=_initializers, conduit=std::move(conduit), opContextHelper=std::move(opContextHelper), promise=std::move(promise)]() mutable {
			auto d = weakDelegate.lock();
			if (!d) {
				promise.set_exception(std::make_exception_ptr(std::runtime_error("Request expired before it was completed")));
				return;
			}

			++d->_activeOperationCount;
			if (!d->_shuttingDown) {
				TRY {
					PerformCompile(*d, inits, std::move(conduit), std::move(opContextHelper), std::move(promise), store.get());
				} CATCH (...) {
					--d->_activeOperationCount;
					promise.set_exception(std::current_exception());
				} CATCH_END
			} else {
				promise.set_exception(std::make_exception_ptr(std::runtime_error("System shutdown before compile request was completed")));
			}
			--d->_activeOperationCount;
		});

        return result;
    }

	IntermediateCompilers::Marker::Marker(
        InitializerPack&& initializers,
        std::shared_ptr<ExtensionAndDelegate> delegate,
		RegisteredCompilerId registeredCompilerId,
		std::shared_ptr<IIntermediatesStore> intermediateStore)
    : _delegate(std::move(delegate)), _intermediateStore(std::move(intermediateStore))
	, _initializers(std::move(initializers))
	, _registeredCompilerId(registeredCompilerId)
    {
	}

	IntermediateCompilers::Marker::~Marker() {}

    std::shared_ptr<IIntermediateCompileMarker> IntermediateCompilers::Prepare(
        uint64_t targetCode, 
        InitializerPack&& initializers)
    {
		ScopedLock(_delegatesLock);
		auto initializerArchivableHash = initializers.ArchivableHash();
		uint64_t requestHashCode = HashCombine(initializerArchivableHash, targetCode);
		auto existing = _markers.find(requestHashCode);
		if (existing != _markers.end())
			return existing->second;

		std::string firstInitializer;
		for (const auto&a:_requestAssociations) {
			auto i = std::find(a.second._compileRequestCodes.begin(), a.second._compileRequestCodes.end(), targetCode);
			if (i == a.second._compileRequestCodes.end())
				continue;
			bool passMatchPattern = true;
			if (!a.second._matchPattern.empty()) {
				if (firstInitializer.empty())
					firstInitializer = initializers.GetInitializer<std::string>(0);		// first initializer is assumed to be a string
				auto asStringView = cx::make_string_view(a.second._matchPattern.data(), a.second._matchPattern.size());
				passMatchPattern = wildcards::match(firstInitializer, asStringView);
			}
			if (passMatchPattern) {
				// find the associated delegate and use that
				for (const auto&d:_delegates) {
					if (d.first != a.first) continue;
					auto result = std::make_shared<Marker>(std::move(initializers), d.second, d.first, _store);
					for (auto markerTargetCode:a.second._compileRequestCodes)
						_markers.insert(std::make_pair(HashCombine(initializerArchivableHash, markerTargetCode), result));
					return result;
				}
				return nullptr;
			}
		}

		return nullptr;
    }

	bool IntermediateCompilers::HasAssociatedCompiler(CompileRequestCode targetCode, StringSection<> firstInitializer)
	{
		ScopedLock(_delegatesLock);
		for (const auto&a:_requestAssociations) {
			auto i = std::find(a.second._compileRequestCodes.begin(), a.second._compileRequestCodes.end(), targetCode);
			if (i == a.second._compileRequestCodes.end())
				continue;
			auto asStringView = cx::make_string_view(a.second._matchPattern.data(), a.second._matchPattern.size());
			if (!a.second._matchPattern.empty() && wildcards::match(firstInitializer, asStringView))
				return true;
		}
		return false;
	}

	auto IntermediateCompilers::RegisterCompiler(
		const std::string& name,
		const std::string& shortName,
		OSServices::LibVersionDesc srcVersion,
		const DependencyValidation& compilerDepVal,
		CompileOperationDelegate&& delegate,
		ArchiveNameDelegate&& archiveNameDelegate
		) -> RegisteredCompilerId
	{
		ScopedLock(_delegatesLock);
		auto registration = std::make_shared<ExtensionAndDelegate>();
		auto result = _nextCompilerId++;
		registration->_name = name;
		registration->_srcVersion = srcVersion;
		registration->_delegate = std::move(delegate);
		registration->_archiveNameDelegate = std::move(archiveNameDelegate);
		registration->_compilerLibraryDepVal = compilerDepVal;
		if (_store)
			registration->_storeGroupId = _store->RegisterCompileProductsGroup(MakeStringSection(shortName), srcVersion, !!registration->_archiveNameDelegate);
		_delegates.push_back(std::make_pair(result, std::move(registration)));
		return result;
	}

	auto IntermediateCompilers::RegisterCompiler(
		const std::string& name,
		const std::string& shortName,
		OSServices::LibVersionDesc srcVersion,
		const DependencyValidation& compilerDepVal,
		CompileOperationDelegate2&& delegate,
		ArchiveNameDelegate&& archiveNameDelegate
		) -> RegisteredCompilerId
	{
		ScopedLock(_delegatesLock);
		auto registration = std::make_shared<ExtensionAndDelegate>();
		auto result = _nextCompilerId++;
		registration->_name = name;
		registration->_srcVersion = srcVersion;
		registration->_delegateWithConduit = std::move(delegate);
		registration->_archiveNameDelegate = std::move(archiveNameDelegate);
		registration->_compilerLibraryDepVal = compilerDepVal;
		if (_store)
			registration->_storeGroupId = _store->RegisterCompileProductsGroup(MakeStringSection(shortName), srcVersion, !!registration->_archiveNameDelegate);
		_delegates.push_back(std::make_pair(result, std::move(registration)));
		return result;
	}

	void IntermediateCompilers::DeregisterCompiler(RegisteredCompilerId id)
	{
		std::shared_ptr<ExtensionAndDelegate> del;
		{
			ScopedLock(_delegatesLock);
			_extensionsAndDelegatesMap.erase(id);
			_requestAssociations.erase(id);
			for (auto i=_markers.begin(); i!=_markers.end();)
				if (i->second->GetRegisteredCompilerId() == id) {
					i = _markers.erase(i);
				} else ++i;

			for (auto i=_delegates.begin(); i!=_delegates.end();)
				if (i->first == id) {
					i->second->_shuttingDown.store(true);
					del = std::move(i->second);
					i = _delegates.erase(i);
				} else ++i;
		}

		if (del) {
			while (del->_activeOperationCount.load() != 0) {
				bool completed = ConsoleRig::GlobalServices::GetInstance().GetLongTaskThreadPool().StallAndDrainQueue(std::chrono::milliseconds(100));
				if (completed) break;
			}
		}
	}

	void IntermediateCompilers::AssociateRequest(
		RegisteredCompilerId compiler,
		IteratorRange<const uint64_t*> outputAssetTypes,
		const std::string& matchPattern)
	{
		ScopedLock(_delegatesLock);
		DelegateAssociation association;
		association._compileRequestCodes = std::vector<uint64_t>{ outputAssetTypes.begin(), outputAssetTypes.end() };
		association._matchPattern = matchPattern;
		_requestAssociations.insert(std::make_pair(compiler, association));
	}

	std::vector<std::pair<std::string, std::string>> IntermediateCompilers::GetExtensionsForTargetCode(CompileRequestCode targetCode)
	{
		std::vector<std::pair<std::string, std::string>> ext;

		ScopedLock(_delegatesLock);
		for (const auto&d:_delegates) {
			auto a = _requestAssociations.equal_range(d.first);
			for (auto association=a.first; association != a.second; ++association) {
				if (std::find(association->second._compileRequestCodes.begin(), association->second._compileRequestCodes.end(), targetCode) != association->second._compileRequestCodes.end()) {
					// This compiler can make this type. Let's check what extensions have been registered
					auto r = _extensionsAndDelegatesMap.equal_range(d.first);
					for (auto i=r.first; i!=r.second; ++i)
						ext.push_back({i->second, d.second->_name});
				}
			}
		}
		return ext;
	}

	std::vector<uint64_t> IntermediateCompilers::GetTargetCodesForExtension(StringSection<> extension)
	{
		std::vector<uint64_t> result;

		ScopedLock(_delegatesLock);
		for (const auto&d:_extensionsAndDelegatesMap) {
			if (!XlEqStringI(extension, d.second)) continue;		// case insensitive comparison, for convention's sake

			auto a = _requestAssociations.equal_range(d.first);
			for (auto association=a.first; association != a.second; ++association)
				for (auto targetCode:association->second._compileRequestCodes)
					if (std::find(result.begin(), result.end(), targetCode) == result.end())
						result.push_back(targetCode);
		}

		return result;
	}

	void IntermediateCompilers::AssociateExtensions(
		RegisteredCompilerId associatedCompiler,
		const std::string& commaSeparatedExtensions)
	{
		ScopedLock(_delegatesLock);
		auto i = commaSeparatedExtensions.begin();
		for (;;) {
			while (i != commaSeparatedExtensions.end() && (*i == ' ' || *i == '\t' || *i == ',')) ++i;
			if (i == commaSeparatedExtensions.end())
				break;

			if (*i == '.') ++i;		// if the token begins with a '.', we should just skip over and ignore it

			auto tokenBegin = i;
			auto lastNonWhitespace = commaSeparatedExtensions.end();	// set to a sentinel, to distinquish never-set from set-to-first
			while (i != commaSeparatedExtensions.end() && *i != ',') {
				if (*i != ' ' && *i != '\t') lastNonWhitespace = i;
				++i;
			}
			if (lastNonWhitespace != commaSeparatedExtensions.end()) {
				_extensionsAndDelegatesMap.insert({associatedCompiler, std::string{tokenBegin, lastNonWhitespace+1}});
			}
		}
	}

    void IntermediateCompilers::StallOnPendingOperations(bool cancelAll)
    {
		// todo -- must reimplement, because compilation operations now occur on the main thread pool, rather than a custom thread
		assert(0);
    }

	void IntermediateCompilers::FlushCachedMarkers()
	{
		ScopedLock(_delegatesLock);
		_markers.clear();
	}

	IntermediateCompilers::IntermediateCompilers(std::shared_ptr<IIntermediatesStore> store)
	: _store(std::move(store))
	{ 
	}
	IntermediateCompilers::~IntermediateCompilers() {}

	IIntermediateCompileMarker::~IIntermediateCompileMarker() {}

	std::shared_ptr<IIntermediateCompilers> CreateIntermediateCompilers(std::shared_ptr<IIntermediatesStore> store)
	{
		return std::make_shared<IntermediateCompilers>(std::move(store));
	}

///////////////////////////////////////////////////////////////////////////////////////////////////

	class CompilerLibrary
	{
	public:
		CreateCompileOperationFn* _createCompileOpFunction;
		std::shared_ptr<OSServices::AttachableLibrary> _library;

		struct Kind 
		{ 
			std::vector<uint64_t> _targetCodes; 
			std::string _identifierFilter; 
			std::string _name;
			std::string _shortName;
			std::string _extensionsForOpenDlg;
		};
		std::vector<Kind> _kinds;

		CompilerLibrary(StringSection<> libraryName);
	};

	CompilerLibrary::CompilerLibrary(StringSection<> libraryName)
	{
		auto& pluginSet = ConsoleRig::GlobalServices::GetInstance().GetPluginSet();
		_library = pluginSet.LoadLibrary(libraryName.AsString());

		_createCompileOpFunction = _library->GetFunction<decltype(_createCompileOpFunction)>("CreateCompileOperation");

		auto compilerDescFn = _library->GetFunction<GetCompilerDescFn*>("GetCompilerDesc");
		if (compilerDescFn) {
			auto compilerDesc = (*compilerDescFn)();
			auto targetCount = compilerDesc->FileKindCount();
			for (unsigned c=0; c<targetCount; ++c) {
				auto kind = compilerDesc->GetFileKind(c);
				_kinds.push_back({
					std::vector<uint64_t>{kind._targetCodes.begin(), kind._targetCodes.end()},
					kind._regexFilter,
					kind._name,
					kind._shortName,
					kind._extensionsForOpenDlg});
			}
		}

		// check for problems (missing functions or bad version number)
		if (!_createCompileOpFunction)
			Throw(::Exceptions::BasicLabel("Error while linking asset conversion DLL. Some interface functions are missing. From DLL: (%s)", libraryName.AsString().c_str()));
	}

	DirectorySearchRules DefaultLibrarySearchDirectories()
	{
		DirectorySearchRules result;
		// Default search path for libraries is just the process path.
		// In some cases (eg, for unit tests where the process path points to an internal visual studio path), 
		// we have to include extra paths
		char processPath[MaxPath];
		OSServices::GetProcessPath((utf8*)processPath, dimof(processPath));
		result.AddSearchDirectory(
			MakeFileNameSplitter(processPath).StemAndPath());
		
		char appDir[MaxPath];
    	OSServices::GetCurrentDirectory(dimof(appDir), appDir);
		result.AddSearchDirectory(appDir);
		return result;
	}

	std::vector<CompilerRegistration> DiscoverCompileOperations(
		IIntermediateCompilers& compilerManager,
		StringSection<> librarySearch,
		const DirectorySearchRules& searchRules)
	{
		std::vector<CompilerRegistration> result;

#if XLE_ATTACHABLE_LIBRARIES_ENABLE
		auto candidateCompilers = searchRules.FindFiles(librarySearch);
		for (auto& c : candidateCompilers) {
			TRY {
				CompilerLibrary library{c};

				OSServices::LibVersionDesc srcVersion;
				if (!library._library->TryGetVersion(srcVersion))
					Throw(std::runtime_error("Querying version returned an error"));

				auto compilerFn = MakeStringSection(c);
				auto compilerDepVal = GetDepValSys().Make(compilerFn);

				std::vector<CompilerRegistration> opsFromThisLibrary;
				auto lib = library._library;
				auto fn = library._createCompileOpFunction;
				for (const auto&kind:library._kinds) {
					CompilerRegistration registration(
						compilerManager,
						kind._name + " (" + MakeSplitPath(c).Simplify().Rebuild() + ")",
						kind._shortName,
						srcVersion,
						compilerDepVal,
						[lib, fn](auto initializers) {
							(void)lib; // hold strong reference to the library, so the DLL doesn't get unloaded
							return (*fn)(initializers);
						});

					compilerManager.AssociateRequest(
						registration.RegistrationId(),
						MakeIteratorRange(kind._targetCodes),
						kind._identifierFilter);
					if (!kind._extensionsForOpenDlg.empty())
						compilerManager.AssociateExtensions(registration.RegistrationId(), kind._extensionsForOpenDlg);
					opsFromThisLibrary.push_back(std::move(registration));
				}

				result.reserve(result.size() + opsFromThisLibrary.size());
				for (auto& op:opsFromThisLibrary) result.push_back(std::move(op));
			} CATCH (const std::exception& e) {
				Log(Warning) << "Failed while attempt to attach library: " << e.what() << std::endl;
			} CATCH_END
		}
#endif

		return result;
	}

	CompilerRegistration::CompilerRegistration(
		IIntermediateCompilers& compilers,
		const std::string& name,
		const std::string& shortName,
		OSServices::LibVersionDesc srcVersion,
		const DependencyValidation& compilerDepVal,
		IIntermediateCompilers::CompileOperationDelegate&& delegate,
		IIntermediateCompilers::ArchiveNameDelegate&& archiveNameDelegate)
	: _compilers(&compilers)
	{
		_registration = compilers.RegisterCompiler(name, shortName, srcVersion, compilerDepVal, std::move(delegate), std::move(archiveNameDelegate));
	}
	CompilerRegistration::CompilerRegistration(
		IIntermediateCompilers& compilers,
		const std::string& name,
		const std::string& shortName,
		OSServices::LibVersionDesc srcVersion,
		const DependencyValidation& compilerDepVal,
		IIntermediateCompilers::CompileOperationDelegate2&& delegate,
		IIntermediateCompilers::ArchiveNameDelegate&& archiveNameDelegate)
	: _compilers(&compilers)
	{
		_registration = compilers.RegisterCompiler(name, shortName, srcVersion, compilerDepVal, std::move(delegate), std::move(archiveNameDelegate));
	}
	CompilerRegistration::CompilerRegistration()
	: _compilers(nullptr)
	, _registration(~0u)
	{}
	CompilerRegistration::~CompilerRegistration()
	{
		if (_compilers && _registration != ~0u)
			_compilers->DeregisterCompiler(_registration);
	}
	CompilerRegistration::CompilerRegistration(CompilerRegistration&& moveFrom)
	: _compilers(moveFrom._compilers)
	, _registration(moveFrom._registration)
	{
		moveFrom._compilers = nullptr;
		moveFrom._registration = ~0u;
	}
	CompilerRegistration& CompilerRegistration::operator=(CompilerRegistration&& moveFrom)
	{
		if (this == &moveFrom) return *this;
		if (_compilers && _registration != ~0u)
			_compilers->DeregisterCompiler(_registration);
		_compilers = moveFrom._compilers;
		_registration = moveFrom._registration;
		moveFrom._compilers = nullptr;
		moveFrom._registration = ~0u;
		return *this;
	}

	ICompileOperation::~ICompileOperation() {}
	ICompilerDesc::~ICompilerDesc() {}

	class SimpleCompilerAdapter : public ICompileOperation
	{
	public:
		std::vector<TargetDesc> GetTargets() const override
		{
			if (_serializedArtifacts.empty()) return {};
			return {
				TargetDesc { _targetCode, _serializedArtifacts[0]._name.c_str() }
			};
		}
		::Assets::SerializedTarget	SerializeTarget(unsigned idx) override
		{
			assert(idx == 0);
			return { _serializedArtifacts, _depVal };
		}
		DependencyValidation GetDependencyValidation() const override { return _depVal; }

		SimpleCompilerAdapter(SimpleCompilerResult&& compilerResult)
		: _serializedArtifacts(std::move(compilerResult._artifacts))
		, _depVal(std::move(compilerResult._depVal))
		, _targetCode(compilerResult._targetCode)
		{}

	private:
		std::vector<SerializedArtifact> _serializedArtifacts;
		DependencyValidation _depVal;
		ArtifactTargetCode _targetCode;
	};

	CompilerRegistration RegisterSimpleCompiler(
		IIntermediateCompilers& compilers,
		const std::string& name,
		const std::string& shortName,
		std::function<SimpleCompilerSig>&& fn,
		IIntermediateCompilers::ArchiveNameDelegate&& archiveNameDelegate)
	{
		return CompilerRegistration{
			compilers,
			name, shortName,
			ConsoleRig::GetLibVersionDesc(),
			{},
			[fn=std::move(fn)](const auto& initializers) {
				return std::make_shared<SimpleCompilerAdapter>(fn(initializers));
			}};
	}

}

