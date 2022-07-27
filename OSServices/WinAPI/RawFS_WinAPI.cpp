// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "../RawFS.h"
#include "../../Utility/Streams/PathUtils.h"
#include "../../Utility/StringUtils.h"
#include "../../Utility/Conversion.h"
#include "../../Utility/Optional.h"
#include <assert.h>
#include <utility>

#include "IncludeWindows.h"

namespace OSServices 
{
    static unsigned AsUnderlyingShareMode(FileShareMode::BitField shareMode)
    {
        unsigned underlyingShareMode = 0;
        if (shareMode & FileShareMode::Write)   { underlyingShareMode |= FILE_SHARE_WRITE; }
        if (shareMode & FileShareMode::Read)    { underlyingShareMode |= FILE_SHARE_READ; }
        return underlyingShareMode;
    }

    namespace Internal
    {
        struct UnderlyingOpenMode
        {
            unsigned _underlyingAccessMode;
            unsigned _creationDisposition;
            unsigned _underlyingFlags;
        };
    }

    static Exceptions::IOException::Reason AsExceptionReason(DWORD dw)
    {
        switch (dw) {
        case ERROR_FILE_NOT_FOUND:
        case ERROR_PATH_NOT_FOUND:
            return Exceptions::IOException::Reason::FileNotFound;

        case ERROR_INVALID_NAME:
        case ERROR_BAD_PATHNAME:
            return Exceptions::IOException::Reason::Invalid;

        case ERROR_ACCESS_DENIED:
            return Exceptions::IOException::Reason::AccessDenied;

        case ERROR_WRITE_PROTECT:
            return Exceptions::IOException::Reason::WriteProtect;

		case ERROR_SHARING_VIOLATION:
			return Exceptions::IOException::Reason::ExclusiveLock;

        default:
            return Exceptions::IOException::Reason::Complex;
        }
    }

    static Internal::UnderlyingOpenMode AsUnderlyingOpenMode(const char openMode[])
    {
        Internal::UnderlyingOpenMode result = {0,0,0};

        auto* i = openMode;
        while (*i != '\0') {
            switch (*i) {
            case 'w':
                if (*(i+1) == '+') {
                    ++i;
                    result._underlyingAccessMode = FILE_GENERIC_WRITE | FILE_GENERIC_READ;
                    result._creationDisposition = CREATE_ALWAYS;
                } else {
                    result._underlyingAccessMode = FILE_GENERIC_WRITE;
                    result._creationDisposition = CREATE_ALWAYS;
                }
                break;

            case 'r':
                if (*(i+1) == '+') {
                    ++i;
                    result._underlyingAccessMode = FILE_GENERIC_WRITE | FILE_GENERIC_READ;
                    result._creationDisposition = OPEN_EXISTING;
                } else {
                    result._underlyingAccessMode = FILE_GENERIC_READ;
                    result._creationDisposition = OPEN_EXISTING;
                }
                break;

            case 'b': break;    // binary mode -- actually the only supported mode

            case 'T':
            case 'D': result._underlyingFlags |= FILE_ATTRIBUTE_TEMPORARY; break;
            case 'R': result._underlyingFlags |= FILE_FLAG_RANDOM_ACCESS; break;
            case 'S': result._underlyingFlags |= FILE_FLAG_SEQUENTIAL_SCAN; break;
            
            case 'a':
                Throw(Exceptions::IOException(Exceptions::IOException::Reason::Complex, "Append file mode not supported"));

            case 't':
                Throw(Exceptions::IOException(Exceptions::IOException::Reason::Complex, "Text oriented file modes not supported"));

            case 'c':
                if (XlBeginsWith(MakeStringSection(i), MakeStringSection("ccs=")))
                    Throw(Exceptions::IOException(Exceptions::IOException::Reason::Complex, "Encoded text file modes supported"));
                // else, fall through...

            default:
                Throw(Exceptions::IOException(Exceptions::IOException::Reason::Complex, "Unknown characters found in open mode string (%s)", openMode));
            }
            ++i;
        }

        return result;
    }

