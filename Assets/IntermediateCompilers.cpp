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
#include "../ConsoleRig/AttachableLibrary.h"
#include "../ConsoleRig/GlobalServices.h"
#include "../ConsoleRig/Plugins.h"
#include "../OSServices/Log.h"
#include "../OSServices/RawFS.h"		// for OSServices::GetProcessPath()
#include "../Utility/Threading/Mutex.h"
#include "../Utility/Streams/PathUtils.h"
#include "../Utility/StringFormat.h"
#include "../Utility/Threading/CompletionThreadPool.h"
#include <regex>
#include <set>
#include <unordered_map>
#include <atomic>

namespace Assets 
{
	static const auto ChunkType_Log = ConstHash64<'Log'>::Value;

	struct ExtensionAndDelegate
	{
		std::string _name;
		ConsoleRig::LibVersionDesc _srcVersion = {nullptr, nullptr};
		IIntermediateCompilers::CompileOperationDelegate _delegate;
		IIntermediateCompilers::ArchiveNameDelegate _archiveNameDelegate;
		DependencyValidation _compilerLibraryDepVal;
		IntermediatesStore::CompileProductsGroupId _storeGroupId = 0;
		std::atomic<bool> _shuttingDown = false;
		std::atomic<unsigned> _activeOperationCount = 0;
	};

	struct DelegateAssociation
	{
		std::vector<uint64_t> _targetCodes;
		std::regex _regexFilter;
	};

///////////////////////////////////////////////////////////////////////////////////////////////////

	static DependencyValidation MakeDepVal(
		IteratorRange<const DependentFileState*> deps,
		const DependencyValidation& compilerDepVal)
	{
		auto result = GetDepValSys().Make(deps);
		if (compilerDepVal)
			result.RegisterDependency(compilerDepVal);
		return result;
	}

	class IntermediateCompilers : public IIntermediateCompilers
    {
    public:
        virtual std::shared_ptr<IIntermediateCompileMarker> Prepare(TargetCode, InitializerPack&&) override;
        virtual void StallOnPendingOperations(bool cancelAll) override;
		
		virtual CompilerRegistration RegisterCompiler(
			const std::string& name,
			const std::string& shortName,
			ConsoleRig::LibVersionDesc srcVersion,
			const DependencyValidation& compilerDepVal,
			CompileOperationDelegate&& delegate,
			ArchiveNameDelegate&& archiveNameDelegate
			) override;

		virtual void DeregisterCompiler(RegisteredCompilerId id) override;

		virtual void AssociateRequest(
			RegisteredCompilerId compiler,
			IteratorRange<const uint64_t*> outputAssetTypes,
			const std::string& initializerRegexFilter
			) override;

		virtual void AssociateExtensions(RegisteredCompilerId associatedCompiler, const std::string& commaSeparatedExtensions) override;
		virtual std::vector<std::pair<std::string, std::string>> GetExtensionsForTargetCode(TargetCode typeCode) override;
		virtual std::vector<uint64_t> GetTargetCodesForExtension(StringSection<>) override;

		virtual void FlushCachedMarkers() override;

		IntermediateCompilers(
			const std::shared_ptr<IntermediatesStore>& store);
        ~IntermediateCompilers();
    protected:
		class Marker;

        Threading::Mutex _delegatesLock;
		std::vector<std::pair<RegisteredCompilerId, std::shared_ptr<ExtensionAndDelegate>>> _delegates;
		std::unordered_multimap<RegisteredCompilerId, std::string> _extensionsAndDelegatesMap;
		std::unordered_multimap<RegisteredCompilerId, DelegateAssociation> _requestAssociations;
		std::unordered_map<uint64_t, std::shared_ptr<Marker>> _markers;
		std::shared_ptr<IntermediatesStore> _store;
		RegisteredCompilerId _nextCompilerId = 1;
    };

