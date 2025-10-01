// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "IntermediatesStore.h"
#include "LooseFilesCache.h"
#include "IArtifact.h"
#include "IFileSystem.h"
#include "DepVal.h"
#include "AssetUtils.h"
#include "ArchiveCache.h"
#include "../OSServices/Log.h"
#include "../OSServices/RawFS.h"
#include "../ConsoleRig/GlobalServices.h"
#include "../Utility/Streams/SerializationUtils.h"
#include "../Utility/Streams/PathUtils.h"
#include "../Formatters/StreamDOM.h"
#include "../Utility/Threading/Mutex.h"
#include "../Utility/IteratorUtils.h"
#include "../Utility/PtrUtils.h"
#include "../Utility/StreamUtils.h"
#include "../Utility/MemoryUtils.h"
#include "../Utility/StringFormat.h"
#include "../Utility/FunctionUtils.h"
#include "../Utility/FastParseValue.h"
#include <filesystem>
#include <memory>
#include <unordered_map>

namespace Assets
{
	class IntermediatesStoreBase : public IIntermediatesStore
	{
	public:
		// in very occasional cases, IFileSystem implementations may use IntermediatesStore during another IntermediatesStore operation
		std::shared_timed_mutex _lock;

		struct ConstructorOptions
		{
			::Assets::rstring _baseDir;
			::Assets::rstring _versionString;
			::Assets::rstring _configString;
		};
		ConstructorOptions _constructorOptions;

		struct Group
		{
			std::shared_ptr<LooseFilesStorage> _looseFilesStorage;
			std::shared_ptr<ArchiveCacheSet> _archiveCacheSet;
			std::string _archiveCacheBase;
			int _refCount = 1;
		};

		std::unordered_map<uint64_t, Group> _groups;

		std::shared_ptr<StoreReferenceCounts> _storeRefCounts;

		std::shared_ptr<IFileSystem> _filesystem;
		bool _allowStore = false;
		bool _checkDepVals = false;

		uint64_t MakeHashCode(
			StringSection<> archivableName,
			CompileProductsGroupId groupId) const;
		uint64_t MakeHashCode(
			StringSection<> archiveName,
			ArchiveEntryId entryId,
			CompileProductsGroupId groupId) const;

		std::shared_ptr<IArtifactCollection> StoreCompileProducts(
			StringSection<> archivableName,
			CompileProductsGroupId groupId,
			IteratorRange<const SerializedArtifact*> artifacts,
			::Assets::AssetState state,
			IteratorRange<const DependencyValidation*> dependencies) override;

		std::shared_ptr<IArtifactCollection> RetrieveCompileProducts(
			StringSection<> archivableName,
			CompileProductsGroupId groupId) override;

		void StoreCompileProducts(
			StringSection<> archiveName,
			ArchiveEntryId entryId,
			StringSection<> entryDescriptiveName,
			CompileProductsGroupId groupId,
			IteratorRange<const SerializedArtifact*> artifacts,
			::Assets::AssetState state,
			IteratorRange<const DependencyValidation*> dependencies) override;

		std::shared_ptr<IArtifactCollection> RetrieveCompileProducts(
			StringSection<> archiveName,
			ArchiveEntryId entryId,
			CompileProductsGroupId groupId) override;

		bool AllowStore() override;
		void FlushToDisk() override;

		struct ReadRefCountLock
		{
			ReadRefCountLock(IntermediatesStoreBase* pimpl, uint64_t hashCode, StringSection<> descriptiveName)
			: _pimpl(pimpl), _hashCode(hashCode)
			{
				ScopedLock(_pimpl->_storeRefCounts->_lock);
				auto existing = _pimpl->_storeRefCounts->_storeOperationsInFlight.find(_hashCode);
				if (existing != _pimpl->_storeRefCounts->_storeOperationsInFlight.end())
					Throw(std::runtime_error("Attempting to retrieve compile products while store in flight: " + descriptiveName.AsString()));
				auto read = LowerBound(_pimpl->_storeRefCounts->_readReferenceCount, _hashCode);
				if (read != _pimpl->_storeRefCounts->_readReferenceCount.end() && read->first == _hashCode) {
					++read->second;
				} else
					_pimpl->_storeRefCounts->_readReferenceCount.insert(read, std::make_pair(_hashCode, 1));
			}
			~ReadRefCountLock()
			{
				ScopedLock(_pimpl->_storeRefCounts->_lock);
				auto read = LowerBound(_pimpl->_storeRefCounts->_readReferenceCount, _hashCode);
				if (read != _pimpl->_storeRefCounts->_readReferenceCount.end() && read->first == _hashCode) {
					assert(read->second > 0);
					--read->second;
				} else {
					Log(Error) << "Missing _readReferenceCount marker during cleanup op in RetrieveCompileProducts" << std::endl;
				}
			}
			ReadRefCountLock(const ReadRefCountLock&) = delete;
			ReadRefCountLock& operator=(const ReadRefCountLock&) = delete;
		private:
			IntermediatesStoreBase* _pimpl;
			uint64_t _hashCode;
		};