	template<typename CharType>
		static void ThrowFileOpenException(StringSection<CharType> filename, const char openMode[])
	{
			// use "FormatMessage" to get error code
			//  (as per msdn article: "Retrieving the Last-Error Code")
		LPVOID lpMsgBuf;
		DWORD dw = GetLastError(); 
		FormatMessageA(
			FORMAT_MESSAGE_ALLOCATE_BUFFER | 
			FORMAT_MESSAGE_FROM_SYSTEM |
			FORMAT_MESSAGE_IGNORE_INSERTS,
			NULL,
			dw,
			MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
			(LPSTR) &lpMsgBuf,
			0, NULL);

			// FORMAT_MESSAGE_FROM_SYSTEM will typically give us a new line
			// at the end of the string. We can strip it off here (assuming we have write
			// access to the buffer). We're going to get rid of any terminating '.' as well.
			// Note -- assuming 8 bit base character width here (ie, ASCII, UTF8)
		if (lpMsgBuf) {
			auto *end = XlStringEnd((char*)lpMsgBuf);
			while ((end - 1) > lpMsgBuf && (*(end - 1) == '\n' || *(end - 1) == '\r' || *(end-1) == '.')) {
				--end;
				*end = '\0';
			}
		}

		Exceptions::IOException except(
			AsExceptionReason(dw),
			"Failure during file open. Probably missing file or bad privileges: (%s), openMode: (%s), error string: (%s)", 
			std::string(filename.begin(), filename.end()).c_str(), openMode, (const char*)lpMsgBuf);
		LocalFree(lpMsgBuf);

		Throw(except);
	}

	static void ThrowDuplicateHandleException()
	{
		// use "FormatMessage" to get error code
		//  (as per msdn article: "Retrieving the Last-Error Code")
		LPVOID lpMsgBuf;
		DWORD dw = GetLastError(); 
		FormatMessageA(
			FORMAT_MESSAGE_ALLOCATE_BUFFER | 
			FORMAT_MESSAGE_FROM_SYSTEM |
			FORMAT_MESSAGE_IGNORE_INSERTS,
			NULL,
			dw,
			MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
			(LPSTR) &lpMsgBuf,
			0, NULL);

		// FORMAT_MESSAGE_FROM_SYSTEM will typically give us a new line
		// at the end of the string. We can strip it off here (assuming we have write
		// access to the buffer). We're going to get rid of any terminating '.' as well.
		// Note -- assuming 8 bit base character width here (ie, ASCII, UTF8)
		if (lpMsgBuf) {
			auto *end = XlStringEnd((char*)lpMsgBuf);
			while ((end - 1) > lpMsgBuf && (*(end - 1) == '\n' || *(end - 1) == '\r' || *(end-1) == '.')) {
				--end;
				*end = '\0';
			}
		}

		Exceptions::IOException except(
			AsExceptionReason(dw), 
			"Failure while attempting to duplicate file handle. Error string: (%s)", (const char*)lpMsgBuf);
		LocalFree(lpMsgBuf);

		Throw(except);
	}

	BasicFile::BasicFile(   const utf8 filename[], const char openMode[], 
							FileShareMode::BitField shareMode)
	{
		assert(filename && filename[0]);
		assert(openMode);

		auto underlyingShareMode = AsUnderlyingShareMode(shareMode);
		auto underlyingOpenMode = AsUnderlyingOpenMode(openMode);

		auto handle = CreateFileA(
			(const char*)filename, 
			underlyingOpenMode._underlyingAccessMode,
			underlyingShareMode,
			nullptr, underlyingOpenMode._creationDisposition,
			underlyingOpenMode._underlyingFlags, nullptr);
	
		if (handle == INVALID_HANDLE_VALUE)
			ThrowFileOpenException(MakeStringSection(filename), openMode);

		_file = (void*)handle;
	}

	BasicFile::BasicFile(   const utf16 filename[], const char openMode[], 
							FileShareMode::BitField shareMode)
	{
		assert(filename && filename[0]);
		assert(openMode);

		auto underlyingShareMode = AsUnderlyingShareMode(shareMode);
		auto underlyingOpenMode = AsUnderlyingOpenMode(openMode);

		auto handle = CreateFileW(
			(const wchar_t*)filename, 
			underlyingOpenMode._underlyingAccessMode,
			underlyingShareMode,
			nullptr, underlyingOpenMode._creationDisposition,
			underlyingOpenMode._underlyingFlags, nullptr);

		if (handle == INVALID_HANDLE_VALUE)
			ThrowFileOpenException(MakeStringSection(filename), openMode);

		_file = (void*)handle;
	}

