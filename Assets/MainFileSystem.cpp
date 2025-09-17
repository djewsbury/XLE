// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "IFileSystem.h"
#include "MountingTree.h"
#include "AssetUtils.h"
#include "../OSServices/Log.h"
#include "../Utility/Streams/PathUtils.h"
#include "../Utility/MemoryUtils.h"
#include "../Utility/Threading/ThreadingUtils.h"
#include "../Utility/Threading/Mutex.h"

namespace Assets
{
	struct Ptrs
	{
		std::shared_ptr<MountingTree> s_mainMountingTree;
	};
	Ptrs& GetPtrs()
	{
		static Ptrs ptrs;
		return ptrs;
	}

	static IFileSystem::IOReason AsIOReason(IFileSystem::TranslateResult transResult)
	{
		switch (transResult) {
		case IFileSystem::TranslateResult::Pending: return IFileSystem::IOReason::Mounting;
		case IFileSystem::TranslateResult::Invalid: return IFileSystem::IOReason::Invalid;
		default: return IFileSystem::IOReason::Invalid;
		}
	}

	static FileSnapshot::State AsSnapshotState(IFileSystem::TranslateResult transResult)
	{
		switch (transResult) {
		case IFileSystem::TranslateResult::Pending: return FileSnapshot::State::Pending;
		case IFileSystem::TranslateResult::Invalid: return FileSnapshot::State::DoesNotExist;
		case IFileSystem::TranslateResult::Success: return FileSnapshot::State::Normal;
		default: return FileSnapshot::State::DoesNotExist;
		}
	}

///////////////////////////////////////////////////////////////////////////////////////////////////

	namespace Internal 
	{
		using LookupResult = MountingTree::EnumerableLookup::Result;

		template<typename FileType, typename CharType>
			static IFileSystem::IOReason TryOpen(FileType& result, StringSection<CharType> filename, const char openMode[], OSServices::FileShareMode::BitField shareMode)
		{
			result = FileType();

			MountingTree::CandidateObject candidateObject;
			auto& ptrs = GetPtrs();
			auto lookup = ptrs.s_mainMountingTree->Lookup(filename);
			for (;;) {
				auto r = lookup.TryGetNext(candidateObject);
				if (r == LookupResult::Invalidated) {
                    // "Mounting point lookup was invalidated when the mounting tree changed. Do not change the mount or unmount filesystems while other threads may be accessing the same mounting tree."
                    lookup = ptrs.s_mainMountingTree->Lookup(filename);
                    continue;
                }

				if (r == LookupResult::NoCandidates)
					break;

				assert(candidateObject._fileSystem);
				auto ioRes = candidateObject._fileSystem->TryOpen(result, candidateObject._marker, openMode, shareMode);
				if (ioRes != IFileSystem::IOReason::FileNotFound && ioRes != IFileSystem::IOReason::Invalid)
					return ioRes;
			}

			return IFileSystem::IOReason::FileNotFound;
		}

		template<typename FileType, typename CharType>
			static IFileSystem::IOReason TryOpen(FileType& result, StringSection<CharType> filename, uint64_t size, const char openMode[], OSServices::FileShareMode::BitField shareMode)
		{
			result = FileType();

			MountingTree::CandidateObject candidateObject;
			auto& ptrs = GetPtrs();
			auto lookup = ptrs.s_mainMountingTree->Lookup(filename);
			for (;;) {
                auto r = lookup.TryGetNext(candidateObject);
                if (r == LookupResult::Invalidated) {
                    // "Mounting point lookup was invalidated when the mounting tree changed. Do not change the mount or unmount filesystems while other threads may be accessing the same mounting tree."
                    lookup = ptrs.s_mainMountingTree->Lookup(filename);
                    continue;
                }

				if (r == LookupResult::NoCandidates)
					break;

				assert(candidateObject._fileSystem);
				auto ioRes = candidateObject._fileSystem->TryOpen(result, candidateObject._marker, size, openMode, shareMode);
				if (ioRes != IFileSystem::IOReason::FileNotFound && ioRes != IFileSystem::IOReason::Invalid)
					return ioRes;
			}

			return IFileSystem::IOReason::FileNotFound;
		}

		template<typename CharType>
			IFileSystem::IOReason TryMonitor(FileSnapshot& snapshot, StringSection<CharType> filename, const std::shared_ptr<IFileMonitor>& evnt)
		{
			snapshot = { FileSnapshot::State::DoesNotExist, 0 };
			MountingTree::CandidateObject candidateObject;
			auto& ptrs = GetPtrs();
			auto lookup = ptrs.s_mainMountingTree->Lookup(filename);

			std::optional<FileSnapshot> firstExistingSnapshot;
			bool gotSuccessfulMonitor = false;
			for (;;) {
				auto r = lookup.TryGetNext(candidateObject);
				if (r == LookupResult::Invalidated) {
                    // "Mounting point lookup was invalidated when the mounting tree changed. Do not change the mount or unmount filesystems while other threads may be accessing the same mounting tree."
                    lookup = ptrs.s_mainMountingTree->Lookup(filename);
                    continue;
                }

				if (r == LookupResult::NoCandidates)
					break;

				// We must call TryMonitor for each filesystem, because the filesystems return
				// "success" even if the file doesn't exist. So if we stop early, on the first
				// filesystem will be monitored
				assert(candidateObject._fileSystem);
				FileSnapshot thisSnapshot;
				auto ioRes = candidateObject._fileSystem->TryMonitor(thisSnapshot, candidateObject._marker, evnt);
				(void)ioRes;
				if (!firstExistingSnapshot && thisSnapshot._state == FileSnapshot::State::Normal)
					firstExistingSnapshot = thisSnapshot;
				gotSuccessfulMonitor |= ioRes == IFileSystem::IOReason::Success;
			}

			if (firstExistingSnapshot) {
				snapshot = firstExistingSnapshot.value();
				return gotSuccessfulMonitor ? IFileSystem::IOReason::Success : IFileSystem::IOReason::Invalid;
			} else {
				return gotSuccessfulMonitor ? IFileSystem::IOReason::Success : IFileSystem::IOReason::FileNotFound;
			}
		}

