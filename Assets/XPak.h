// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../Utility/StringUtils.h"
#include <memory>

namespace OSServices { using FileTime = uint64_t; }

namespace Assets
{
	class IFileSystem;
	namespace ArchiveUtility { class FileCache; }
	std::shared_ptr<IFileSystem> CreateXPakFileSystem(StringSection<> archive, std::shared_ptr<ArchiveUtility::FileCache>);
	std::shared_ptr<ArchiveUtility::FileCache> CreateFileCache(size_t sizeInBytes);

	std::shared_ptr<IFileSystem> CreateXPakFileSystem(IteratorRange<const void*> embeddedData, OSServices::FileTime, std::shared_ptr<ArchiveUtility::FileCache>);
}