	auto BasicFile::TryOpen(const utf8 filename[], const char openMode[], FileShareMode::BitField shareMode) never_throws -> Exceptions::IOException::Reason
	{
		assert(_file == INVALID_HANDLE_VALUE);
		assert(filename && filename[0]);
		assert(openMode);

		auto underlyingShareMode = AsUnderlyingShareMode(shareMode);
		auto underlyingOpenMode = AsUnderlyingOpenMode(openMode);

		_file = CreateFileA(
			(const char*)filename, 
			underlyingOpenMode._underlyingAccessMode,
			underlyingShareMode,
			nullptr, underlyingOpenMode._creationDisposition,
			underlyingOpenMode._underlyingFlags, nullptr);

		if (_file != INVALID_HANDLE_VALUE && _file != nullptr)
			return Exceptions::IOException::Reason::Success;

		return AsExceptionReason(GetLastError());
	}

	auto BasicFile::TryOpen(const utf16 filename[], const char openMode[], FileShareMode::BitField shareMode) never_throws -> Exceptions::IOException::Reason
	{
		assert(_file == INVALID_HANDLE_VALUE);
		assert(filename && filename[0]);
		assert(openMode);

		auto underlyingShareMode = AsUnderlyingShareMode(shareMode);
		auto underlyingOpenMode = AsUnderlyingOpenMode(openMode);

		_file = CreateFileW(
			(const wchar_t*)filename, 
			underlyingOpenMode._underlyingAccessMode,
			underlyingShareMode,
			nullptr, underlyingOpenMode._creationDisposition,
			underlyingOpenMode._underlyingFlags, nullptr);

		if (_file != INVALID_HANDLE_VALUE && _file != nullptr)
			return Exceptions::IOException::Reason::Success;

		return AsExceptionReason(GetLastError());
	}

    BasicFile::BasicFile(BasicFile&& moveFrom) never_throws
    {
        _file = moveFrom._file;
        moveFrom._file = INVALID_HANDLE_VALUE;
    }

    BasicFile& BasicFile::operator=(BasicFile&& moveFrom) never_throws
    {
        if (_file != INVALID_HANDLE_VALUE) {
            CloseHandle(_file);
        }
        _file = moveFrom._file;
        moveFrom._file = INVALID_HANDLE_VALUE;
        return *this;
    }

    BasicFile::BasicFile(const BasicFile& copyFrom) never_throws
    {
        _file = INVALID_HANDLE_VALUE;
        auto currentProc = GetCurrentProcess();
        auto hresult = DuplicateHandle(
            currentProc, copyFrom._file,
            currentProc, &_file,
            0, FALSE,
            DUPLICATE_SAME_ACCESS);

        if (!SUCCEEDED(hresult))
			ThrowDuplicateHandleException();
    }

    BasicFile& BasicFile::operator=(const BasicFile& copyFrom) never_throws
    {
        if (_file != INVALID_HANDLE_VALUE)
            CloseHandle(_file);
        _file = INVALID_HANDLE_VALUE;
        auto currentProc = GetCurrentProcess();
        auto hresult = DuplicateHandle(
            currentProc, copyFrom._file,
            currentProc, &_file,
            0, FALSE,
            DUPLICATE_SAME_ACCESS);

        if (!SUCCEEDED(hresult))
			ThrowDuplicateHandleException();
        return *this;
    }

    BasicFile::BasicFile()
    {
        _file = INVALID_HANDLE_VALUE;
    }

    BasicFile::~BasicFile()
    {
        if (_file != INVALID_HANDLE_VALUE) {
            CloseHandle(_file);
        }
    }

    size_t   BasicFile::Read(void *buffer, size_t size, size_t count) const never_throws
    {
        if (!(size * count)) return 0;
        DWORD bytesRead = 0;
        auto result = ReadFile(_file, buffer, DWORD(size * count), &bytesRead, nullptr);
        auto errorCode = GetLastError();
        assert((bytesRead%size)==0); (void)errorCode;
        return result?(bytesRead/size):0;
    }
    