		template<typename CharType>
			IFileSystem::IOReason TryFakeFileChange(StringSection<CharType> filename)
		{
			MountingTree::CandidateObject candidateObject;
			auto& ptrs = GetPtrs();
			auto lookup = ptrs.s_mainMountingTree->Lookup(filename);
			for (;;) {
				auto r = lookup.TryGetNext(candidateObject);
				if (r == LookupResult::Invalidated) {
                    // "Mounting point lookup was invalidated when the mounting tree changed. Do not change the mount or unmount filesystems while other threads may be accessing the same mounting tree."
                    lookup = ptrs.s_mainMountingTree->Lookup(filename);
                    continue;
                }

				if (r == LookupResult::NoCandidates)
					break;

				// As with TryMonitor, we end up calling TryFakeFileChange for every filesystem, not
				// just the first that returns a success code
				assert(candidateObject._fileSystem);
				auto ioRes = candidateObject._fileSystem->TryFakeFileChange(candidateObject._marker);
				(void)ioRes;
			}

			return IFileSystem::IOReason::FileNotFound;
		}

		template<typename CharType>
			FileDesc TryGetDesc(StringSection<CharType> filename)
		{
			MountingTree::CandidateObject candidateObject;
			auto& ptrs = GetPtrs();
			auto lookup = ptrs.s_mainMountingTree->Lookup(filename);
			for (;;) {
				auto r = lookup.TryGetNext(candidateObject);
				if (r == LookupResult::Invalidated) {
                    // "Mounting point lookup was invalidated when the mounting tree changed. Do not change the mount or unmount filesystems while other threads may be accessing the same mounting tree."
                    lookup = ptrs.s_mainMountingTree->Lookup(filename);
                    continue;
                }

				if (r == LookupResult::NoCandidates) 
					break;

				assert(candidateObject._fileSystem);
				auto res = candidateObject._fileSystem->TryGetDesc(candidateObject._marker);
				if (res._snapshot._state != FileSnapshot::State::DoesNotExist) {
					res._mountedName = candidateObject._mountPoint + res._mountedName;
					return res;
				}
			}

			return FileDesc{ std::basic_string<utf8>(), std::basic_string<utf8>(), {FileSnapshot::State::DoesNotExist} };
		}

		template<typename CharType>
			auto TryTranslate(StringSection<CharType> filename) -> std::pair<IFileSystem::Marker, FileSystemId>
		{
			MountingTree::CandidateObject candidateObject;
			auto& ptrs = GetPtrs();
			auto lookup = ptrs.s_mainMountingTree->Lookup(filename);
			for (;;) {
				auto r = lookup.TryGetNext(candidateObject);
				if (r == LookupResult::Invalidated) {
                    // "Mounting point lookup was invalidated when the mounting tree changed. Do not change the mount or unmount filesystems while other threads may be accessing the same mounting tree."
                    lookup = ptrs.s_mainMountingTree->Lookup(filename);
                    continue;
                }

				if (r == LookupResult::NoCandidates) 
					break;

				// RawOS filesystems can give us a translation success even without checkin if the file exists. We have to use TryGetDesc to verify
				// if the file really exists
				assert(candidateObject._fileSystem);
				auto res = candidateObject._fileSystem->TryGetDesc(candidateObject._marker);
				if (res._snapshot._state != FileSnapshot::State::DoesNotExist)
					return { std::move(candidateObject._marker), candidateObject._mountId };
			}

			return {{}, ~0u};
		}
	}

	//
	// note -- the UTF8 and UTF16 versions of these functions are identical... They could be implemented
	//			with a template. But the C++ method resolution works better when they are explicitly separated
	//			like this.
	//		eg, because 
	//			MainFileSystem::FileOpen("SomeFile.txt",...);
	//		relies on automatic conversion for StringSection<utf8>, it works in this case, but not in the
	//		template case.
	//

	auto MainFileSystem::TryOpen(std::unique_ptr<IFileInterface>& result, StringSection<utf8> filename, const char openMode[], OSServices::FileShareMode::BitField shareMode) -> IOReason
	{
		return Internal::TryOpen(result, filename, openMode, shareMode);
	}

	auto MainFileSystem::TryOpen(OSServices::BasicFile& result, StringSection<utf8> filename, const char openMode[], OSServices::FileShareMode::BitField shareMode) -> IOReason
	{
		return Internal::TryOpen(result, filename, openMode, shareMode);
	}

	auto MainFileSystem::TryOpen(OSServices::MemoryMappedFile& result, StringSection<utf8> filename, uint64_t size, const char openMode[], OSServices::FileShareMode::BitField shareMode) -> IOReason
	{
		return Internal::TryOpen(result, filename, size, openMode, shareMode);
	}