		struct WriteRefCountLock
		{
			WriteRefCountLock(IntermediatesStoreBase* pimpl, uint64_t hashCode, StringSection<> descriptiveName)
			: _pimpl(pimpl), _hashCode(hashCode)
			{
				ScopedLock(_pimpl->_storeRefCounts->_lock);
				auto existing = _pimpl->_storeRefCounts->_storeOperationsInFlight.find(_hashCode);
				if (existing != _pimpl->_storeRefCounts->_storeOperationsInFlight.end())
					Throw(std::runtime_error("Multiple stores in flight for the same compile product: " + descriptiveName.AsString()));
				auto read = LowerBound(_pimpl->_storeRefCounts->_readReferenceCount, _hashCode);
				if (read != _pimpl->_storeRefCounts->_readReferenceCount.end() && read->first == _hashCode && read->second != 0)
					Throw(std::runtime_error("Attempting to store compile product while still reading from it: " + descriptiveName.AsString()));
				_pimpl->_storeRefCounts->_storeOperationsInFlight.insert(_hashCode);
			}

			~WriteRefCountLock()
			{
				ScopedLock(_pimpl->_storeRefCounts->_lock);
				auto existing = _pimpl->_storeRefCounts->_storeOperationsInFlight.find(_hashCode);
				if (existing != _pimpl->_storeRefCounts->_storeOperationsInFlight.end()) {
					_pimpl->_storeRefCounts->_storeOperationsInFlight.erase(existing);
				} else {
					Log(Error) << "Missing _storeOperationsInFlight marker during cleanup op in StoreCompileProducts" << std::endl;
				}
			}

			WriteRefCountLock(const WriteRefCountLock&) = delete;
			WriteRefCountLock& operator=(const WriteRefCountLock&) = delete;
		private:
			IntermediatesStoreBase* _pimpl;
			uint64_t _hashCode;
		};
	};

	static std::string MakeSafeName(StringSection<> input)
	{
		auto result = input.AsString();
		for (auto&b:result)
			if (b == ':' || b == '*' || b == '/' || b == '\\') b = '-';
		return result;
	}

	uint64_t IntermediatesStoreBase::MakeHashCode(
		StringSection<> archivableName,
		CompileProductsGroupId groupId) const
	{
		return Hash64(archivableName.begin(), archivableName.end(), groupId);
	}

	uint64_t IntermediatesStoreBase::MakeHashCode(
		StringSection<> archiveName,
		ArchiveEntryId entryId,
		CompileProductsGroupId groupId) const
	{
		return HashCombine(entryId, Hash64(archiveName.begin(), archiveName.end(), groupId));
	}

	std::shared_ptr<IArtifactCollection> IntermediatesStoreBase::RetrieveCompileProducts(
		StringSection<> archivableName,
		CompileProductsGroupId groupId)
	{
		std::shared_lock<std::shared_timed_mutex> l(_lock);
		auto hashCode = MakeHashCode(archivableName, groupId);
		ReadRefCountLock readRef(this, hashCode, archivableName);

		auto groupi = _groups.find(groupId);
		if (groupi == _groups.end())
			Throw(std::runtime_error("GroupId has not be registered in intermediates store during retrieve operation"));

		if (groupi->second._looseFilesStorage)
			return groupi->second._looseFilesStorage->RetrieveCompileProducts(archivableName, _storeRefCounts, hashCode);
		if (groupi->second._archiveCacheSet) {
			auto archive = groupi->second._archiveCacheSet->GetArchive(groupi->second._archiveCacheBase + archivableName.AsString());
			if (archive)
				return archive->TryOpenFromCache(0);
		}
		return nullptr;
	}