    size_t   BasicFile::Write(const void *buffer, size_t size, size_t count) never_throws
    {
        if (!(size * count)) return 0;
        DWORD bytesWritten = 0;
        auto result = WriteFile(_file, buffer, DWORD(size * count), &bytesWritten, nullptr);
        assert((bytesWritten%size)==0);
        return result?(bytesWritten/size):0;
    }

    size_t   BasicFile::Seek(size_t offset, FileSeekAnchor origin) never_throws
    {
        unsigned underlingMoveMethod = 0;
        switch (origin) {
		case FileSeekAnchor::Start:		underlingMoveMethod = FILE_BEGIN; break;
        case FileSeekAnchor::Current:	underlingMoveMethod = FILE_CURRENT; break;
        case FileSeekAnchor::End:		underlingMoveMethod = FILE_END; break;
        default: assert(0);
        }
		#if TARGET_64BIT
			LONG high = (LONG)(offset>>32ull);
			return SetFilePointer(_file, (LONG)offset, &high, underlingMoveMethod);
		#else
			return SetFilePointer(_file, offset, nullptr, underlingMoveMethod);
		#endif
    }

    size_t   BasicFile::TellP() const never_throws
    {
        return SetFilePointer(_file, 0, nullptr, FILE_CURRENT);
    }

    void    BasicFile::Flush() const never_throws
    {
        FlushFileBuffers(_file);
    }

    uint64      BasicFile::GetSize() const never_throws
    {
        if (_file == INVALID_HANDLE_VALUE) return 0;
        DWORD highWord = 0;
        auto lowWord = ::GetFileSize(_file, &highWord);
        return (uint64(highWord)<<32ull) | uint64(lowWord);
    }

	bool		BasicFile::IsGood() const never_throws
	{
		return _file != INVALID_HANDLE_VALUE;
	}

	static uint64 AsUInt64(FILETIME ft) { return (uint64(ft.dwHighDateTime) << 32ull) | uint64(ft.dwLowDateTime); }

	FileTime	BasicFile::GetFileTime() const never_throws
	{
		FILETIME ft;
		::GetFileTime(_file, nullptr, nullptr, &ft);
		return AsUInt64(ft);
	}

///////////////////////////////////////////////////////////////////////////////////////////////////

	bool DoesFileExist(StringSection<char> filename)
	{
		char buffer[MAX_PATH];
		XlCopyString(buffer, filename);	// copy to get the null terminator
		DWORD dwAttrib = GetFileAttributesA(buffer);
		return dwAttrib != INVALID_FILE_ATTRIBUTES && !(dwAttrib & FILE_ATTRIBUTE_DIRECTORY);
	}

	bool DoesDirectoryExist(StringSection<char> filename)
	{
		char buffer[MAX_PATH];
		XlCopyString(buffer, filename);	// copy to get the null terminator
		DWORD dwAttrib = GetFileAttributesA(buffer);
		return (dwAttrib != INVALID_FILE_ATTRIBUTES) && (dwAttrib & FILE_ATTRIBUTE_DIRECTORY);
	}

	void CreateDirectory_Int(const char* dn)    { CreateDirectoryA(dn, nullptr); }
	void CreateDirectory_Int(const wchar_t* dn) { CreateDirectoryW(dn, nullptr); }
	void CreateDirectory_Int(const utf16* dn)	{ CreateDirectoryW((const wchar_t*)dn, nullptr); }

	template<typename Char>
		void CreateDirectoryRecursive_Int(StringSection<Char> filename)
	{
				// note that because our input string may not have a null 
				// terminator at the very end, we have to copy at least
				// once... So might as well copy and then we can safely
				// modify the copy as we go through
		Char buffer[MaxPath];
		XlCopyString(buffer, filename);

		SplitPath<Char> split(buffer);
		for (const auto& section:split.GetSections()) {
			Char q = 0;
			std::swap(q, *const_cast<Char*>(section.end()));
			CreateDirectory_Int(buffer);
			std::swap(q, *const_cast<Char*>(section.end()));
		}
	}

	void CreateDirectoryRecursive(StringSection<utf8> filename)
	{
		CreateDirectoryRecursive_Int(filename);
	}

	void CreateDirectoryRecursive(StringSection<utf16> filename)
	{
		CreateDirectoryRecursive_Int(filename);
	}