	IFileSystem::IOReason MainFileSystem::TryMonitor(FileSnapshot& snapshot, StringSection<utf8> filename, const std::shared_ptr<IFileMonitor>& evnt)
	{
		return Internal::TryMonitor(snapshot, filename, evnt);
	}

	IFileSystem::IOReason MainFileSystem::TryFakeFileChange(StringSection<utf8> filename)
	{
		return Internal::TryFakeFileChange(filename);
	}

	FileDesc MainFileSystem::TryGetDesc(StringSection<utf8> filename)
	{
		return Internal::TryGetDesc(filename);
	}

	auto MainFileSystem::TryTranslate(StringSection<utf8> filename) -> std::pair<IFileSystem::Marker, FileSystemId>
	{
		return Internal::TryTranslate(filename);
	}

	auto MainFileSystem::TryOpen(std::unique_ptr<IFileInterface>& result, StringSection<utf16> filename, const char openMode[], OSServices::FileShareMode::BitField shareMode) -> IOReason
	{
		return Internal::TryOpen(result, filename, openMode, shareMode);
	}

	auto MainFileSystem::TryOpen(OSServices::BasicFile& result, StringSection<utf16> filename, const char openMode[], OSServices::FileShareMode::BitField shareMode) -> IOReason
	{
		return Internal::TryOpen(result, filename, openMode, shareMode);
	}

	auto MainFileSystem::TryOpen(OSServices::MemoryMappedFile& result, StringSection<utf16> filename, uint64_t size, const char openMode[], OSServices::FileShareMode::BitField shareMode) -> IOReason
	{
		return Internal::TryOpen(result, filename, size, openMode, shareMode);
	}

	IFileSystem::IOReason MainFileSystem::TryMonitor(FileSnapshot& snapshot, StringSection<utf16> filename, const std::shared_ptr<IFileMonitor>& evnt)
	{
		return Internal::TryMonitor(snapshot, filename, evnt);
	}

	IFileSystem::IOReason MainFileSystem::TryFakeFileChange(StringSection<utf16> filename)
	{
		return Internal::TryFakeFileChange(filename);
	}

	FileDesc MainFileSystem::TryGetDesc(StringSection<utf16> filename)
	{
		return Internal::TryGetDesc(filename);
	}

	auto MainFileSystem::TryTranslate(StringSection<utf16> filename) -> std::pair<IFileSystem::Marker, FileSystemId>
	{
		return Internal::TryTranslate(filename);
	}

	OSServices::BasicFile MainFileSystem::OpenBasicFile(StringSection<utf8> filename, const char openMode[], OSServices::FileShareMode::BitField shareMode)
	{
		OSServices::BasicFile result;
		auto ioRes = TryOpen(result, filename, openMode, shareMode);
		if (ioRes != IOReason::Success) {
			auto& depValSys = ::Assets::GetDepValSys();
			Throw(::Assets::Exceptions::IOResourceError(
				(unsigned)ioRes,
				depValSys.Make(filename),
				"Failure while opening file (%s) in mode (%s)", std::string((const char*)filename.begin(), (const char*)filename.end()).c_str(), openMode));
		}
		return result;
	}

	OSServices::MemoryMappedFile MainFileSystem::OpenMemoryMappedFile(StringSection<utf8> filename, uint64_t size, const char openMode[], OSServices::FileShareMode::BitField shareMode)
	{
		OSServices::MemoryMappedFile result;
		auto ioRes = TryOpen(result, filename, size, openMode, shareMode);
		if (ioRes != IOReason::Success) {
			auto& depValSys = ::Assets::GetDepValSys();
			Throw(::Assets::Exceptions::IOResourceError(
				(unsigned)ioRes,
				depValSys.Make(filename),
				"Failure while opening file (%s) in mode (%s)", std::string((const char*)filename.begin(), (const char*)filename.end()).c_str(), openMode));
		}
		return result;
	}

	std::unique_ptr<IFileInterface> MainFileSystem::OpenFileInterface(StringSection<utf8> filename, const char openMode[], OSServices::FileShareMode::BitField shareMode)
	{
		std::unique_ptr<IFileInterface> result;
		auto ioRes = TryOpen(result, filename, openMode, shareMode);
		if (ioRes != IOReason::Success) {
			auto& depValSys = ::Assets::GetDepValSys();
			Throw(::Assets::Exceptions::IOResourceError(
				(unsigned)ioRes, 
				depValSys.Make(filename),
				"Failure while opening file (%s) in mode (%s)", std::string((const char*)filename.begin(), (const char*)filename.end()).c_str(), openMode));
		}
		return result;
	}

	IFileSystem* MainFileSystem::GetFileSystem(FileSystemId id)
	{
		return GetPtrs().s_mainMountingTree->GetMountedFileSystem(id);		// in all current cases the FileSystemId overlaps with the MountId in s_mainMountingTree
	}

	std::shared_ptr<IFileSystem> MainFileSystem::GetFileSystemPtr(FileSystemId id)
	{
		return GetPtrs().s_mainMountingTree->GetMountedFileSystemPtr(id);		// in all current cases the FileSystemId overlaps with the MountId in s_mainMountingTree
	}

	std::basic_string<utf8> MainFileSystem::GetMountPoint(FileSystemId id)
	{
		return GetPtrs().s_mainMountingTree->GetMountPoint(id);
	}

