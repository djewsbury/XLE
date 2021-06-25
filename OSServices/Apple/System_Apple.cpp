// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "System_Apple.h"
#include "../RawFS.h"
#include "../TimeUtils.h"
#include "../FileSystemMonitor.h"
#include "../../Utility/StringUtils.h"
#include "../../Utility/Threading/Mutex.h"
#include "../../Utility/Streams/PathUtils.h"
#include "../../Core/SelectConfiguration.h"
#include "../../Core/Types.h"

#include <cstdio>
#include <mach/mach_time.h>
#include <mach-o/dyld.h>
#include <pthread/pthread.h>
#include <libgen.h>
#include <dirent.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/event.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <limits.h>

#include <unordered_map>
#include <unordered_set>
#include <thread>

namespace OSServices
{
	uint64 GetPerformanceCounter()
	{
		return mach_absolute_time();
	}
	
	uint64 GetPerformanceCounterFrequency()
	{
		mach_timebase_info_data_t tbInfo;
		mach_timebase_info(&tbInfo);
		return tbInfo.denom * 1000000000 / tbInfo.numer;
	}

	namespace Internal
	{
		class DirectoryChanges : public KEvent
		{
		public:
			std::vector<std::string> FindChanges()
			{
				ScopedLock(_cacheLock);

				// We know that something within the directory changed, we
				// just don't know exactly what.
				std::vector<std::string> filesCurrentlyInDir;
				// note -- don't use fdopendir(_fd) here, because that will tend take control of our _fd, and it
				// becomes undefined if we do something else with the _fd (such as watching it for notifications)
				// It seems to work, but might not be the most reliable option
				DIR *dir = opendir(_dirName.c_str());	
				if (dir != NULL) {
					rewinddir(dir);
					struct dirent *entry;
					while ((entry = readdir(dir)) != NULL)
						if (entry->d_type == DT_REG)
							filesCurrentlyInDir.push_back(entry->d_name);
				}
				closedir(dir);

				// This method is actually pretty awkward, because we can't do the next steps in
				// an atomic fashion. We know that something in the directory has changed, but we don't
				// know what. The only way to check is to iterate through all items and compare it to
				// our previous results.
				// But the directory could continue to be changed while we're doing this. Or, alternatively,
				// if there are 2 quick changes, we can end up skipping one
				// But when we do it like this, it matches behaviour on the other platforms much better...
				// which ultimately might reduce platform specific issues

				std::sort(filesCurrentlyInDir.begin(), filesCurrentlyInDir.end());

				auto cacheIterator = _statusCache.begin();
				auto newDirIterator = filesCurrentlyInDir.begin();
				std::vector<std::string> changedFiles;
				for (;;) {
					auto skipI = cacheIterator;
					while (skipI != _statusCache.end() && (newDirIterator == filesCurrentlyInDir.end() || skipI->_name < *newDirIterator)) {
						changedFiles.push_back(skipI->_name);
						++skipI;
					}
					if (cacheIterator != skipI)
						cacheIterator = _statusCache.erase(cacheIterator, skipI);

					while (newDirIterator != filesCurrentlyInDir.end() && (cacheIterator == _statusCache.end() || *newDirIterator < cacheIterator->_name)) {
						struct stat fdata;
						std::memset(&fdata, 0, sizeof(fdata));
						auto result = fstatat(_fd, newDirIterator->c_str(), &fdata, 0);
						if (result == 0) {
							cacheIterator = 1+_statusCache.insert(cacheIterator, { *newDirIterator, (uint64_t)fdata.st_mtimespec.tv_sec });							
							changedFiles.push_back(*newDirIterator);
						}
						++newDirIterator;
					}

					if (cacheIterator == _statusCache.end() && newDirIterator == filesCurrentlyInDir.end())
						break;

					assert(*newDirIterator == cacheIterator->_name);

					struct stat fdata;
					std::memset(&fdata, 0, sizeof(fdata));
					auto result = fstatat(_fd, newDirIterator->c_str(), &fdata, 0);
					if (result == 0 && cacheIterator->_lastModTime != (uint64_t)fdata.st_mtimespec.tv_sec) {
						changedFiles.push_back(*newDirIterator);
					}
					++cacheIterator;
					++newDirIterator;
				}

				return changedFiles;
			}

			std::any GeneratePayload(const KEventTriggerPayload&) override
			{
				return FindChanges();
			}

			DirectoryChanges(const char* dirName) : _dirName(dirName)
			{
				_fd = open(dirName, O_EVTONLY);
				
				// Setup the KEvent to monitor this directory
				_ident = _fd;
				_filter = EVFILT_VNODE;
				_fflags = NOTE_WRITE | NOTE_DELETE | NOTE_RENAME;

				// prime initial state
				FindChanges();
			}

			~DirectoryChanges()
			{
				close(_fd);
			}
		private:
			int _fd = -1;
			std::string _dirName;

			Threading::Mutex _cacheLock;
			struct CachedFileStatus { std::string _name; uint64_t _lastModTime; };
			std::vector<CachedFileStatus> _statusCache;
		};

		class MonitoredDirectory : public IConduitConsumer
		{
		public:
			virtual void OnEvent(std::any&& payload)
			{
				auto& changes = std::any_cast<const std::vector<std::string>&>(payload);
				for (auto c:changes)
					OnChange(MakeStringSection(c));
			}