	static FileAttributes AsFileAttributes(const WIN32_FILE_ATTRIBUTE_DATA& attribs)
	{
		return { uint64(attribs.nFileSizeHigh) << 32 | uint64(attribs.nFileSizeLow), AsUInt64(attribs.ftLastWriteTime), AsUInt64(attribs.ftLastAccessTime) };
	}

	std::optional<FileAttributes> TryGetFileAttributes(const utf8 filename[])
	{
		WIN32_FILE_ATTRIBUTE_DATA attribs;
		auto result = GetFileAttributesExA(
			(const char*)filename, 
			GetFileExInfoStandard,
			&attribs);
		return (result) ? AsFileAttributes(attribs) : std::optional<FileAttributes>{};
	}

	std::optional<FileAttributes> TryGetFileAttributes(const utf16 filename[])
	{
		WIN32_FILE_ATTRIBUTE_DATA attribs;
		auto result = GetFileAttributesExW(
			(const wchar_t*)filename, 
			GetFileExInfoStandard,
			&attribs);
		return (result) ? AsFileAttributes(attribs) : std::optional<FileAttributes>{};
	}

	std::vector<std::string> FindFiles(const std::string& searchPath, FindFilesFilter::BitField filter)
	{
		std::vector<std::string> result;

		char buffer[256];
		Legacy::XlDirname(buffer, dimof(buffer), searchPath.c_str());
		std::string basePath = buffer;
		if (!basePath.empty() && basePath[basePath.size()-1]!='/' && basePath[basePath.size()-1]!='\\') {
			basePath += "/";
		}
		WIN32_FIND_DATAA findData;
		memset(&findData, 0, sizeof(findData));
		HANDLE findHandle = FindFirstFileA(searchPath.c_str(), &findData);
		if (findHandle != INVALID_HANDLE_VALUE) {
			do {
				bool isDir = !!(findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY);
				if (filter & (1<<unsigned(isDir))) {
					result.push_back(basePath + findData.cFileName);
				}
			} while (FindNextFileA(findHandle, &findData));
			FindClose(findHandle);
		}

		return std::move(result);
	}

	static size_t GetFileSize(HANDLE fileHandle)
	{
		LARGE_INTEGER fileSize;
		GetFileSizeEx(fileHandle, &fileSize);
		return (size_t)fileSize.QuadPart;
	}

	Exceptions::IOException::Reason MemoryMappedFile::TryOpen(
		const utf8 filename[], uint64 size, const char openMode[], 
		FileShareMode::BitField shareMode) never_throws
	{
		assert(_data.empty() && !_closeFn);

		auto underlyingOpenMode = AsUnderlyingOpenMode(openMode);
		auto underlyingShareMode = AsUnderlyingShareMode(shareMode);

		if (underlyingOpenMode._underlyingAccessMode & FILE_WRITE_DATA)		// writable memory mapped files must have read access enabled, so the CreateFileMapping will succeed
			underlyingOpenMode._underlyingAccessMode |= FILE_GENERIC_READ;

		auto fileHandle = CreateFileA(
			(const char*)filename, underlyingOpenMode._underlyingAccessMode, 
			underlyingShareMode, 
			nullptr, underlyingOpenMode._creationDisposition, 
			underlyingOpenMode._underlyingFlags, nullptr);
		if (fileHandle == INVALID_HANDLE_VALUE)
			return AsExceptionReason(GetLastError());

		unsigned pageAccessMode = (underlyingOpenMode._underlyingAccessMode & FILE_WRITE_DATA) ? PAGE_READWRITE : PAGE_READONLY;
		auto mapping = CreateFileMappingA(
			fileHandle, nullptr, pageAccessMode, DWORD(size>>32), DWORD(size), nullptr);
		if (!mapping || mapping == INVALID_HANDLE_VALUE) {
			if (!mapping && (GetFileSize(fileHandle) == 0) && (!size || !(underlyingOpenMode._underlyingAccessMode & FILE_WRITE_DATA))) {
				// If we attempt to open a zero sized file in read mode (or with size 0 in write mode), we will get here
				// Let's not consider this an error, we'll just return with an empty result
				_closeFn = [fileHandle](IteratorRange<const void*>){ CloseHandle(fileHandle); };
				return Exceptions::IOException::Reason::Success;
			}
			CloseHandle(fileHandle);
			return AsExceptionReason(GetLastError());
		}

		unsigned mapAccess = (underlyingOpenMode._underlyingAccessMode & FILE_WRITE_DATA) ? FILE_MAP_WRITE : FILE_MAP_READ;
		auto mappingStart = MapViewOfFile(mapping, mapAccess, 0, 0, 0);
		if (!mappingStart) {
			CloseHandle(mapping);
			CloseHandle(fileHandle);
			return AsExceptionReason(GetLastError());
		}

		_data = MakeIteratorRange(mappingStart, PtrAdd(mappingStart, GetFileSize(fileHandle)));
		_closeFn = [fileHandle, mapping](IteratorRange<const void*> data)
			{
				assert(!data.empty());
				UnmapViewOfFile(data.begin());
				CloseHandle(mapping);
				CloseHandle(fileHandle);
			};
		return Exceptions::IOException::Reason::Success;
	}