	FileSystemWalker MainFileSystem::BeginWalk(StringSection<utf8> initialSubDirectory)
	{
		return GetPtrs().s_mainMountingTree->BeginWalk(initialSubDirectory);
	}

	FileSystemWalker MainFileSystem::BeginWalk(IteratorRange<const FileSystemId*> fileSystems, StringSection<utf8> initialSubDirectory)
	{
		auto& mountingTree = *GetPtrs().s_mainMountingTree;
		std::vector<FileSystemWalker::StartingFS> startingFS;
		auto initialSplit = MakeSplitPath(initialSubDirectory);
		#if defined(XLE_VERIFY_FILESYSTEMWALKER_POINTERS)
			for (auto id:fileSystems) {
				if (auto fs = std::dynamic_pointer_cast<ISearchableFileSystem>(mountingTree.GetMountedFileSystemPtr(id))) {
					auto mntPt = mountingTree.GetMountPoint(id);
					auto splitMntPt = MakeSplitPath(mntPt);
					startingFS.push_back({
						{}, MakeRelativePath(splitMntPt, initialSplit),
						std::move(fs), id});
				}
			}
		#else
			for (auto id:fileSystems) {
				if (auto fs = dynamic_cast<ISearchableFileSystem*>(mountingTree.GetMountedFileSystemPtr(id).get())) {
					auto mntPt = mountingTree.GetMountPoint(id);
					auto splitMntPt = MakeSplitPath(mntPt);
					startingFS.push_back({
						{}, MakeRelativePath(splitMntPt, initialSplit),
						fs, id});
				}
			}
		#endif
		return FileSystemWalker(std::move(startingFS));

	}

///////////////////////////////////////////////////////////////////////////////////////////////////

	const std::shared_ptr<MountingTree>& MainFileSystem::GetMountingTree() { assert(GetPtrs().s_mainMountingTree); return GetPtrs().s_mainMountingTree; }
	const std::shared_ptr<IFileSystem>& MainFileSystem::GetDefaultFileSystem() { return GetPtrs().s_mainMountingTree->GetDefaultFileSystem(); }
	void MainFileSystem::Init(const std::shared_ptr<MountingTree>& mountingTree, const std::shared_ptr<IFileSystem>& defaultFileSystem)
	{
		auto& ptrs = GetPtrs();
		ptrs.s_mainMountingTree = mountingTree;
		if (ptrs.s_mainMountingTree) {
			ptrs.s_mainMountingTree->SetDefaultFileSystem(defaultFileSystem);
		} else {
			assert(!defaultFileSystem);		// can't handle null mounting tree but non-null defaultFileSystem currently
		}
	}

    void MainFileSystem::Shutdown()
    {
        Init(nullptr, nullptr);
    }

	T2(CharType, FileObject) IFileSystem::IOReason TryOpen(FileObject& result, IFileSystem& fs, StringSection<CharType> fn, const char openMode[], OSServices::FileShareMode::BitField shareMode)
	{
		result = FileObject();

		IFileSystem::Marker marker;
		auto transResult = fs.TryTranslate(marker, fn);
		if (transResult == IFileSystem::TranslateResult::Success)
			return fs.TryOpen(result, marker, openMode, shareMode);

		return AsIOReason(transResult);
	}

	T2(CharType, FileObject) IFileSystem::IOReason TryOpen(FileObject& result, IFileSystem& fs, StringSection<CharType> fn, uint64_t size, const char openMode[], OSServices::FileShareMode::BitField shareMode)
	{
		result = FileObject();

		IFileSystem::Marker marker;
		auto transResult = fs.TryTranslate(marker, fn);
		if (transResult == IFileSystem::TranslateResult::Success)
			return fs.TryOpen(result, marker, size, openMode, shareMode);

		return AsIOReason(transResult);
	}

	T1(CharType) IFileSystem::IOReason TryMonitor(IFileSystem& fs, FileSnapshot& snapshot, StringSection<CharType> fn, const std::shared_ptr<IFileMonitor>& evnt)
	{
		IFileSystem::Marker marker;
		auto transResult = fs.TryTranslate(marker, fn);
		if (transResult == IFileSystem::TranslateResult::Success)
			return fs.TryMonitor(snapshot, marker, evnt);
		return AsIOReason(transResult);
	}

	T1(CharType) IFileSystem::IOReason TryFakeFileChange(IFileSystem& fs, StringSection<CharType> fn)
	{
		IFileSystem::Marker marker;
		auto transResult = fs.TryTranslate(marker, fn);
		if (transResult == IFileSystem::TranslateResult::Success)
			return fs.TryFakeFileChange(marker);
		return AsIOReason(transResult);
	}

	T1(CharType) FileDesc TryGetDesc(IFileSystem& fs, StringSection<CharType> fn)
	{
		IFileSystem::Marker marker;
		auto transResult = fs.TryTranslate(marker, fn);
		if (transResult == IFileSystem::TranslateResult::Success)
			return fs.TryGetDesc(marker);
		return FileDesc{std::basic_string<utf8>(), std::basic_string<utf8>(), {AsSnapshotState(transResult)}};
	}

