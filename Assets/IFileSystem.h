// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../Utility/UTFUtils.h"			// for utf8, utf16
#include "../Utility/StringUtils.h"			// for StringSection
#include "../OSServices/RawFS.h"
#include "../Core/Types.h"
#include "../Core/Exceptions.h"
#include <string>
#include <memory>

namespace OSServices { class OnChangeCallback; }

namespace Assets
{
	class FileDesc;
	struct FileSnapshot;
	class MountingTree;
	class IFileInterface;
	using IFileMonitor = OSServices::OnChangeCallback;
	using Blob = std::shared_ptr<std::vector<uint8_t>>;
	using FileSystemId = unsigned;
	static const FileSystemId FileSystemId_Invalid = ~0u;

	static const OSServices::FileShareMode::BitField FileShareMode_Default = OSServices::FileShareMode::Read;

	/// <summary>Interface for interacting with a file</summary>
	/// A file can be a physical file on disk, or any logical object that behaves like a file.
	/// IFileInterface objects are typically returned from IFileSystem implementations as a result
	/// of an "open" operation.
	///
	/// This provides typical file system behaviour, such as reading, writing, searching and
	/// getting description information.
	class IFileInterface
	{
	public:
		virtual size_t			Write(const void * source, size_t size, size_t count = 1) never_throws = 0;
		virtual size_t			Read(void * destination, size_t size, size_t count = 1) const never_throws = 0;
		virtual ptrdiff_t		Seek(ptrdiff_t seekOffset, OSServices::FileSeekAnchor = OSServices::FileSeekAnchor::Start) never_throws = 0;
		virtual size_t			TellP() const never_throws = 0;

		virtual size_t			GetSize() const never_throws = 0;
		virtual FileSnapshot	GetSnapshot() const never_throws = 0;

		virtual 			   ~IFileInterface();
	};

	/// <summary>Interface for a mountable virtual file system</summary>
	/// Provides a generic way to access different types of resources in a file-system like way.
	/// Typical implementions include things like archive files and "virtual" memory-based files.
	/// But the underlying OS filesystem is accessed via a IFileSystem, as well.
	///
	/// File systems can be mounted via a MountingTree. This works much like the *nix virtual
	/// file system (where new file systems can be mounted under any filespec prefix).
	/// 
	/// IFileSystem can be compared to the interfaces in the /fs/ tree of linux. Some of the 
	/// functions provide similar functionality. It's possible that we could build an adapter
	/// to allow filesystem implementations from linux to mounted as a IFileSystem.
	/// Howver, note that IFileSystem is intended mostly for input. So there are no functions for 
	/// things like creating or removing directories.
	class IFileSystem
	{
	public:
		using Marker = std::vector<uint8_t>;

		enum class TranslateResult { Success, Pending, Invalid };
		virtual TranslateResult		TryTranslate(Marker& result, StringSection<utf8> filename) = 0;
		virtual TranslateResult		TryTranslate(Marker& result, StringSection<utf16> filename) = 0;

		using IOReason = OSServices::Exceptions::IOException::Reason;

		virtual IOReason	TryOpen(std::unique_ptr<IFileInterface>& result, const Marker& marker, const char openMode[], OSServices::FileShareMode::BitField shareMode=FileShareMode_Default) = 0;
		virtual IOReason	TryOpen(OSServices::BasicFile& result, const Marker& marker, const char openMode[], OSServices::FileShareMode::BitField shareMode=FileShareMode_Default) = 0;
		virtual IOReason	TryOpen(OSServices::MemoryMappedFile& result, const Marker& marker, uint64_t size, const char openMode[], OSServices::FileShareMode::BitField shareMode=FileShareMode_Default) = 0;

		virtual	IOReason	TryMonitor(/* out */ FileSnapshot&, const Marker& marker, const std::shared_ptr<IFileMonitor>& evnt) = 0;
		virtual IOReason	TryFakeFileChange(const Marker& marker) = 0;
		virtual	FileDesc	TryGetDesc(const Marker& marker) = 0;
		virtual				~IFileSystem();
	};

	class ISearchableFileSystem
	{
	public:
		virtual std::vector<IFileSystem::Marker> FindFiles(
			StringSection<utf8> baseDirectory,
			StringSection<utf8> regexMatchPattern) = 0;
		virtual std::vector<std::basic_string<utf8>> FindSubDirectories(
			StringSection<utf8> baseDirectory) = 0;
		virtual ~ISearchableFileSystem();
	};

