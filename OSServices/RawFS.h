// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../Core/Exceptions.h"
#include "../Core/Types.h"
#include "../Utility/StringUtils.h" // for StringSection
#include "../Utility/Optional.h"
#include "../Utility/IteratorUtils.h"
#include "../Utility/Streams/PathUtils.h"       // for s_defaultFilenameRules
#include <memory>       // for std::unique_ptr

#include <vector>
#include <string>
#include <functional>

namespace OSServices 
{
    #if PLATFORMOS_TARGET == PLATFORMOS_WINDOWS
		constexpr FilenameRules s_rawos_fileNameRules('/', false);
	#else
		constexpr FilenameRules s_rawos_fileNameRules('/', true);
	#endif

    namespace Exceptions
    {
        class IOException : public ::Exceptions::BasicLabel
        {
        public:
            /// <summary>A incomplete list of a few common file related errors</summary>
            /// Openning a file can result in a wide variety of possible errors. However, there
            /// are a few particularly common ones (like file not found, etc). This enum provides
            /// a way to quickly identify some of the common error types.
            enum class Reason
            {
                Success, FileNotFound,
                AccessDenied, WriteProtect,
				Mounting, Invalid,
				ExclusiveLock,				// locked for read and/or write exclusively by another process (ie, this is called "sharing violation" in the Win32 error codes)
                Complex
            };

			Reason GetReason() const { return _reason; }

            IOException(Reason reason, const char format[], ...) never_throws;
        private:
            Reason _reason;
        };
    }

	struct FileShareMode
	{
		enum { Read = 1<<0, Write = 1<<1 };
		typedef unsigned BitField;
	};

	using FileTime = uint64;

	enum class FileSeekAnchor { Start = SEEK_SET, Current = SEEK_CUR, End = SEEK_END };

        //
        //  "BasicFile" --  C++ wrapper for file interactions.
        //                  Prefer using BasicFile, instead of C-style interface functions 
        //
        //      Cannot be copied, but can be moved.
        //      Constructor will throw on file-system errors
        //      But other methods should not throw on errors
        //
    class BasicFile
    {
    public:
        size_t      Read(void *buffer, size_t size, size_t count) const never_throws;
        size_t      Write(const void *buffer, size_t size, size_t count) never_throws;
        size_t      Seek(size_t offset, FileSeekAnchor anchor = FileSeekAnchor::Start) never_throws;
        size_t      TellP() const never_throws;
        void        Flush() const never_throws;

        uint64      GetSize() const never_throws;
		bool		IsGood() const never_throws;
		FileTime	GetFileTime() const never_throws;

		Exceptions::IOException::Reason TryOpen(const utf8 filename[], const char openMode[], FileShareMode::BitField shareMode) never_throws;
		Exceptions::IOException::Reason TryOpen(const utf16 filename[], const char openMode[], FileShareMode::BitField shareMode) never_throws;
		BasicFile(const utf8 filename[], const char openMode[], FileShareMode::BitField shareMode);
		BasicFile(const utf16 filename[], const char openMode[], FileShareMode::BitField shareMode);

        BasicFile(BasicFile&& moveFrom) never_throws;
        BasicFile& operator=(BasicFile&& moveFrom) never_throws;
        BasicFile(const BasicFile& copyFrom) never_throws;
        BasicFile& operator=(const BasicFile& copyFrom) never_throws;
        BasicFile();
        ~BasicFile();

    protected:
        void* _file;
    };

    class MemoryMappedFile
    {
    public:
        IteratorRange<void*>		GetData()		{ return _data; }
		IteratorRange<const void*>	GetData() const	{ return _data; }
        bool            IsGood() const				{ return !_data.empty(); }
        size_t          GetSize() const				{ return _data.size(); }

		using CloseFn = std::function<void(IteratorRange<const void*>)>;

		Exceptions::IOException::Reason TryOpen(const utf8 filename[], uint64 size, const char openMode[], FileShareMode::BitField shareMode) never_throws;
		Exceptions::IOException::Reason TryOpen(const utf16 filename[], uint64 size, const char openMode[], FileShareMode::BitField shareMode) never_throws;
		MemoryMappedFile(
			const utf8 filename[], uint64 size, 
			const char openMode[],
			FileShareMode::BitField shareMode);
		MemoryMappedFile(
			const utf16 filename[], uint64 size, 
			const char openMode[],
			FileShareMode::BitField shareMode);

        MemoryMappedFile();
		MemoryMappedFile(IteratorRange<void*> data, CloseFn&& close);
        MemoryMappedFile(MemoryMappedFile&& moveFrom) never_throws;
        MemoryMappedFile& operator=(MemoryMappedFile&& moveFrom) never_throws;
        ~MemoryMappedFile();

	protected:
        IteratorRange<void*> _data;
		CloseFn _closeFn;
    };

	class FileAttributes
	{
	public:
		uint64_t _size;
		uint64_t _lastWriteTime;
		uint64_t _lastAccessTime;
	};

	bool DoesFileExist(StringSection<char> filename);
	bool DoesDirectoryExist(StringSection<char> filename);

	std::optional<FileAttributes> TryGetFileAttributes(const utf8 filename[]);
	std::optional<FileAttributes> TryGetFileAttributes(const utf16 filename[]);

	void CreateDirectoryRecursive(StringSection<utf8> filename);
	void CreateDirectoryRecursive(StringSection<utf16> filename);

	namespace FindFilesFilter
	{
		enum Enum 
		{
			File = 1<<0,
			Directory = 1<<1,
			All =  0xfffffffful
		};
		typedef unsigned BitField;
	}
	std::vector<std::string> FindFiles(const std::string& searchPath, FindFilesFilter::BitField filter = FindFilesFilter::All);
    void FindFiles(
        std::vector<uint64_t>& hashNames,
        const char searchPath[], FindFilesFilter::BitField filter = FindFilesFilter::All,
        const FilenameRules& hashingFnRules = s_defaultFilenameRules);

    bool GetCurrentDirectory(uint32_t dim, char dst[]);
    void ChDir(const utf8 path[]);

    void GetProcessPath(utf8 dst[], size_t bufferCount);
    void GetModulePath(utf8 dst[], size_t bufferCount, const utf8 moduleFilename[]);
	const char* GetCommandLine();
    using ModuleId = size_t;
    ModuleId GetCurrentModuleId();
    FileTime GetModuleFileTime();
    FileTime GetModuleFileTime(const utf8 moduleFilename[]);

    void DeleteFile(const utf8 path[]);
    void MoveFile(const utf8 destination[], const utf8 source[]);

    std::string SystemErrorCodeAsString(int errorCode);

    std::fstream CreateAppDataFile(StringSection<> appName, StringSection<> fileName, std::ios_base::openmode);
}
