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
#include "../Utility/Streams/StreamDOM.h"
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
	class IntermediatesStore::Pimpl
	{
	public:
		// in very occasional cases, IFileSystem implementations may use IntermediatesStore during another IntermediatesStore operation
		std::shared_timed_mutex _lock;
		mutable std::string _resolvedBaseDirectory;
		mutable std::unique_ptr<IFileInterface> _markerFile;
		std::shared_ptr<IFileSystem> _filesystem;

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

		void ResolveBaseDirectory() const;
		uint64_t MakeHashCode(
			StringSection<> archivableName,
			CompileProductsGroupId groupId) const;
		uint64_t MakeHashCode(
			StringSection<> archiveName,
			ArchiveEntryId entryId,
			CompileProductsGroupId groupId) const;

		struct ReadRefCountLock
		{
			ReadRefCountLock(Pimpl* pimpl, uint64_t hashCode, StringSection<> descriptiveName)
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
		private:
			Pimpl* _pimpl;
			uint64_t _hashCode;
		};

		struct WriteRefCountLock
		{
			WriteRefCountLock(Pimpl* pimpl, uint64_t hashCode, StringSection<> descriptiveName)
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
		private:
			Pimpl* _pimpl;
			uint64_t _hashCode;
		};
	};

	static ResChar ConvChar(ResChar input) 
	{
		return (ResChar)((input == '\\')?'/':tolower(input));
	}

	static std::string MakeSafeName(StringSection<> input)
	{
		auto result = input.AsString();
		for (auto&b:result)
			if (b == ':' || b == '*' || b == '/' || b == '\\') b = '-';
		return result;
	}

	uint64_t IntermediatesStore::Pimpl::MakeHashCode(
		StringSection<> archivableName,
		CompileProductsGroupId groupId) const
	{
		return Hash64(archivableName.begin(), archivableName.end(), groupId);
	}

	uint64_t IntermediatesStore::Pimpl::MakeHashCode(
		StringSection<> archiveName,
		ArchiveEntryId entryId,
		CompileProductsGroupId groupId) const
	{
		return HashCombine(entryId, Hash64(archiveName.begin(), archiveName.end(), groupId));
	}

	bool IntermediatesStore::TryRegisterDependency(
		DependencyValidation& target,
		const DependentFileState& fileState,
		const StringSection<> assetName)
	{
		auto mostRecentState = GetDependentFileState(fileState._filename);
		target.RegisterDependency(mostRecentState);

		if (mostRecentState._status == DependentFileState::Status::Shadowed) {
			Log(Verbose) << "Asset (" << assetName << ") is invalidated because dependency (" << fileState._filename << ") is marked shadowed" << std::endl;
			return false;
		}

		if (mostRecentState._status == fileState._status) {
			// If the status on both is "DoesNotExist", then we do not consider this as invalidating the asset
			if (fileState._status != DependentFileState::Status::DoesNotExist && mostRecentState._timeMarker != fileState._timeMarker) {
				Log(Verbose)
					<< "Asset (" << assetName
					<< ") is invalidated because of file data on dependency (" << fileState._filename << ")" << std::endl;
				return false;
			}
		} else {
			if (mostRecentState._status == DependentFileState::Status::DoesNotExist && fileState._status != DependentFileState::Status::DoesNotExist) {
				Log(Verbose)
					<< "Asset (" << assetName
					<< ") is invalidated because of missing dependency (" << fileState._filename << ")" << std::endl;
			} else if (mostRecentState._status != DependentFileState::Status::DoesNotExist && fileState._status == DependentFileState::Status::DoesNotExist) {
				Log(Verbose)
					<< "Asset (" << assetName
					<< ") is invalidated because dependency (" << fileState._filename << ") was not present previously, but now exists" << std::endl;
			} else {
				Log(Verbose)
					<< "Asset (" << assetName
					<< ") is invalidated because dependency (" << fileState._filename << ") state does not match expected" << std::endl;
			}
			return false;
		}

		return true;
	}

	DependentFileState IntermediatesStore::GetDependentFileState(StringSection<ResChar> filename)
	{
		return GetDepValSys().GetDependentFileState(filename);
	}

	std::shared_ptr<IArtifactCollection> IntermediatesStore::RetrieveCompileProducts(
		StringSection<> archivableName,
		CompileProductsGroupId groupId)
	{
		std::shared_lock<std::shared_timed_mutex> l(_pimpl->_lock);
		auto hashCode = _pimpl->MakeHashCode(archivableName, groupId);
		Pimpl::ReadRefCountLock readRef(_pimpl.get(), hashCode, archivableName);

		auto groupi = _pimpl->_groups.find(groupId);
		if (groupi == _pimpl->_groups.end())
			Throw(std::runtime_error("GroupId has not be registered in intermediates store during retrieve operation"));

		return groupi->second._looseFilesStorage->RetrieveCompileProducts(archivableName, _pimpl->_storeRefCounts, hashCode);
	}

	std::shared_ptr<IArtifactCollection> IntermediatesStore::StoreCompileProducts(
		StringSection<> archivableName,
		CompileProductsGroupId groupId,
		IteratorRange<const ICompileOperation::SerializedArtifact*> artifacts,
		::Assets::AssetState state,
		IteratorRange<const DependentFileState*> dependencies)
	{
		std::unique_lock<std::shared_timed_mutex> l(_pimpl->_lock);
		auto hashCode = _pimpl->MakeHashCode(archivableName, groupId);
		Pimpl::WriteRefCountLock writeRef(_pimpl.get(), hashCode, archivableName);

		auto groupi = _pimpl->_groups.find(groupId);
		if (groupi == _pimpl->_groups.end())
			Throw(std::runtime_error("GroupId has not be registered in intermediates store during retrieve operation"));

		// Make sure the dependencies are unique, because we tend to get a lot of dupes from certain compile operations
		std::vector<DependentFileState> uniqueDependencies { dependencies.begin(), dependencies.end() };
		std::sort(uniqueDependencies.begin(), uniqueDependencies.end());
		auto i = std::unique(uniqueDependencies.begin(), uniqueDependencies.end());

		return groupi->second._looseFilesStorage->StoreCompileProducts(archivableName, artifacts, state, {uniqueDependencies.begin(), i}, _pimpl->_storeRefCounts, hashCode);
	}

	std::shared_ptr<IArtifactCollection> IntermediatesStore::RetrieveCompileProducts(
		StringSection<> archiveName,
		ArchiveEntryId entryId,
		CompileProductsGroupId groupId)
	{
		std::shared_lock<std::shared_timed_mutex> l(_pimpl->_lock);
		auto hashCode = _pimpl->MakeHashCode(archiveName, entryId, groupId);
		Pimpl::ReadRefCountLock readRef(_pimpl.get(), hashCode, (StringMeld<256>() << archiveName << "-" << std::hex << entryId).AsStringSection());

		auto groupi = _pimpl->_groups.find(groupId);
		if (groupi == _pimpl->_groups.end())
			Throw(std::runtime_error("GroupId has not be registered in intermediates store during retrieve operation"));
		if (!groupi->second._archiveCacheSet) return nullptr;

		auto archive = groupi->second._archiveCacheSet->GetArchive(groupi->second._archiveCacheBase + archiveName.AsString());
		if (!archive) return nullptr;

		return archive->TryOpenFromCache(entryId);
	}

	void IntermediatesStore::StoreCompileProducts(
		StringSection<> archiveName,
		ArchiveEntryId entryId,
		StringSection<> entryDescriptiveName,
		CompileProductsGroupId groupId,
		IteratorRange<const ICompileOperation::SerializedArtifact*> artifacts,
		::Assets::AssetState state,
		IteratorRange<const DependentFileState*> dependencies)
	{
		std::unique_lock<std::shared_timed_mutex> l(_pimpl->_lock);
		auto hashCode = _pimpl->MakeHashCode(archiveName, entryId, groupId);
		Pimpl::WriteRefCountLock writeRef(_pimpl.get(), hashCode, entryDescriptiveName);

		auto groupi = _pimpl->_groups.find(groupId);
		if (groupi == _pimpl->_groups.end())
			Throw(std::runtime_error("GroupId has not be registered in intermediates store during retrieve operation"));

		if (!groupi->second._archiveCacheSet)
			Throw(std::runtime_error("Attempting to store compile products in an archive cache for a group that doesn't have archives enabled"));

		auto archive = groupi->second._archiveCacheSet->GetArchive(groupi->second._archiveCacheBase + archiveName.AsString());
		if (!archive)
			Throw(std::runtime_error("Failed to create archive when storing compile products"));

		// Make sure the dependencies are unique, because we tend to get a lot of dupes from certain compile operations
		std::vector<DependentFileState> uniqueDependencies { dependencies.begin(), dependencies.end() };
		std::sort(uniqueDependencies.begin(), uniqueDependencies.end());
		auto i = std::unique(uniqueDependencies.begin(), uniqueDependencies.end());

		archive->Commit(entryId, entryDescriptiveName.AsString(), artifacts, state, {uniqueDependencies.begin(), i});
	}

	void IntermediatesStore::FlushToDisk()
	{
		std::unique_lock<std::shared_timed_mutex> l(_pimpl->_lock);
		for (const auto&group:_pimpl->_groups)
			if (group.second._archiveCacheSet)
				group.second._archiveCacheSet->FlushToDisk();
	}

	void IntermediatesStore::Pimpl::ResolveBaseDirectory() const
	{
		if (!_resolvedBaseDirectory.empty()) return;

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

				InputStreamFormatter<> formatter(MakeStringSection(rawData.get(), PtrAdd(rawData.get(), fileSize)));
				StreamDOM<InputStreamFormatter<>> doc(formatter);

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

	auto IntermediatesStore::RegisterCompileProductsGroup(
		StringSection<> name, 
		const OSServices::LibVersionDesc& compilerVersionInfo,
		bool enableArchiveCacheSet) -> CompileProductsGroupId
	{
		std::unique_lock<std::shared_timed_mutex> l(_pimpl->_lock);
		auto id = Hash64(name.begin(), name.end());
		auto existing = _pimpl->_groups.find(id);
		if (existing == _pimpl->_groups.end()) {
			auto safeGroupName = MakeSafeName(name);
			_pimpl->ResolveBaseDirectory();
			std::string looseFilesBase = Concatenate(_pimpl->_resolvedBaseDirectory, "/", safeGroupName, "/");
			Pimpl::Group newGroup;
			newGroup._looseFilesStorage = std::make_shared<LooseFilesStorage>(_pimpl->_filesystem, looseFilesBase, compilerVersionInfo);
			if (enableArchiveCacheSet) {
				newGroup._archiveCacheSet = std::make_shared<ArchiveCacheSet>(_pimpl->_filesystem, compilerVersionInfo);
				newGroup._archiveCacheBase = looseFilesBase;
			}
			_pimpl->_groups.insert({id, std::move(newGroup)});		// ref count starts at 1
		} else
			++existing->second._refCount;
		return id;
	}

	void IntermediatesStore::DeregisterCompileProductsGroup(CompileProductsGroupId id)
	{
		std::unique_lock<std::shared_timed_mutex> l(_pimpl->_lock);
		auto existing = _pimpl->_groups.find(id);
		if (existing != _pimpl->_groups.end()) {
			--existing->second._refCount;
			if (!existing->second._refCount) {
				if (existing->second._archiveCacheSet)
					existing->second._archiveCacheSet->FlushToDisk();
				_pimpl->_groups.erase(existing);
			}
		}
	}

	IntermediatesStore::IntermediatesStore(
		std::shared_ptr<IFileSystem> intermediatesFilesystem,
		StringSection<> baseDirectory, StringSection<> versionString, StringSection<> configString, 
		bool universal)
	{
		_pimpl = std::make_unique<Pimpl>();
		_pimpl->_filesystem = intermediatesFilesystem;
		if (universal) {
			// This is the "universal" store directory. A single directory is used by all
			// versions of the game.
			_pimpl->_resolvedBaseDirectory = baseDirectory.AsString() + "/.int/u";
		} else {
			_pimpl->_constructorOptions._baseDir = baseDirectory.AsString();
			_pimpl->_constructorOptions._versionString = versionString.AsString();
			_pimpl->_constructorOptions._configString = configString.AsString();
		}
		_pimpl->_storeRefCounts = std::make_shared<StoreReferenceCounts>();
	}

	IntermediatesStore::~IntermediatesStore() 
	{
	}
}