	class FileSystemWalker
	{
	public:
		class DirectoryIterator 
		{
		public:
			FileSystemWalker get() const;
			FileSystemWalker operator*() const { return get(); }
			std::basic_string<utf8> Name() const;
			friend bool operator==(const DirectoryIterator& lhs, const DirectoryIterator& rhs)
			{
				assert(lhs._helper == rhs._helper);
				return lhs._idx == rhs._idx;
			}
			friend bool operator!=(const DirectoryIterator& lhs, const DirectoryIterator& rhs)
			{
				return !operator==(lhs, rhs);
			}
			void operator++() { ++_idx; }
			void operator++(int) { ++_idx; }
		private:
			DirectoryIterator(const FileSystemWalker* helper, unsigned idx);
			friend class FileSystemWalker;
			const FileSystemWalker* _helper;
			unsigned _idx;
		};

		class FileIterator
		{
		public:
			struct Value
			{
				IFileSystem::Marker _marker;
				FileSystemId _fs;
			};
			Value get() const;
			Value operator*() const { return get(); }
			FileDesc Desc() const;
			friend bool operator==(const FileIterator& lhs, const FileIterator& rhs)
			{
				assert(lhs._helper == rhs._helper);
				return lhs._idx == rhs._idx;
			}
			friend bool operator!=(const FileIterator& lhs, const FileIterator& rhs)
			{
				return !operator==(lhs, rhs);
			}
			void operator++() { ++_idx; }
			void operator++(int) { ++_idx; }
		private:
			FileIterator(const FileSystemWalker* helper, unsigned idx);
			friend class FileSystemWalker;
			const FileSystemWalker* _helper;
			unsigned _idx;
		};
		
		DirectoryIterator begin_directories() const;
		DirectoryIterator end_directories() const;

		FileIterator begin_files() const;
		FileIterator end_files() const;

		FileSystemWalker RecurseTo(const std::basic_string<utf8>& subDirectory) const;

		FileSystemWalker();
		~FileSystemWalker();
		FileSystemWalker(FileSystemWalker&&);
		FileSystemWalker& operator=(FileSystemWalker&&);
	private:
		class Pimpl;
		std::unique_ptr<Pimpl> _pimpl;

		struct StartingFS
		{
			std::basic_string<utf8> _pendingDirectories;
			std::basic_string<utf8> _internalPoint;
			std::shared_ptr<ISearchableFileSystem> _fs;
			FileSystemId _fsId = FileSystemId_Invalid;
		};

		FileSystemWalker(std::vector<StartingFS>&& fileSystems);

		friend class MountingTree;
		friend FileSystemWalker BeginWalk(const std::shared_ptr<ISearchableFileSystem>&, StringSection<>);
	};

	struct FileSnapshot
	{
		enum class State { DoesNotExist, Normal, Pending };
		State					_state;
		OSServices::FileTime	_modificationTime;
		friend bool operator==(const FileSnapshot& lhs, const FileSnapshot& rhs);
		friend bool operator<(const FileSnapshot& lhs, const FileSnapshot& rhs);
	};

	/// <summary>Description of a file object within a filesystem</summary>
	/// Typically files have a few basic properties that can be queried.
	/// But note the "files" in this sense can mean more than just files on disk.
	/// So some properties will not apply to all files.
	/// Also note that some filesystems can map multiple names onto the same object.
	/// (for example, a filesystem that is not case sensitive will map all case variations
	/// onto the same file).
	/// In cases like this, the "_naturalName" below represents the form closest to how
	/// the object is stored internally.
	class FileDesc
	{
	public:
		std::basic_string<utf8>	_naturalName;
		std::basic_string<utf8>	_mountedName;
		FileSnapshot 			_snapshot;
		uint64_t				_size;
	};

	/// <summary>Provides access to the global mounting tree</summary>
	/// The global mounting tree is the default mounting tree used to resolve file requests made by
	/// code in this process. It can be thought of as similar to the file system namespace for the 
	/// current process in Linux.
	///
	/// File requests that can't be resolved by the mounting tree (eg, absolute paths and paths beginning
	/// with a drive name) are passed onto a default filesystem (which is typically just raw access to 
	/// the underlying OS filesystem).
	class MainFileSystem
	{
	public:
		using IOReason = IFileSystem::IOReason;

		static IOReason	TryOpen(std::unique_ptr<IFileInterface>& result, StringSection<utf8> filename, const char openMode[], OSServices::FileShareMode::BitField shareMode=FileShareMode_Default);
		static IOReason	TryOpen(OSServices::BasicFile& result, StringSection<utf8> filename, const char openMode[], OSServices::FileShareMode::BitField shareMode=FileShareMode_Default);
		static IOReason	TryOpen(OSServices::MemoryMappedFile& result, StringSection<utf8> filename, uint64_t size, const char openMode[], OSServices::FileShareMode::BitField shareMode=FileShareMode_Default);
		static IOReason	TryMonitor(FileSnapshot&, StringSection<utf8> filename, const std::shared_ptr<IFileMonitor>& evnt);
		static IOReason	TryFakeFileChange(StringSection<utf8> filename);
		static FileDesc	TryGetDesc(StringSection<utf8> filename);