	std::shared_ptr<IArtifactCollection> IntermediatesStoreBase::StoreCompileProducts(
		StringSection<> archivableName,
		CompileProductsGroupId groupId,
		IteratorRange<const SerializedArtifact*> artifacts,
		::Assets::AssetState state,
		IteratorRange<const DependencyValidation*> depVals)
	{
		if (!_allowStore)
			Throw(std::runtime_error("Attempting to store into a read-only intermediates store"));

		std::unique_lock<std::shared_timed_mutex> l(_lock);
		auto hashCode = MakeHashCode(archivableName, groupId);
		WriteRefCountLock writeRef(this, hashCode, archivableName);

		auto groupi = _groups.find(groupId);
		if (groupi == _groups.end())
			Throw(std::runtime_error("GroupId has not be registered in intermediates store during retrieve operation"));

		// Make sure the dependencies are unique, because we tend to get a lot of dupes from certain compile operations
		std::vector<::Assets::DependentFileState> dependencies;
		for (const auto&d:depVals) d.CollateDependentFileStates(dependencies);
		std::sort(dependencies.begin(), dependencies.end());
		auto i = std::unique(dependencies.begin(), dependencies.end());

		if (groupi->second._looseFilesStorage)
			return groupi->second._looseFilesStorage->StoreCompileProducts(archivableName, artifacts, state, {dependencies.begin(), i}, _storeRefCounts, hashCode);
		if (groupi->second._archiveCacheSet) {
			auto archive = groupi->second._archiveCacheSet->GetArchive(groupi->second._archiveCacheBase + archivableName.AsString());
			if (!archive)
				Throw(std::runtime_error("Failed to create archive when storing compile products"));

			archive->Commit(0, {}, artifacts, state, {dependencies.begin(), i});
		}
		return nullptr;
	}

	std::shared_ptr<IArtifactCollection> IntermediatesStoreBase::RetrieveCompileProducts(
		StringSection<> archiveName,
		ArchiveEntryId entryId,
		CompileProductsGroupId groupId)
	{
		std::shared_lock<std::shared_timed_mutex> l(_lock);
		auto hashCode = MakeHashCode(archiveName, entryId, groupId);
		ReadRefCountLock readRef(this, hashCode, (StringMeld<256>() << archiveName << "-" << std::hex << entryId).AsStringSection());

		auto groupi = _groups.find(groupId);
		if (groupi == _groups.end())
			Throw(std::runtime_error("GroupId has not be registered in intermediates store during retrieve operation"));
		if (!groupi->second._archiveCacheSet) return nullptr;

		auto archive = groupi->second._archiveCacheSet->GetArchive(groupi->second._archiveCacheBase + archiveName.AsString());
		if (!archive) return nullptr;

		return archive->TryOpenFromCache(entryId);
	}

	void IntermediatesStoreBase::StoreCompileProducts(
		StringSection<> archiveName,
		ArchiveEntryId entryId,
		StringSection<> entryDescriptiveName,
		CompileProductsGroupId groupId,
		IteratorRange<const SerializedArtifact*> artifacts,
		::Assets::AssetState state,
		IteratorRange<const DependencyValidation*> depVals)
	{
		if (!_allowStore)
			Throw(std::runtime_error("Attempting to store into a read-only intermediates store"));

		std::unique_lock<std::shared_timed_mutex> l(_lock);
		auto hashCode = MakeHashCode(archiveName, entryId, groupId);
		WriteRefCountLock writeRef(this, hashCode, entryDescriptiveName);

		auto groupi = _groups.find(groupId);
		if (groupi == _groups.end())
			Throw(std::runtime_error("GroupId has not be registered in intermediates store during retrieve operation"));

		if (!groupi->second._archiveCacheSet)
			Throw(std::runtime_error("Attempting to store compile products in an archive cache for a group that doesn't have archives enabled"));

		auto archive = groupi->second._archiveCacheSet->GetArchive(groupi->second._archiveCacheBase + archiveName.AsString());
		if (!archive)
			Throw(std::runtime_error("Failed to create archive when storing compile products"));

		// Make sure the dependencies are unique, because we tend to get a lot of dupes from certain compile operations
		std::vector<::Assets::DependentFileState> dependencies;
		for (const auto&d:depVals) d.CollateDependentFileStates(dependencies);
		std::sort(dependencies.begin(), dependencies.end());
		auto i = std::unique(dependencies.begin(), dependencies.end());

		archive->Commit(entryId, entryDescriptiveName.AsString(), artifacts, state, {dependencies.begin(), i});
	}

	bool IntermediatesStoreBase::AllowStore()
	{
		return _allowStore;
	}