			virtual void OnException(const std::exception_ptr& exception)
			{}

			void OnChange(StringSection<> filename)
			{
				auto hash = HashFilename(filename);
				ScopedLock(_callbacksLock);
				auto range = std::equal_range(
					_callbacks.begin(), _callbacks.end(),
					hash, CompareFirst<uint64, std::weak_ptr<OnChangeCallback>>());

				bool foundExpired = false;
				for (auto i2=range.first; i2!=range.second; ++i2) {
						// todo -- what happens if OnChange() results in a change to _callbacks?
					auto l = i2->second.lock();
					if (l) l->OnChange();
					else foundExpired = true;
				}

				if (foundExpired) {
						// Remove any pointers that have expired
						// (note that we only check matching pointers. Non-matching pointers
						// that have expired are untouched)
					_callbacks.erase(
						std::remove_if(range.first, range.second,
							[](std::pair<uint64, std::weak_ptr<OnChangeCallback>>& i)
							{ return i.second.expired(); }),
						range.second);
				}
			}
			
			void AttachCallback(
				uint64_t filenameHash,
				std::shared_ptr<OnChangeCallback> callback)
			{
				ScopedLock(_callbacksLock);
				_callbacks.insert(
					LowerBound(_callbacks, filenameHash),
					std::make_pair(filenameHash, std::move(callback)));
			}

			MonitoredDirectory()
			{}

			~MonitoredDirectory()
			{}

		private:
			std::vector<std::pair<uint64_t, std::weak_ptr<OnChangeCallback>>>  _callbacks;
			Threading::Mutex	_callbacksLock;
		};
	}

	class RawFSMonitor::Pimpl
	{
	public:
		std::shared_ptr<PollingThread> _pollingThread;
		Threading::Mutex _monitoredDirectoriesLock;
		std::vector<std::pair<uint64_t, std::shared_ptr<Internal::MonitoredDirectory>>> _monitoredDirectories;
	};
	
	void RawFSMonitor::Attach(StringSection<utf16>, std::shared_ptr<OnChangeCallback>)
	{
		assert(0);
	}

	void RawFSMonitor::Attach(StringSection<utf8> filename, std::shared_ptr<OnChangeCallback> callback)
	{
		auto split = MakeFileNameSplitter(filename);
		utf8 directoryName[MaxPath];
		MakeSplitPath(split.DriveAndPath()).Simplify().Rebuild(directoryName);
		auto hash = HashFilenameAndPath(MakeStringSection(directoryName));

		{
			ScopedLock(_pimpl->_monitoredDirectoriesLock);
			auto i = LowerBound(_pimpl->_monitoredDirectories, hash);
			if (i != _pimpl->_monitoredDirectories.cend() && i->first == hash) {
				i->second->AttachCallback(HashFilename(split.FileAndExtension()), std::move(callback));
				return;
			}
			
			auto newMonitoredDirectory = std::make_shared<Internal::MonitoredDirectory>();
			newMonitoredDirectory->AttachCallback(HashFilename(split.FileAndExtension()), std::move(callback));

			auto newConduitProducer = std::make_shared<Internal::DirectoryChanges>(directoryName);

			auto connectionFuture = _pimpl->_pollingThread->Connect(
				newConduitProducer,
				newMonitoredDirectory);

			_pimpl->_monitoredDirectories.insert(i, std::make_pair(hash, newMonitoredDirectory));
		}
	}

	void RawFSMonitor::FakeFileChange(StringSection<utf16>)
	{
		assert(0);
	}

	void RawFSMonitor::FakeFileChange(StringSection<utf8> filename)
	{
		auto split = MakeFileNameSplitter(filename);
		utf8 directoryName[MaxPath];
		MakeSplitPath(split.DriveAndPath()).Simplify().Rebuild(directoryName);
		auto hash = HashFilenameAndPath(MakeStringSection(directoryName));

		{
			ScopedLock(_pimpl->_monitoredDirectoriesLock);
			auto i = LowerBound(_pimpl->_monitoredDirectories, hash);
			if (i != _pimpl->_monitoredDirectories.cend() && i->first == hash) {
				i->second->OnChange(split.FileAndExtension());
				return;
			}
		}
	}

	RawFSMonitor::RawFSMonitor(const std::shared_ptr<PollingThread>& pollingThread)
	{
		_pimpl = std::make_unique<Pimpl>();
		_pimpl->_pollingThread = pollingThread;
	}
	RawFSMonitor::~RawFSMonitor() {}
}

namespace OSServices
{
	bool GetCurrentDirectory(uint32_t dim, char dst[])
	{
		assert(0);
		if (dim > 0) dst[0] = '\0';
		return false;
	}

	void GetProcessPath(utf8 dst[], size_t bufferCount)
	{
		if (!bufferCount) return;

		uint32_t bufsize = bufferCount;
		if (_NSGetExecutablePath(dst, &bufsize) != 0)
			dst[0] = '\0';
	}

	void ChDir(const utf8 path[]) {}

	const char* GetCommandLine() { return ""; }

	ModuleId GetCurrentModuleId() { return 0; }

	void DeleteFile(const utf8 path[])
	{
		std::remove(path);
	}

	FileTime GetModuleFileTime()
	{
		assert(0);
		return 0;
	}

}