	Exceptions::IOException::Reason MemoryMappedFile::TryOpen(
		const utf16 filename[], uint64 size, const char openMode[],
		FileShareMode::BitField shareMode) never_throws
	{
		assert(_data.empty() && !_closeFn);

		auto underlyingOpenMode = AsUnderlyingOpenMode(openMode);
		auto underlyingShareMode = AsUnderlyingShareMode(shareMode);

		if (underlyingOpenMode._underlyingAccessMode & FILE_WRITE_DATA)		// writable memory mapped files must have read access enabled, so the CreateFileMapping will succeed
			underlyingOpenMode._underlyingAccessMode |= FILE_GENERIC_READ;

		auto fileHandle = CreateFileW(
			(const wchar_t*)filename, underlyingOpenMode._underlyingAccessMode, 
			underlyingShareMode, 
			nullptr, underlyingOpenMode._creationDisposition, 
			underlyingOpenMode._underlyingFlags, nullptr);
		if (fileHandle == INVALID_HANDLE_VALUE)
			return AsExceptionReason(GetLastError());

		unsigned pageAccessMode = (underlyingOpenMode._underlyingAccessMode & FILE_WRITE_DATA) ? PAGE_READWRITE : PAGE_READONLY;
		auto mapping = CreateFileMappingA(
			fileHandle, nullptr, pageAccessMode, DWORD(size>>32), DWORD(size), nullptr);
		if (!mapping || mapping == INVALID_HANDLE_VALUE) {
			if (!mapping && (GetFileSize(fileHandle) == 0) && (!size || !(underlyingOpenMode._underlyingAccessMode & FILE_WRITE_DATA))) {
				// If we attempt to open a zero sized file in read mode (or with size 0 in write mode), we will get here
				// Let's not consider this an error, we'll just return with an empty result
				_closeFn = [fileHandle](IteratorRange<const void*>){ CloseHandle(fileHandle); };
				return Exceptions::IOException::Reason::Success;
			}
			CloseHandle(fileHandle);
			return AsExceptionReason(GetLastError());
		}

		unsigned mapAccess = (underlyingOpenMode._underlyingAccessMode & FILE_WRITE_DATA) ? FILE_MAP_WRITE : FILE_MAP_READ;
		auto mappingStart = MapViewOfFile(mapping, mapAccess, 0, 0, 0);
		if (!mappingStart) {
			CloseHandle(mapping);
			CloseHandle(fileHandle);
			return AsExceptionReason(GetLastError());
		}

		_data = MakeIteratorRange(mappingStart, PtrAdd(mappingStart, GetFileSize(fileHandle)));
		_closeFn = [fileHandle, mapping](IteratorRange<const void*> data)
			{
				assert(!data.empty());
				UnmapViewOfFile(data.begin());
				CloseHandle(mapping);
				CloseHandle(fileHandle);
			};
		return Exceptions::IOException::Reason::Success;
	}