    class IntermediateCompilers::Marker : public IIntermediateCompileMarker
    {
    public:
		using IdentifiersList = IteratorRange<const StringSection<>*>;
        std::shared_ptr<IArtifactCollection> GetExistingAsset(TargetCode) const override;
        std::shared_ptr<ArtifactCollectionFuture> InvokeCompile() override;
		RegisteredCompilerId GetRegisteredCompilerId() { return _registeredCompilerId; }
		void StallForActiveFuture();

        Marker(
            InitializerPack&& requestName,
            std::shared_ptr<ExtensionAndDelegate> delegate,
			RegisteredCompilerId registeredCompilerId,
			std::shared_ptr<IntermediatesStore> intermediateStore);
        ~Marker();
    private:
		std::weak_ptr<ArtifactCollectionFuture> _activeFuture;
        std::weak_ptr<ExtensionAndDelegate> _delegate;
		std::shared_ptr<IntermediatesStore> _intermediateStore;
        InitializerPack _initializers;
		RegisteredCompilerId _registeredCompilerId;

		static void PerformCompile(
			const ExtensionAndDelegate& delegate,
			const InitializerPack& initializers,
			ArtifactCollectionFuture& compileMarker,
			IntermediatesStore* destinationStore);
    };

    std::shared_ptr<IArtifactCollection> IntermediateCompilers::Marker::GetExistingAsset(TargetCode targetCode) const
    {
        if (!_intermediateStore) return nullptr;

		auto d = _delegate.lock();
		if (!d)
			return nullptr;

		if (d->_archiveNameDelegate) {
			auto archiveEntry = d->_archiveNameDelegate(targetCode, _initializers);
			if (!archiveEntry._archive.empty())
				return _intermediateStore->RetrieveCompileProducts(archiveEntry._archive, archiveEntry._entryId, d->_storeGroupId);
		}

		StringMeld<MaxPath> nameWithTargetCode;
		nameWithTargetCode << _initializers.ArchivableName() << "-" << std::hex << targetCode;
		return _intermediateStore->RetrieveCompileProducts(nameWithTargetCode.AsStringSection(), d->_storeGroupId);
    }

	void IntermediateCompilers::Marker::PerformCompile(
		const ExtensionAndDelegate& delegate,
		const InitializerPack& initializers, 
		ArtifactCollectionFuture& compileMarker,
		IntermediatesStore* destinationStore)
    {
		std::vector<DependentFileState> deps;
		assert(!initializers.IsEmpty());

		auto firstInitializer = initializers.GetInitializer<std::string>(0);		// first initializer is assumed to be a string

        TRY
        {
            auto model = delegate._delegate(initializers);
			if (!model)
				Throw(std::runtime_error("Compiler library returned null to compile request on " + initializers.ArchivableName()));

			deps = model->GetDependencies();
			std::vector<std::pair<TargetCode, std::shared_ptr<IArtifactCollection>>> finalCollections;

			// ICompileOperations can have multiple "targets", and then those targets can have multiple
			// chunks within them. Each target should generally maps onto a single "asset" 
			// (with separate "state" values, etc). But an asset can be constructed from multiple chunks

			// note that there's a problem here -- if we've compiled a particular operation and it produced
			// a specific target; and then later on we compile again but this time the operation does not
			// produce that same output target, then the target remains in the cache and will not be removed

			auto targets = model->GetTargets();
			finalCollections.reserve(targets.size());
			for (unsigned t=0; t<targets.size(); ++t) {
				const auto& target = targets[t];

				std::vector<ICompileOperation::SerializedArtifact> serializedArtifacts;
				AssetState state = AssetState::Pending;

				TRY {
					serializedArtifacts = model->SerializeTarget(t);
					state = AssetState::Ready;

					// If we produced no artifacts, or if we produced only one and it's a "log" -- then we consider
					// this target to be invalid
					if (serializedArtifacts.empty() || (serializedArtifacts.size() == 1 && serializedArtifacts[0]._chunkTypeCode == ChunkType_Log))
						state = AssetState::Invalid;
				} CATCH(const std::exception& e) {
					serializedArtifacts.push_back({ChunkType_Log, 0, "compiler-exception", AsBlob(e)});
					state = AssetState::Invalid;
				} CATCH(...) {
					serializedArtifacts.push_back({ChunkType_Log, 0, "compiler-exception", AsBlob("Unknown exception thrown from compiler")});
					state = AssetState::Invalid;
				} CATCH_END

				auto artifactCollection = std::make_shared<BlobArtifactCollection>(
					MakeIteratorRange(serializedArtifacts), state,
					::Assets::MakeDepVal(MakeIteratorRange(deps), delegate._compilerLibraryDepVal));

				finalCollections.push_back(std::make_pair(target._targetCode, artifactCollection));

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
								MakeIteratorRange(serializedArtifacts),
								state,
								MakeIteratorRange(deps));
							storedInArchive = true;
						}
					}

