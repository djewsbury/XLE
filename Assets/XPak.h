// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../Utility/StringUtils.h"
#include <memory>

namespace Assets
{
	class IFileSystem;
	namespace ArchiveUtility { class FileCache; }
	std::shared_ptr<IFileSystem> CreateXPakFileSystem(StringSection<> archive, std::shared_ptr<ArchiveUtility::FileCache>);
	std::shared_ptr<ArchiveUtility::FileCache> CreateFileCache(size_t sizeInBytes);
}