	template IFileSystem::IOReason TryOpen<utf8, std::unique_ptr<IFileInterface>>(std::unique_ptr<IFileInterface>& result, IFileSystem& fs, StringSection<utf8> fn, const char openMode[], OSServices::FileShareMode::BitField shareMode);
	template IFileSystem::IOReason TryOpen<utf8, OSServices::BasicFile>(OSServices::BasicFile& result, IFileSystem& fs, StringSection<utf8> fn, const char openMode[], OSServices::FileShareMode::BitField shareMode);
	template IFileSystem::IOReason TryOpen<utf8, OSServices::MemoryMappedFile>(OSServices::MemoryMappedFile& result, IFileSystem& fs, StringSection<utf8> fn, uint64_t size, const char openMode[], OSServices::FileShareMode::BitField shareMode);
	template IFileSystem::IOReason TryMonitor<utf8>(IFileSystem& fs, FileSnapshot&, StringSection<utf8> fn, const std::shared_ptr<IFileMonitor>& evnt);
	template FileDesc TryGetDesc<utf8>(IFileSystem& fs, StringSection<utf8> fn);
	template IFileSystem::IOReason TryOpen<utf16, std::unique_ptr<IFileInterface>>(std::unique_ptr<IFileInterface>& result, IFileSystem& fs, StringSection<utf16> fn, const char openMode[], OSServices::FileShareMode::BitField shareMode);
	template IFileSystem::IOReason TryOpen<utf16, OSServices::BasicFile>(OSServices::BasicFile& result, IFileSystem& fs, StringSection<utf16> fn, const char openMode[], OSServices::FileShareMode::BitField shareMode);
	template IFileSystem::IOReason TryOpen<utf16, OSServices::MemoryMappedFile>(OSServices::MemoryMappedFile& result, IFileSystem& fs, StringSection<utf16> fn, uint64_t size, const char openMode[], OSServices::FileShareMode::BitField shareMode);
	template IFileSystem::IOReason TryMonitor<utf16>(IFileSystem& fs, FileSnapshot&, StringSection<utf16> fn, const std::shared_ptr<IFileMonitor>& evnt);
	template FileDesc TryGetDesc<utf16>(IFileSystem& fs, StringSection<utf16> fn);

	std::unique_ptr<uint8_t[]> MainFileSystem::TryLoadFileAsMemoryBlock(StringSection<char> sourceFileName, size_t* sizeResult)
	{
		return MainFileSystem::TryLoadFileAsMemoryBlock(sourceFileName, sizeResult, nullptr);
	}

	std::unique_ptr<uint8_t[]> MainFileSystem::TryLoadFileAsMemoryBlock(StringSection<char> sourceFileName, size_t* sizeResult, FileSnapshot* fileState)
	{
		std::unique_ptr<IFileInterface> file;
		if (MainFileSystem::TryOpen(file, sourceFileName, "rb", OSServices::FileShareMode::Read) == IFileSystem::IOReason::Success) {

			if (fileState)
				*fileState = file->GetSnapshot();

			size_t size = file->GetSize();
			if (size) {
				auto result = std::make_unique<uint8_t[]>(size);
				file->Read(result.get(), 1, size);
				if (sizeResult) {
					*sizeResult = size;
				}
				return result;
			}
		}

		// on missing file (or failed load), we return the equivalent of an empty file
		if (sizeResult) { *sizeResult = 0; }
		if (fileState)
			*fileState = { FileSnapshot::State::DoesNotExist, 0 };
		return nullptr;
	}

	Blob MainFileSystem::TryLoadFileAsBlob(StringSection<char> sourceFileName)
	{
		return MainFileSystem::TryLoadFileAsBlob(sourceFileName, nullptr);
	}

	Blob MainFileSystem::TryLoadFileAsBlob(StringSection<char> sourceFileName, FileSnapshot* fileState)
	{
		std::unique_ptr<IFileInterface> file;
		if (MainFileSystem::TryOpen(file, sourceFileName, "rb", OSServices::FileShareMode::Read) == IFileSystem::IOReason::Success) {

			if (fileState)
				*fileState = file->GetSnapshot();

			size_t size = file->GetSize();
			if (size) {
				auto result = std::make_shared<std::vector<uint8_t>>(size);
				file->Read(result->data(), 1, size);
				return result;
			}
		}

		if (fileState)
			*fileState = { FileSnapshot::State::DoesNotExist, 0 };
		return nullptr;
	}

	FileSystemWalker BeginWalk(const std::shared_ptr<ISearchableFileSystem>& fs, StringSection<> initialSubDirectory)
	{
		std::vector<FileSystemWalker::StartingFS> startingFS;
		#if defined(XLE_VERIFY_FILESYSTEMWALKER_POINTERS)
			startingFS.push_back({{}, initialSubDirectory.AsString(), fs, 0});		// here, relying on 0 being a sentinel in the mounting tree than doesn't correspond to any mountId
		#else
			startingFS.push_back({{}, initialSubDirectory.AsString(), fs.get(), 0});		// here, relying on 0 being a sentinel in the mounting tree than doesn't correspond to any mountId
		#endif
		return FileSystemWalker(std::move(startingFS));
	}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	#if defined(XLE_VERIFY_FILESYSTEMWALKER_POINTERS)
		FileSystemWalker::StartingFS::StartingFS(
			const std::string& pendingDirectories,
			const std::string& internalPoint,
			std::weak_ptr<ISearchableFileSystem> fs,
			FileSystemId fsId)
		: _pendingDirectories(pendingDirectories)
		, _internalPoint(internalPoint)
		, _fsVerification(std::move(fs))
		, _fsId(fsId)
		{
			_fs = _fsVerification.lock().get();
			assert(_fs);
		}
	#else
		FileSystemWalker::StartingFS::StartingFS(
			const std::string& pendingDirectories,
			const std::string& internalPoint,
			ISearchableFileSystem* fs,
			FileSystemId fsId)
		: _pendingDirectories(pendingDirectories)
		, _internalPoint(internalPoint)
		, _fs(fs)
		, _fsId(fsId)
		{
		}
	#endif

