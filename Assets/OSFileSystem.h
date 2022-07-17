// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../Utility/StringUtils.h"
#include "../Utility/UTFUtils.h"
#include <memory>

namespace OSServices { class PollingThread; }

namespace Assets
{
	class IFileSystem;

	namespace OSFileSystemFlags
	{
		enum Flags { AllowAbsolute = 1<<0, IgnorePaths = 1<<1 };
		using BitField = unsigned;
	}

	/**
	 * <summary>Create a mountable filesystem that reads and writes from the underlying OS filesystem</summary>
	 * Note that the polling thread is only required for filesystem monitoring; and so it not essential 
	**/
	std::shared_ptr<IFileSystem> CreateFileSystem_OS(
		StringSection<utf8> root = StringSection<utf8>(), 
		const std::shared_ptr<OSServices::PollingThread>& pollingThread = nullptr,
		OSFileSystemFlags::BitField flags = 0);
}