	void IntermediatesStoreBase::FlushToDisk()
	{
		std::unique_lock<std::shared_timed_mutex> l(_lock);
		if (!_filesystem || !_allowStore) return;
		for (const auto&group:_groups)
			if (group.second._archiveCacheSet)
				group.second._archiveCacheSet->FlushToDisk();
	}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	class ArchivedIntermediatesStore : public IntermediatesStoreBase
	{
	public:
		CompileProductsGroupId RegisterCompileProductsGroup(
			StringSection<> name,
			const OSServices::LibVersionDesc& compilerVersionInfo,
			bool enableArchiveCacheSet) override;
		void DeregisterCompileProductsGroup(CompileProductsGroupId) override;

		virtual std::string GetBaseDirectory() const override { return {}; }

		std::string _filesystemMountPt;

		ArchivedIntermediatesStore(std::shared_ptr<IFileSystem> intermediatesFilesystem, StringSection<> intermediatesFilesystemMountPt);
		~ArchivedIntermediatesStore();
	};

	auto ArchivedIntermediatesStore::RegisterCompileProductsGroup(
		StringSection<> name, 
		const OSServices::LibVersionDesc& compilerVersionInfo,
		bool enableArchiveCacheSet) -> CompileProductsGroupId
	{
		std::unique_lock<std::shared_timed_mutex> l(_lock);
		auto id = Hash64(name.begin(), name.end());
		auto existing = _groups.find(id);
		if (existing == _groups.end()) {
			Group newGroup;
			std::string looseFilesBase = Concatenate(MakeSafeName(name), "/");
			newGroup._looseFilesStorage = std::make_shared<LooseFilesStorage>(_filesystem, looseFilesBase, _filesystemMountPt, compilerVersionInfo, _checkDepVals);
			if (enableArchiveCacheSet) {
				newGroup._archiveCacheSet = std::make_shared<ArchiveCacheSet>(_filesystem, compilerVersionInfo, _checkDepVals);
				newGroup._archiveCacheBase = looseFilesBase;
			}
			_groups.insert({id, std::move(newGroup)});		// ref count starts at 1
		} else
			++existing->second._refCount;
		return id;
	}

	void ArchivedIntermediatesStore::DeregisterCompileProductsGroup(CompileProductsGroupId id)
	{
		std::unique_lock<std::shared_timed_mutex> l(_lock);
		auto existing = _groups.find(id);
		if (existing != _groups.end()) {
			--existing->second._refCount;
			if (!existing->second._refCount) {
				if (existing->second._archiveCacheSet)
					existing->second._archiveCacheSet->FlushToDisk();
				_groups.erase(existing);
			}
		}
	}

	ArchivedIntermediatesStore::ArchivedIntermediatesStore(std::shared_ptr<IFileSystem> intermediatesFilesystem, StringSection<> intermediatesFilesystemMountPt)
	{
		assert(intermediatesFilesystem);
		_filesystem = std::move(intermediatesFilesystem);
		_filesystemMountPt = intermediatesFilesystemMountPt.AsString();
		_storeRefCounts = std::make_shared<StoreReferenceCounts>();
		_allowStore = false;
		_checkDepVals = false;
	}

	ArchivedIntermediatesStore::~ArchivedIntermediatesStore() {}

	std::shared_ptr<IIntermediatesStore> CreateArchivedIntermediatesStore(std::shared_ptr<IFileSystem> intermediatesFilesystem, StringSection<> intermediatesFilesystemMountPt)
	{
		return std::make_shared<ArchivedIntermediatesStore>(std::move(intermediatesFilesystem), intermediatesFilesystemMountPt);
	}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	class ProgressiveIntermediatesStore : public IntermediatesStoreBase
	{
	public:
		mutable std::string _resolvedBaseDirectory;
		mutable std::unique_ptr<IFileInterface> _markerFile;

		void ResolveBaseDirectory() const;
		std::string GetBaseDirectory() const override;

		CompileProductsGroupId RegisterCompileProductsGroup(
			StringSection<> name,
			const OSServices::LibVersionDesc& compilerVersionInfo,
			bool enableArchiveCacheSet) override;
		void DeregisterCompileProductsGroup(CompileProductsGroupId) override;

		ProgressiveIntermediatesStore(
			std::shared_ptr<IFileSystem> intermediatesFilesystem,
			StringSection<> baseDirectory,
			StringSection<> versionString,
			StringSection<> configString,
			bool universal = false);
		ProgressiveIntermediatesStore();		// default is an in-memory-only intermediates store
		~ProgressiveIntermediatesStore();
		ProgressiveIntermediatesStore(const ProgressiveIntermediatesStore&) = delete;
		ProgressiveIntermediatesStore& operator=(const ProgressiveIntermediatesStore&) = delete;
	};