	MemoryMappedFile::MemoryMappedFile(
		const utf8 filename[], uint64 size, const char openMode[], 
		FileShareMode::BitField shareMode)
	{
		auto underlyingOpenMode = AsUnderlyingOpenMode(openMode);
		auto underlyingShareMode = AsUnderlyingShareMode(shareMode);

		if (underlyingOpenMode._underlyingAccessMode & FILE_WRITE_DATA)		// writable memory mapped files must have read access enabled, so the CreateFileMapping will succeed
			underlyingOpenMode._underlyingAccessMode |= FILE_GENERIC_READ;

		auto fileHandle = CreateFileA(
			(const char*)filename, underlyingOpenMode._underlyingAccessMode, 
			underlyingShareMode, 
			nullptr, underlyingOpenMode._creationDisposition, 
			underlyingOpenMode._underlyingFlags, nullptr);
		if (fileHandle == INVALID_HANDLE_VALUE)
			ThrowFileOpenException(MakeStringSection(filename), openMode);

		unsigned pageAccessMode = (underlyingOpenMode._underlyingAccessMode & FILE_WRITE_DATA) ? PAGE_READWRITE : PAGE_READONLY;
		auto mapping = CreateFileMappingA(
			fileHandle, nullptr, pageAccessMode, DWORD(size>>32), DWORD(size), nullptr);
		if (!mapping || mapping == INVALID_HANDLE_VALUE) {
			CloseHandle(fileHandle);
			ThrowFileOpenException(MakeStringSection(filename), openMode);
		}

		unsigned mapAccess = (underlyingOpenMode._underlyingAccessMode & FILE_WRITE_DATA) ? FILE_MAP_WRITE : FILE_MAP_READ;
		auto mappingStart = MapViewOfFile(mapping, mapAccess, 0, 0, 0);
		if (!mappingStart) {
			CloseHandle(mapping);
			CloseHandle(fileHandle);
			ThrowFileOpenException(MakeStringSection(filename), openMode);
		}

		_data = MakeIteratorRange(mappingStart, PtrAdd(mappingStart, GetFileSize(fileHandle)));
		_closeFn = [fileHandle, mapping](IteratorRange<const void*> data)
			{
				assert(!data.empty());
				UnmapViewOfFile(data.begin());
				CloseHandle(mapping);
				CloseHandle(fileHandle);
			};
	}

	MemoryMappedFile::MemoryMappedFile(
		const utf16 filename[], uint64 size, const char openMode[],
		FileShareMode::BitField shareMode)
	{
		auto underlyingOpenMode = AsUnderlyingOpenMode(openMode);
		auto underlyingShareMode = AsUnderlyingShareMode(shareMode);

		if (underlyingOpenMode._underlyingAccessMode & FILE_WRITE_DATA)		// writable memory mapped files must have read access enabled, so the CreateFileMapping will succeed
			underlyingOpenMode._underlyingAccessMode |= FILE_GENERIC_READ;

		auto fileHandle = CreateFileW(
			(const wchar_t*)filename, underlyingOpenMode._underlyingAccessMode, 
			underlyingShareMode, 
			nullptr, underlyingOpenMode._creationDisposition, 
			underlyingOpenMode._underlyingFlags, nullptr);
		if (fileHandle == INVALID_HANDLE_VALUE)
			ThrowFileOpenException(MakeStringSection(filename), openMode);

		unsigned pageAccessMode = (underlyingOpenMode._underlyingAccessMode & FILE_WRITE_DATA) ? PAGE_READWRITE : PAGE_READONLY;
		auto mapping = CreateFileMappingA(
			fileHandle, nullptr, pageAccessMode, DWORD(size>>32), DWORD(size), nullptr);
		if (!mapping || mapping == INVALID_HANDLE_VALUE) {
			CloseHandle(fileHandle);
			ThrowFileOpenException(MakeStringSection(filename), openMode);
		}

		unsigned mapAccess = (underlyingOpenMode._underlyingAccessMode & FILE_WRITE_DATA) ? FILE_MAP_WRITE : FILE_MAP_READ;
		auto mappingStart = MapViewOfFile(mapping, mapAccess, 0, 0, 0);
		if (!mappingStart) {
			CloseHandle(mapping);
			CloseHandle(fileHandle);
			ThrowFileOpenException(MakeStringSection(filename), openMode);
		}

		_data = MakeIteratorRange(mappingStart, PtrAdd(mappingStart, GetFileSize(fileHandle)));
		_closeFn = [fileHandle, mapping](IteratorRange<const void*> data)
			{
				assert(!data.empty());
				UnmapViewOfFile(data.begin());
				CloseHandle(mapping);
				CloseHandle(fileHandle);
			};
	}
}