	class FileSystemWalker::Pimpl
	{
	public:
		std::vector<StartingFS> _fileSystems;

		struct SubFile
		{
			unsigned _filesystemIndex;
			IFileSystem::Marker _marker;
			FileDesc _desc;
			uint64_t _naturalNameHash;
		};
		std::vector<SubFile> _files;

		struct SubDirectory
		{
			std::string _name;
			std::vector<unsigned> _filesystemIndices;
		};
		std::vector<std::pair<uint64_t, SubDirectory>> _directories;

		bool _foundFiles = false;
		bool _foundDirectories = false;

		Pimpl(std::vector<StartingFS>&& fileSystems)
		: _fileSystems(std::move(fileSystems)) {}

		void FindFiles()
		{
			if (_foundFiles) return;
			assert(_files.empty());

			for (unsigned fsIdx=0; fsIdx<_fileSystems.size(); ++fsIdx) {
				auto& fs = _fileSystems[fsIdx];
				if (!fs._pendingDirectories.empty()) continue;

				CheckPointers(fs);
				auto foundMarkers = fs._fs->FindFiles(MakeStringSection(fs._internalPoint), "*");
				auto mountPoint = MainFileSystem::GetMountPoint(fs._fsId);

				auto* baseFS = dynamic_cast<IFileSystem*>(fs._fs);
				assert(baseFS);
				for (auto&m:foundMarkers) {
					// The filesystem will give us it's internal "marker" representation of the filename
					// But we're probably more interested in the natural name of the file; but we'll have
					// to query that from the filesystem again
					auto desc = baseFS->TryGetDesc(m);
					if (desc._snapshot._state != FileSnapshot::State::Normal) {
						Log(Warning) << "Unexpected file state found while searching directory tree" << std::endl;
						continue;
					}

					// see notes in Name() -- desc._mountedName will include some directory parts, we will strip it down to just the filename part
					auto hash = HashFilenameAndPath(MakeFileNameSplitter(desc._mountedName).FileAndExtension());
					auto existing = std::find_if(
						_files.begin(), _files.end(),
						[hash](const SubFile& file) { return file._naturalNameHash == hash; });

					// When we multiple files with the same name, we'll always keep whichever we found
					// first. Normally this should only happen when 2 different filesystems have a file
					// with the same name, mounted at the same location.
					if (existing == _files.end()) {
						desc._mountedName = mountPoint + desc._mountedName;
						_files.emplace_back(SubFile{
							fsIdx, std::move(m), 
							std::move(desc), hash});
					} else {
						// If you hit this assert, it might mean that a filesystem is producing multiple entries with the same name
						assert(existing->_filesystemIndex != fsIdx);
					}
				}
			}

			_foundFiles = true;
		}

		void FindDirectories()
		{
			if (_foundDirectories) return;
			assert(_directories.empty());

			for (unsigned fsIdx=0; fsIdx<_fileSystems.size(); ++fsIdx) {
				auto& fs = _fileSystems[fsIdx];
				if (!fs._pendingDirectories.empty()) {
					auto splitPath = MakeSplitPath(fs._pendingDirectories);
                    if (splitPath.GetSectionCount() != 0) {
                        auto dir = splitPath.GetSections()[0];
                        auto hash = HashFilenameAndPath(dir);
                        auto existing = LowerBound(_directories, hash);
                        if (existing == _directories.end() || existing->first != hash)
                            existing = _directories.insert(existing, std::make_pair(hash, SubDirectory{dir.AsString()}));
                        existing->second._filesystemIndices.push_back(fsIdx);
                        continue;
                    }
				}

				CheckPointers(fs);
				auto foundSubDirs = fs._fs->FindSubDirectories(MakeStringSection(fs._internalPoint));
				if (foundSubDirs.size() < 8 && _directories.size() < 8) {
					for (auto&m:foundSubDirs) {
						auto hash = HashFilenameAndPath(MakeStringSection(m));
						auto existing = LowerBound(_directories, hash);
						if (existing == _directories.end() || existing->first != hash)
							existing = _directories.insert(existing, std::make_pair(hash, SubDirectory{m}));
						existing->second._filesystemIndices.push_back(fsIdx);
						continue;
					}
				} else {
					// Large number of subdirectories, we should do a sort & merge
					// This would be even more efficient if the filesystem could give us the strings and hashes in a more
					// efficient form to begin with
					std::vector<std::pair<uint64_t, std::string>> sortedFoundSubdirs;
					sortedFoundSubdirs.reserve(foundSubDirs.size());
					for (const auto& f:foundSubDirs)
						sortedFoundSubdirs.emplace_back(HashFilenameAndPath(MakeStringSection(f)), f);
					std::sort(sortedFoundSubdirs.begin(), sortedFoundSubdirs.end(), CompareFirst2{});

					// we can't use std::inplace_merge(), because we want to do a slightly custom merge
					std::vector<std::pair<uint64_t, SubDirectory>> newDirectories;
					newDirectories.reserve(_directories.size()+sortedFoundSubdirs.size());
					auto m0 = _directories.begin();
					auto m1 = sortedFoundSubdirs.begin();
					while (m0 != _directories.end() && m1 != sortedFoundSubdirs.end()) {
						if (m0->first == m1->first) {
							newDirectories.emplace_back(std::move(*m0));
							newDirectories.back().second._filesystemIndices.push_back(fsIdx);
							++m0; ++m1;
						} else if (m0->first < m1->first) {
							newDirectories.emplace_back(std::move(*m0));
							++m0;
						} else {
							assert(m1->first < m0->first);
							newDirectories.emplace_back(m1->first, SubDirectory{m1->second});
							newDirectories.back().second._filesystemIndices.push_back(fsIdx);
							++m1;
						}
					}
					while (m0 != _directories.end()) {
						newDirectories.emplace_back(std::move(*m0));
						++m0;
					}
					while (m1 != sortedFoundSubdirs.end()) {
						newDirectories.emplace_back(m1->first, SubDirectory{m1->second, {fsIdx}});
						++m1;
					}
					_directories = std::move(newDirectories);
				}
			}

			_foundDirectories = true;
		}