		static OSServices::BasicFile OpenBasicFile(StringSection<utf8> filename, const char openMode[], OSServices::FileShareMode::BitField shareMode=FileShareMode_Default);
		static OSServices::MemoryMappedFile OpenMemoryMappedFile(StringSection<utf8> filename, uint64_t size, const char openMode[], OSServices::FileShareMode::BitField shareMode=FileShareMode_Default);
		static std::unique_ptr<IFileInterface> OpenFileInterface(StringSection<utf8> filename, const char openMode[], OSServices::FileShareMode::BitField shareMode=FileShareMode_Default);

		static IOReason	TryOpen(std::unique_ptr<IFileInterface>& result, StringSection<utf16> filename, const char openMode[], OSServices::FileShareMode::BitField shareMode=FileShareMode_Default);
		static IOReason	TryOpen(OSServices::BasicFile& result, StringSection<utf16> filename, const char openMode[], OSServices::FileShareMode::BitField shareMode=FileShareMode_Default);
		static IOReason	TryOpen(OSServices::MemoryMappedFile& result, StringSection<utf16> filename, uint64_t size, const char openMode[], OSServices::FileShareMode::BitField shareMode=FileShareMode_Default);
		static IOReason	TryMonitor(FileSnapshot&, StringSection<utf16> filename, const std::shared_ptr<IFileMonitor>& evnt);
		static IOReason	TryFakeFileChange(StringSection<utf16> filename);
		static FileDesc	TryGetDesc(StringSection<utf16> filename);

		static IFileSystem* GetFileSystem(FileSystemId id);
		static std::basic_string<utf8> GetMountPoint(FileSystemId id);

		static FileSystemWalker BeginWalk(StringSection<utf8> initialSubDirectory = "");

		static const std::shared_ptr<MountingTree>& GetMountingTree();
		static const std::shared_ptr<IFileSystem>& GetDefaultFileSystem();
		static void Init(const std::shared_ptr<MountingTree>& mountingTree, const std::shared_ptr<IFileSystem>& defaultFileSystem);
        static void Shutdown();

		static std::unique_ptr<uint8_t[]> TryLoadFileAsMemoryBlock(StringSection<char> sourceFileName, size_t* sizeResult = nullptr);
		static std::unique_ptr<uint8_t[]> TryLoadFileAsMemoryBlock(StringSection<char> sourceFileName, size_t* sizeResult, FileSnapshot* fileState);
		static Blob TryLoadFileAsBlob(StringSection<char> sourceFileName);
		static Blob TryLoadFileAsBlob(StringSection<char> sourceFileName, FileSnapshot* fileState);

		static std::unique_ptr<uint8_t[]> TryLoadFileAsMemoryBlock_TolerateSharingErrors(StringSection<char> sourceFileName, size_t* sizeResult);
		static std::unique_ptr<uint8_t[]> TryLoadFileAsMemoryBlock_TolerateSharingErrors(StringSection<char> sourceFileName, size_t* sizeResult, FileSnapshot* fileState);
		static Blob TryLoadFileAsBlob_TolerateSharingErrors(StringSection<char> sourceFileName);
		static Blob TryLoadFileAsBlob_TolerateSharingErrors(StringSection<char> sourceFileName, FileSnapshot* fileState);
	};

	T2(CharType, FileObject) IFileSystem::IOReason TryOpen(FileObject& result, IFileSystem& fs, StringSection<CharType> fn, const char openMode[], OSServices::FileShareMode::BitField shareMode=FileShareMode_Default);
	T2(CharType, FileObject) IFileSystem::IOReason TryOpen(FileObject& result, IFileSystem& fs, StringSection<CharType> fn, uint64_t size, const char openMode[], OSServices::FileShareMode::BitField shareMode=FileShareMode_Default);
	T1(CharType) IFileSystem::IOReason TryMonitor(IFileSystem& fs, FileSnapshot&, StringSection<CharType> fn, const std::shared_ptr<IFileMonitor>& evnt);
	T1(CharType) IFileSystem::IOReason TryFakeFileChange(IFileSystem& fs, StringSection<CharType> fn);
	T1(CharType) FileDesc TryGetDesc(IFileSystem& fs, StringSection<CharType> fn);
	FileSystemWalker BeginWalk(const std::shared_ptr<ISearchableFileSystem>& fs, StringSection<> initialSubDirectory = "");

	inline bool operator==(const FileSnapshot& lhs, const FileSnapshot& rhs)
	{
		return lhs._modificationTime == rhs._modificationTime && lhs._state == rhs._state;
	}

	inline bool operator<(const FileSnapshot& lhs, const FileSnapshot& rhs)
	{
		if (lhs._modificationTime < rhs._modificationTime) return true;
		if (lhs._modificationTime > rhs._modificationTime) return false;
		return (int)lhs._state < (int)rhs._state;
	}
}