					if (!storedInArchive) {
						StringMeld<MaxPath> nameWithTargetCode;
						nameWithTargetCode << initializers.ArchivableName() << "-" << std::hex << target._targetCode;
						destinationStore->StoreCompileProducts(
							nameWithTargetCode.AsStringSection(),
							delegate._storeGroupId,
							MakeIteratorRange(serializedArtifacts),
							state,
							MakeIteratorRange(deps));
					}
				}
			}
       
			compileMarker.SetArtifactCollections(MakeIteratorRange(finalCollections));

		} CATCH(const Exceptions::ConstructionError& e) {
			auto depVal = MakeDepVal(MakeIteratorRange(deps), delegate._compilerLibraryDepVal);
			if (deps.empty())
				depVal.RegisterDependency(MakeFileNameSplitter(firstInitializer).AllExceptParameters());		// fallback case -- compiler might have failed because of bad input file. Interpret the initializer as a filename and create a dep val for it
			Throw(Exceptions::ConstructionError(e, depVal));
		} CATCH(const std::exception& e) {
			auto depVal = MakeDepVal(MakeIteratorRange(deps), delegate._compilerLibraryDepVal);
			if (deps.empty())
				depVal.RegisterDependency(MakeFileNameSplitter(firstInitializer).AllExceptParameters());
			Throw(Exceptions::ConstructionError(e, depVal));
		} CATCH(...) {
			auto depVal = MakeDepVal(MakeIteratorRange(deps), delegate._compilerLibraryDepVal);
			if (deps.empty())
				depVal.RegisterDependency(MakeFileNameSplitter(firstInitializer).AllExceptParameters());
			Throw(Exceptions::ConstructionError(Exceptions::ConstructionError::Reason::Unknown, depVal, "%s", "unknown exception"));
		} CATCH_END
    }

	static void QueueCompileOperation(
		const std::shared_ptr<::Assets::ArtifactCollectionFuture>& future,
		std::function<void(::Assets::ArtifactCollectionFuture&)>&& operation)
	{
        if (!ConsoleRig::GlobalServices::GetInstance().GetLongTaskThreadPool().IsGood()) {
            operation(*future);
            return;
        }

		auto fn = std::move(operation);
		ConsoleRig::GlobalServices::GetInstance().GetLongTaskThreadPool().EnqueueBasic(
			[future, fn]() {
				TRY
				{
					fn(*future);
				}
				CATCH(...)
				{
					future->StoreException(std::current_exception());
				}
				CATCH_END
				assert(future->GetAssetState() != ::Assets::AssetState::Pending);	// if it is still marked "pending" at this stage, it will never change state
		});
	}

    std::shared_ptr<ArtifactCollectionFuture> IntermediateCompilers::Marker::InvokeCompile()
    {
		auto activeFuture = _activeFuture.lock();
		if (activeFuture)
			return activeFuture;

        auto backgroundOp = std::make_shared<ArtifactCollectionFuture>();
        backgroundOp->SetDebugLabel(_initializers.ArchivableName());

		const bool allowBackgroundOps = true;
		if (allowBackgroundOps) {
			// Unfortunately we have to copy _initializers here, because we 
			// must allow for this marker to be reused (and both InvokeCompile 
			// and GetExistingAsset use _initializers)
			std::weak_ptr<ExtensionAndDelegate> weakDelegate = _delegate;
			std::shared_ptr<IntermediatesStore> store = _intermediateStore;
			QueueCompileOperation(
				backgroundOp,
				[weakDelegate, store, inits=_initializers](ArtifactCollectionFuture& op) {
				auto d = weakDelegate.lock();
				if (!d) {
					op.SetState(AssetState::Invalid);
					return;
				}

				++d->_activeOperationCount;
				if (!d->_shuttingDown) {
					TRY {
						PerformCompile(*d, inits, op, store.get());
					} CATCH (...) {
						--d->_activeOperationCount;
						throw;
					} CATCH_END
				} else {
					op.SetState(AssetState::Invalid);
				}
				--d->_activeOperationCount;
			});
		} else {
			auto d = _delegate.lock();
			if (!d) {
				backgroundOp->SetState(AssetState::Invalid);
			} else {
				++d->_activeOperationCount;
				if (!d->_shuttingDown) {
					TRY {
						PerformCompile(*d, _initializers, *backgroundOp, _intermediateStore.get());
					} CATCH (...) {
						--d->_activeOperationCount;
						throw;
					} CATCH_END
				} else {
					backgroundOp->SetState(AssetState::Invalid);
				}
				--d->_activeOperationCount;
			}
		}
        
		_activeFuture = backgroundOp;
        return std::move(backgroundOp);
    }

	IntermediateCompilers::Marker::Marker(
        InitializerPack&& initializers,
        std::shared_ptr<ExtensionAndDelegate> delegate,
		RegisteredCompilerId registeredCompilerId,
		std::shared_ptr<IntermediatesStore> intermediateStore)
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

		auto firstInitializer = initializers.GetInitializer<std::string>(0);		// first initializer is assumed to be a string

		for (const auto&a:_requestAssociations) {
			auto i = std::find(a.second._targetCodes.begin(), a.second._targetCodes.end(), targetCode);
			if (i == a.second._targetCodes.end())
				continue;
			if (std::regex_match(firstInitializer.begin(), firstInitializer.end(), a.second._regexFilter)) {
				// find the associated delegate and use that
				for (const auto&d:_delegates) {
					if (d.first != a.first) continue;
					auto result = std::make_shared<Marker>(std::move(initializers), d.second, d.first, _store);
					for (auto markerTargetCode:a.second._targetCodes)
						_markers.insert(std::make_pair(HashCombine(initializerArchivableHash, markerTargetCode), result));
					return result;
				}
				return nullptr;
			}
		}

		return nullptr;
    }

	auto IntermediateCompilers::RegisterCompiler(
		const std::string& name,
		const std::string& shortName,
		ConsoleRig::LibVersionDesc srcVersion,
		const DependencyValidation& compilerDepVal,
		CompileOperationDelegate&& delegate,
		ArchiveNameDelegate&& archiveNameDelegate
		) -> CompilerRegistration
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
		return { result };
	}

	void IntermediateCompilers::DeregisterCompiler(RegisteredCompilerId id)
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
				while (i->second->_activeOperationCount.load() != 0) {
					bool completed = ConsoleRig::GlobalServices::GetInstance().GetLongTaskThreadPool().StallAndDrainQueue(std::chrono::milliseconds(100));
					if (completed) break;
				}
				i = _delegates.erase(i);
			} else ++i;
	}

	void IntermediateCompilers::AssociateRequest(
		RegisteredCompilerId compiler,
		IteratorRange<const uint64_t*> outputAssetTypes,
		const std::string& initializerRegexFilter)
	{
		ScopedLock(_delegatesLock);
		DelegateAssociation association;
		association._targetCodes = std::vector<uint64_t>{ outputAssetTypes.begin(), outputAssetTypes.end() };
		if (!initializerRegexFilter.empty())
			association._regexFilter = std::regex{initializerRegexFilter, std::regex_constants::icase};
		_requestAssociations.insert(std::make_pair(compiler, association));
	}

	std::vector<std::pair<std::string, std::string>> IntermediateCompilers::GetExtensionsForTargetCode(TargetCode targetCode)
	{
		std::vector<std::pair<std::string, std::string>> ext;

		ScopedLock(_delegatesLock);
		for (const auto&d:_delegates) {
			auto a = _requestAssociations.equal_range(d.first);
			for (auto association=a.first; association != a.second; ++association) {
				if (std::find(association->second._targetCodes.begin(), association->second._targetCodes.end(), targetCode) != association->second._targetCodes.end()) {
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
			if (!XlEqString(extension, d.second)) continue;

			auto a = _requestAssociations.equal_range(d.first);
			for (auto association=a.first; association != a.second; ++association)
				for (auto targetCode:association->second._targetCodes)
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

	IntermediateCompilers::IntermediateCompilers(
		const std::shared_ptr<IntermediatesStore>& store)
	: _store(store)
	{ 
	}
	IntermediateCompilers::~IntermediateCompilers() {}

	IIntermediateCompileMarker::~IIntermediateCompileMarker() {}

	std::shared_ptr<IIntermediateCompilers> CreateIntermediateCompilers(const std::shared_ptr<IntermediatesStore>& store)
	{
		return std::make_shared<IntermediateCompilers>(store);
	}

///////////////////////////////////////////////////////////////////////////////////////////////////

	class CompilerLibrary
	{
	public:
		CreateCompileOperationFn* _createCompileOpFunction;
		std::shared_ptr<ConsoleRig::AttachableLibrary> _library;

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
			MakeFileNameSplitter(processPath).DriveAndPath());
		
		char appDir[MaxPath];
    	OSServices::GetCurrentDirectory(dimof(appDir), appDir);
		result.AddSearchDirectory(appDir);
		return result;
	}

	std::vector<IntermediateCompilers::RegisteredCompilerId> DiscoverCompileOperations(
		IIntermediateCompilers& compilerManager,
		StringSection<> librarySearch,
		const DirectorySearchRules& searchRules)
	{
		std::vector<IntermediateCompilers::RegisteredCompilerId> result;

		auto candidateCompilers = searchRules.FindFiles(librarySearch);
		for (auto& c : candidateCompilers) {
			TRY {
				CompilerLibrary library{c};

				ConsoleRig::LibVersionDesc srcVersion;
				if (!library._library->TryGetVersion(srcVersion))
					Throw(std::runtime_error("Querying version returned an error"));

				std::vector<IntermediateCompilers::RegisteredCompilerId> opsFromThisLibrary;
				auto lib = library._library;
				auto fn = library._createCompileOpFunction;
				for (const auto&kind:library._kinds) {
					auto compilerFn = MakeStringSection(c);
					auto compilerDepVal = GetDepValSys().Make(compilerFn);
					auto registrationId = compilerManager.RegisterCompiler(
						kind._name + " (" + MakeSplitPath(c).Simplify().Rebuild() + ")",
						kind._shortName,
						srcVersion,
						compilerDepVal,
						[lib, fn](auto initializers) {
							(void)lib; // hold strong reference to the library, so the DLL doesn't get unloaded
							return (*fn)(initializers);
						});

					compilerManager.AssociateRequest(
						registrationId._registrationId,
						MakeIteratorRange(kind._targetCodes),
						kind._identifierFilter);
					if (!kind._extensionsForOpenDlg.empty())
						compilerManager.AssociateExtensions(registrationId._registrationId, kind._extensionsForOpenDlg);
					opsFromThisLibrary.push_back(registrationId._registrationId);
				}

				result.insert(result.end(), opsFromThisLibrary.begin(), opsFromThisLibrary.end());
			} CATCH (const std::exception& e) {
				Log(Warning) << "Failed while attempt to attach library: " << e.what() << std::endl;
			} CATCH_END
		}

		return result;
	}

	ICompileOperation::~ICompileOperation() {}
	ICompilerDesc::~ICompilerDesc() {}

}