	auto ProgressiveIntermediatesStore::RegisterCompileProductsGroup(
		StringSection<> name, 
		const OSServices::LibVersionDesc& compilerVersionInfo,
		bool enableArchiveCacheSet) -> CompileProductsGroupId
	{
		std::unique_lock<std::shared_timed_mutex> l(_lock);
		auto id = Hash64(name.begin(), name.end());
		auto existing = _groups.find(id);
		if (existing == _groups.end()) {
			Group newGroup;
			if (_filesystem) {
				auto safeGroupName = MakeSafeName(name);
				ResolveBaseDirectory();
				std::string looseFilesBase = Concatenate(_resolvedBaseDirectory, "/", safeGroupName, "/");
				newGroup._looseFilesStorage = std::make_shared<LooseFilesStorage>(_filesystem, looseFilesBase, std::string{}, compilerVersionInfo, _checkDepVals);
				if (enableArchiveCacheSet) {
					newGroup._archiveCacheSet = std::make_shared<ArchiveCacheSet>(_filesystem, compilerVersionInfo, _checkDepVals);
					newGroup._archiveCacheBase = looseFilesBase;
				}
			} else {
				// in-memory only group
				newGroup._archiveCacheSet = std::make_shared<ArchiveCacheSet>(nullptr, compilerVersionInfo, _checkDepVals);
			}
			_groups.insert({id, std::move(newGroup)});		// ref count starts at 1
		} else
			++existing->second._refCount;
		return id;
	}

	void ProgressiveIntermediatesStore::DeregisterCompileProductsGroup(CompileProductsGroupId id)
	{
		std::unique_lock<std::shared_timed_mutex> l(_lock);
		auto existing = _groups.find(id);
		if (existing != _groups.end()) {
			--existing->second._refCount;
			if (!existing->second._refCount) {
				if (existing->second._archiveCacheSet)
					existing->second._archiveCacheSet->FlushToDisk();
				_groups.erase(existing);
			}
		}
	}

	void ProgressiveIntermediatesStore::ResolveBaseDirectory() const
	{
		if (!_resolvedBaseDirectory.empty()) return;
		assert(_filesystem);

			//  First, we need to find an output directory to use.
			//  We want a directory that isn't currently being used, and
			//  that matches the version string.

		auto cfgDir = _constructorOptions._baseDir + "/.int-" + _constructorOptions._configString;
		std::string goodBranchDir;

			//  Look for existing directories that could match the version
			//  string we have. 
		std::set<unsigned> indicesUsed;
		auto searchableFS = std::dynamic_pointer_cast<ISearchableFileSystem>(_filesystem);
		assert(searchableFS);
		auto searchPath = BeginWalk(searchableFS, cfgDir);
		for (auto candidateDirectory=searchPath.begin_directories(); candidateDirectory!=searchPath.end_directories(); ++candidateDirectory) {
			auto candidateName = candidateDirectory.Name();
			
			unsigned asInt = 0;
			if (FastParseValue(MakeStringSection(candidateName), asInt) == AsPointer(candidateName.end()))
				indicesUsed.insert(asInt);

			auto markerFileName = cfgDir + "/" + candidateName + "/.store";
			std::unique_ptr<IFileInterface> markerFile;
			auto ioReason = TryOpen(markerFile, *_filesystem, MakeStringSection(markerFileName), "rb", 0);
			if (ioReason != IFileSystem::IOReason::Success)
				continue;

			auto fileSize = markerFile->GetSize();
			if (fileSize != 0) {
				auto rawData = std::unique_ptr<char[]>(new char[int(fileSize)]);
				markerFile->Read(rawData.get(), 1, size_t(fileSize));

				Formatters::TextInputFormatter<> formatter(MakeStringSection(rawData.get(), PtrAdd(rawData.get(), fileSize)));
				Formatters::StreamDOM<Formatters::TextInputFormatter<>> doc(formatter);

				auto compareVersion = doc.RootElement().Attribute("VersionString").Value();
				if (XlEqString(compareVersion, _constructorOptions._versionString)) {
					// this branch is already present, and is good... so use it
					goodBranchDir = cfgDir + "/" + candidateName;
					_markerFile = std::move(markerFile);
					break;
				}
			}
		}

		if (goodBranchDir.empty()) {
				// if we didn't find an existing folder we can use, we need to create a new one
				// search through to find the first unused directory
			for (unsigned d=0;;++d) {
				if (indicesUsed.find(d) != indicesUsed.end())
					continue;

				goodBranchDir = cfgDir + "/" + std::to_string(d);
				std::filesystem::create_directories(goodBranchDir);

				auto markerFileName = goodBranchDir + "/.store";

					// Opening without sharing to prevent other instances of XLE apps from using
					// the same directory.
				TryOpen(_markerFile, *_filesystem, MakeStringSection(markerFileName), "wb", 0);
				if (!_markerFile)
					Throw(std::runtime_error("Failed while opening intermediates store marker file"));
				auto outStr = std::string("VersionString=") + _constructorOptions._versionString + "\n";
				_markerFile->Write(outStr.data(), 1, outStr.size());
				break;
			}
		}

		_resolvedBaseDirectory = goodBranchDir;
	}