		void CheckPointers(const StartingFS&fs)
		{
			#if defined(XLE_VERIFY_FILESYSTEMWALKER_POINTERS)
				assert(fs._fsVerification.lock());
			#endif
		}
	};

	auto FileSystemWalker::begin_directories() const -> DirectoryIterator
	{
		_pimpl->FindDirectories();
		return DirectoryIterator{this, 0};
	}

	auto FileSystemWalker::end_directories() const -> DirectoryIterator
	{
		_pimpl->FindDirectories();
		return DirectoryIterator{this, (unsigned)_pimpl->_directories.size()};
	}

	auto FileSystemWalker::begin_files() const -> FileIterator
	{
		_pimpl->FindFiles();
		return FileIterator{this, 0};
	}

	auto FileSystemWalker::end_files() const -> FileIterator
	{
		_pimpl->FindFiles();
		return FileIterator{this, (unsigned)_pimpl->_files.size()};
	}

	FileSystemWalker FileSystemWalker::RecurseTo(const std::basic_string<utf8>& subDirectory) const
	{
		std::vector<StartingFS> nextStep;

		auto hash = HashFilenameAndPath(MakeStringSection(subDirectory));

		_pimpl->FindDirectories();
		auto i = LowerBound(_pimpl->_directories, hash);
		if (i == _pimpl->_directories.end() || i->first != hash)
			return {};

		for (auto fsIdx:i->second._filesystemIndices) {
			auto& fs = _pimpl->_fileSystems[fsIdx];
			auto splitPath = MakeSplitPath(fs._pendingDirectories);
            if (splitPath.GetSectionCount() != 0) {
				assert(HashFilenameAndPath(splitPath.GetSection(0)) == hash);

				// strip off the first part of the path name
				auto sections = splitPath.GetSections();
				utf8 newPending[MaxPath];
				SplitPath<utf8>(std::vector<SplitPath<utf8>::Section>{&sections[1], sections.end()}).Rebuild(newPending);
				#if defined(XLE_VERIFY_FILESYSTEMWALKER_POINTERS)
					nextStep.emplace_back(StartingFS{newPending, fs._internalPoint, fs._fsVerification, fs._fsId});
				#else
					nextStep.emplace_back(StartingFS{newPending, fs._internalPoint, fs._fs, fs._fsId});
				#endif
			} else {
				auto newInternalPoint = fs._internalPoint;
				if (!newInternalPoint.empty()) newInternalPoint += "/";
				newInternalPoint += subDirectory;
				#if defined(XLE_VERIFY_FILESYSTEMWALKER_POINTERS)
					nextStep.emplace_back(StartingFS{{}, newInternalPoint, fs._fsVerification, fs._fsId});
				#else
					nextStep.emplace_back(StartingFS{{}, newInternalPoint, fs._fs, fs._fsId});
				#endif
			}
		}

		return FileSystemWalker{std::move(nextStep)};
	}

	FileSystemWalker::FileSystemWalker()
	{
		_pimpl = std::make_unique<Pimpl>(std::vector<StartingFS>{});
	}

	FileSystemWalker::FileSystemWalker(std::vector<StartingFS>&& fileSystems)
	{
		_pimpl = std::make_unique<Pimpl>(std::move(fileSystems));
	}

	FileSystemWalker::~FileSystemWalker() {}

	FileSystemWalker::FileSystemWalker(FileSystemWalker&& moveFrom)
	: _pimpl(std::move(moveFrom._pimpl))
	{
	}
	FileSystemWalker& FileSystemWalker::operator=(FileSystemWalker&& moveFrom)
	{
		_pimpl.reset();
		_pimpl = std::move(moveFrom._pimpl);
		return *this;
	}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	FileSystemWalker FileSystemWalker::DirectoryIterator::get() const
	{
		return _helper->RecurseTo(_helper->_pimpl->_directories[_idx].second._name);
	}

	std::basic_string<utf8> FileSystemWalker::DirectoryIterator::Name() const
	{
		return _helper->_pimpl->_directories[_idx].second._name;
	}

	FileSystemWalker::DirectoryIterator::DirectoryIterator(const FileSystemWalker* helper, unsigned idx)
	: _helper(helper), _idx(idx)
	{
	}

