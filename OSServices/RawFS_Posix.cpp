#include "RawFS.h"
#include "../Utility/Streams/PathUtils.h"
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>
#include <unordered_map>

// Sadly, Android does not include glob
#if PLATFORMOS_TARGET != PLATFORMOS_ANDROID
#include <glob.h>
#endif

namespace OSServices 
{
	bool DoesFileExist(StringSection<char> filename)
	{
		assert(0);
		return false;
	}

	bool DoesDirectoryExist(StringSection<char> filename)
	{
		assert(0);
		return false;
	}

	void CreateDirectory_Int(const utf8* dn)    
	{ 
#ifdef __MINGW32__
		auto result = mkdir(dn);
#else
		auto result = mkdir(dn, 0775);
#endif
		(void)result;
		// assert(result == 0);
	}
	void CreateDirectory_Int(const wchar_t* dn) { assert(0); }
	void CreateDirectory_Int(const utf16* dn)	{ assert(0); }

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

	void CreateDirectoryRecursive(const StringSection<utf8> filename)
	{
		CreateDirectoryRecursive_Int(filename);
	}

	void CreateDirectoryRecursive(const StringSection<utf16> filename)
	{
		CreateDirectoryRecursive_Int(filename);
	}

	static FileAttributes AsFileAttributes(const struct stat& fileData)
	{
		return { (uint64_t)fileData.st_size, (uint64_t)fileData.st_mtime, (uint64_t)fileData.st_ctime };
	}

	std::optional<FileAttributes> TryGetFileAttributes(const utf8 filename[])
	{
		struct stat fdata;
		auto result = stat((const char*)filename, &fdata);
		if (result!=0) return {};
		return AsFileAttributes(fdata);
	}

	std::optional<FileAttributes> TryGetFileAttributes(const utf16 filename[])
	{
		assert(0);
		return {};
	}

	std::vector<std::string> FindFiles(const std::string& searchPath, FindFilesFilter::BitField filter)
	{
		std::vector<std::string> fileList;

		DIR *dir = opendir(searchPath.c_str());
		if (dir != NULL) {
			struct dirent *entry;
			while ((entry = readdir(dir)) != NULL) {
				if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
					continue;
				}
				if ((entry->d_type == DT_DIR) && (filter & FindFilesFilter::Directory)) {
					fileList.push_back(entry->d_name);
				}

				if ((entry->d_type == DT_REG) && (filter & FindFilesFilter::File)) {
					fileList.push_back(entry->d_name);
				}
			}
			closedir(dir);
		}

		return fileList;
	}
}