	std::string ProgressiveIntermediatesStore::GetBaseDirectory() const
	{
		ResolveBaseDirectory();
		return _resolvedBaseDirectory;
	}

	ProgressiveIntermediatesStore::ProgressiveIntermediatesStore(
		std::shared_ptr<IFileSystem> intermediatesFilesystem,
		StringSection<> baseDirectory, StringSection<> versionString, StringSection<> configString,
		bool universal)
	{
		_filesystem = intermediatesFilesystem;
		if (universal) {
			// This is the "universal" store directory. A single directory is used by all
			// versions of the game.
			_resolvedBaseDirectory = baseDirectory.AsString() + "/.int/u";
		} else {
			_constructorOptions._baseDir = baseDirectory.AsString();
			_constructorOptions._versionString = versionString.AsString();
			_constructorOptions._configString = configString.AsString();
		}
		_storeRefCounts = std::make_shared<StoreReferenceCounts>();
		_allowStore = true;
		_checkDepVals = true;
	}

	ProgressiveIntermediatesStore::ProgressiveIntermediatesStore()
	{
		_storeRefCounts = std::make_shared<StoreReferenceCounts>();
		_allowStore = true;
		_checkDepVals = true;
	}

	ProgressiveIntermediatesStore::~ProgressiveIntermediatesStore() 
	{
	}

	std::shared_ptr<IIntermediatesStore> CreateTemporaryCacheIntermediatesStore(
		std::shared_ptr<IFileSystem> intermediatesFilesystem,
		StringSection<> baseDirectory, StringSection<> versionString, StringSection<> configString, 
		bool universal)
	{
		return std::make_shared<ProgressiveIntermediatesStore>(std::move(intermediatesFilesystem), baseDirectory, versionString, configString, universal);
	}

	std::shared_ptr<IIntermediatesStore> CreateMemoryOnlyIntermediatesStore()
	{
		return std::make_shared<ProgressiveIntermediatesStore>();
	}

	std::pair<::Assets::DependencyValidation, bool> ConstructDepVal(IteratorRange<const DependentFileState*> files, StringSection<> archivableName)
	{
		if (files.empty())
			return {::Assets::DependencyValidation{}, true};		// if we have no dependencies whatsoever, we must always be considered valid

		auto depVal = GetDepValSys().Make(files);
		bool stillValid = depVal.GetValidationIndex() == 0;
		if (!stillValid && Verbose.IsEnabled()) {
			std::vector<DependencyUpdateReport> dependencyUpdates;
			depVal.CollateDependentFileUpdates(dependencyUpdates);

			for (const auto&update:dependencyUpdates) {
				if (update._currentStateSnapshot._state == FileSnapshot::State::DoesNotExist && update._registeredSnapshot._state != FileSnapshot::State::DoesNotExist) {
					Log(Verbose)
						<< "Asset (" << archivableName
						<< ") is invalidated because of missing dependency (" << update._filename << ")" << std::endl;
				} else if (update._currentStateSnapshot._state != FileSnapshot::State::DoesNotExist && update._registeredSnapshot._state == FileSnapshot::State::DoesNotExist) {
					Log(Verbose)
						<< "Asset (" << archivableName
						<< ") is invalidated because dependency (" << update._filename << ") was not present previously, but now exists" << std::endl;
				} else {
					Log(Verbose)
						<< "Asset (" << archivableName
						<< ") is invalidated because dependency (" << update._filename << ") state does not match expected" << std::endl;
				}
			}
		}
		return std::make_pair(std::move(depVal), stillValid);
	}

	IIntermediatesStore::~IIntermediatesStore() {}
}