	auto FileSystemWalker::FileIterator::get() const -> Value
	{
		auto fsIdx = _helper->_pimpl->_files[_idx]._filesystemIndex;
		return {
			_helper->_pimpl->_files[_idx]._marker,
			_helper->_pimpl->_fileSystems[fsIdx]._fsId};
	}

	FileDesc FileSystemWalker::FileIterator::Desc() const
	{
		return _helper->_pimpl->_files[_idx]._desc;
	}

	std::string FileSystemWalker::FileIterator::Name() const
	{
		#if defined(_DEBUG)
			// We could calculate this in a more expensive way by using MakeRelativePath using the mounted filename
			// and the mounted path of the directory... But it should just ultimately come down to stripping off
			// any path components from the filename
			auto& fs = _helper->_pimpl->_fileSystems[_helper->_pimpl->_files[_idx]._filesystemIndex];
			auto fsMountedPath = Concatenate(MainFileSystem::GetMountPoint(fs._fsId), "/", fs._internalPoint);
			auto fsPath = MakeSplitPath(fsMountedPath);
			auto test = MakeRelativePath(fsPath, MakeSplitPath(_helper->_pimpl->_files[_idx]._desc._mountedName));
			assert(XlEqString(MakeFileNameSplitter(_helper->_pimpl->_files[_idx]._desc._mountedName).FileAndExtension(), test));
		#endif
		return MakeFileNameSplitter(_helper->_pimpl->_files[_idx]._desc._mountedName).FileAndExtension().AsString();
	}

	FileSystemWalker::FileIterator::FileIterator(const FileSystemWalker* helper, unsigned idx)
	: _helper(helper), _idx(idx)
	{
	}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	std::unique_ptr<uint8_t[]> MainFileSystem::TryLoadFileAsMemoryBlock_TolerateSharingErrors(StringSection<char> sourceFileName, size_t* sizeResult)
	{
		return MainFileSystem::TryLoadFileAsMemoryBlock_TolerateSharingErrors(sourceFileName, sizeResult, nullptr);
	}

	std::unique_ptr<uint8_t[]> MainFileSystem::TryLoadFileAsMemoryBlock_TolerateSharingErrors(StringSection<char> sourceFileName, size_t* sizeResult, FileSnapshot* fileState)
	{
		std::unique_ptr<::Assets::IFileInterface> file;

        unsigned retryCount = 0;
        for (;;) {
            auto openResult = ::Assets::MainFileSystem::TryOpen(file, sourceFileName, "rb", OSServices::FileShareMode::Read);
            if (openResult == ::Assets::IFileSystem::IOReason::Success) {

				if (fileState)
					*fileState = file->GetSnapshot();

                size_t size = file->GetSize();
                if (sizeResult) {
                    *sizeResult = size;
                }
                if (size) {
                    auto result = std::make_unique<uint8_t[]>(size);
                    file->Read(result.get(), 1, size);
                    return result;
                } else {
                    return nullptr;
                }
            }

            // If we get an access denied error, we're going to try a few more times, with short
            // delays in between. This can be important when hot reloading a resource -- because
            // we will get the filesystem update trigger on write, before an editor has closed
            // the file. During that window, we can get a sharing failure. We just have to yield
            // some CPU time and allow the editor to close the file.
            if (openResult != ::Assets::IFileSystem::IOReason::ExclusiveLock || retryCount >= 5) break;

            ++retryCount;
            Threading::Sleep(retryCount*retryCount*15);
        }

        // on missing file (or failed load), we return the equivalent of an empty file
        if (sizeResult) { *sizeResult = 0; }
		if (fileState)
			*fileState = { FileSnapshot::State::DoesNotExist, 0 };
        return nullptr;
	}

	Blob MainFileSystem::TryLoadFileAsBlob_TolerateSharingErrors(StringSection<char> sourceFileName, FileSnapshot* fileState)
	{
		std::unique_ptr<IFileInterface> file;
		unsigned retryCount = 0;
        for (;;) {
			auto openResult = MainFileSystem::TryOpen(file, sourceFileName, "rb", OSServices::FileShareMode::Read);
			if (openResult == IFileSystem::IOReason::Success) {
				if (fileState)
					*fileState = file->GetSnapshot();

				size_t size = file->GetSize();
				if (size) {
					auto result = std::make_shared<std::vector<uint8_t>>(size);
					file->Read(result->data(), 1, size);
					return result;
				} else {
					return nullptr;
				}
			}

			// See similar logic in TryLoadFileAsMemoryBlock_TolerateSharingErrors for retrying
			// after getting a "ExclusiveLock" error result
			if (openResult != ::Assets::IFileSystem::IOReason::ExclusiveLock || retryCount >= 5) break;

            ++retryCount;
            Threading::Sleep(retryCount*retryCount*15);
		}

		if (fileState)
			*fileState = { FileSnapshot::State::DoesNotExist, 0 };
		return nullptr;
	}

	Blob MainFileSystem::TryLoadFileAsBlob_TolerateSharingErrors(StringSection<char> sourceFileName)
	{
		return MainFileSystem::TryLoadFileAsBlob_TolerateSharingErrors(sourceFileName, nullptr);
	}

	IFileInterface::~IFileInterface() {}
	IFileSystem::~IFileSystem() {}
	ISearchableFileSystem::~ISearchableFileSystem() {}
}
