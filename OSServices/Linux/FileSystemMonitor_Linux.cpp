// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "System_Linux.h"
#include "../FileSystemMonitor.h"
#include "../PollingThread.h"
#include "../Log.h"
#include "../../Utility/Streams/PathUtils.h"
#include "../../Utility/Threading/Mutex.h"
#include "../../Utility/Threading/LockFree.h"
#include "../../Utility/MemoryUtils.h"
#include "../../Utility/UTFUtils.h"
#include "../../Utility/IteratorUtils.h"
#include "../../Utility/Conversion.h"
#include "../../Core/Exceptions.h"
#include <vector>
#include <memory>
#include <cctype>

#include <sys/inotify.h>
#include <poll.h>
#include <unistd.h>

namespace OSServices
{
	class MonitoredDirectory
	{
	public:
		void            AttachCallback(uint64_t filenameHash, std::shared_ptr<OnChangeCallback> callback);
		void            OnChange(StringSection<> filename);
		int             wd() const { return _wd; }

		MonitoredDirectory(const std::string& directoryName, int wd);
		~MonitoredDirectory();
	private:
		std::vector<std::pair<uint64_t, std::weak_ptr<OnChangeCallback>>>  _callbacks;
		Threading::Mutex	_callbacksLock;

		std::string     _directoryName;
		int             _wd;
	};

	MonitoredDirectory::MonitoredDirectory(const std::string& directoryName, int wd)
	: _directoryName(directoryName), _wd(wd)
	{
	}

	MonitoredDirectory::~MonitoredDirectory()
	{
	}

	void MonitoredDirectory::AttachCallback(
		uint64_t filenameHash,
		std::shared_ptr<OnChangeCallback> callback)
	{
		ScopedLock(_callbacksLock);
		_callbacks.insert(
			LowerBound(_callbacks, filenameHash),
			std::make_pair(filenameHash, std::move(callback)));
	}

	void MonitoredDirectory::OnChange(StringSection<> filename)
	{
		auto hash = HashFilename(filename);
		ScopedLock(_callbacksLock);
		auto range = std::equal_range(
			_callbacks.begin(), _callbacks.end(),
			hash, CompareFirst<uint64_t, std::weak_ptr<OnChangeCallback>>());

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
					[](std::pair<uint64_t, std::weak_ptr<OnChangeCallback>>& i)
					{ return i.second.expired(); }),
				range.second);
		}
	}

	class RawFSMonitor::Pimpl
	{
	public:
		class Conduit : public IConduitProducer, public IConduitProducer_PlatformHandle
		{
		public:
			int _inotify_fd = -1;

			IOPlatformHandle GetPlatformHandle() const override { return (IOPlatformHandle)_inotify_fd; }
			PollingEventType::BitField GetListenTypes() const override { return PollingEventType::Input; }

			struct ChangeData
			{
				int _wd;
				std::string _name;
			};

			std::any GeneratePayload(PollingEventType::BitField triggeredEvents) override
			{
				std::vector<ChangeData> changedFiles;
				assert(triggeredEvents & PollingEventType::Input);
				char buf[4096] __attribute__ ((aligned(__alignof__(struct inotify_event))));
				for (;;) {
					/* Read some events. */
					auto len = read(_inotify_fd, buf, sizeof buf);
					if (len <= 0)
						break;

					const struct inotify_event *event;
					for (auto* ptr = buf; ptr < buf + len; ptr += sizeof(struct inotify_event) + event->len) {
						event = (const struct inotify_event *) ptr;
						changedFiles.push_back({event->wd, event->name});
					}
				}

				return changedFiles;
			}
		};

		class ConduitConsumer : public IConduitConsumer
		{
		public:
			Utility::Threading::Mutex _monitoredDirectoriesLock;
			std::vector<std::pair<uint64_t, std::unique_ptr<MonitoredDirectory>>> _monitoredDirectories;

			void OnEvent(std::any&& payload) override
			{
				auto& changedFiles = std::any_cast<const std::vector<Conduit::ChangeData>&>(payload);
				// Unfortunately our list of monitored directories and files isn't sorted in
				// a way to help us with the following lookup
				ScopedLock(_monitoredDirectoriesLock);
				for (const auto&change:changedFiles)
					for (const auto&m:_monitoredDirectories)
						if (m.second->wd() == change._wd)
							m.second->OnChange(change._name);		// note that we've got _monitoredDirectoriesLock locked while calling this
			}
			void OnException(const std::exception_ptr& exception) override
			{
				TRY
				{
					std::rethrow_exception(exception);
				}
				CATCH(const std::exception& e)
				{
					Log(Error) << "Raw file system monitoring cancelled because of exception: " << e.what() << std::endl;
				} 
				CATCH(...)
				{
					Log(Error) << "Raw file system monitoring cancelled because of unknown exception" << std::endl;
				}
				CATCH_END
			}
		};

		// Separating the conduit from the shared_ptr<> to PollingThread by making
		// the conduit another class. If the conduit has a reference to the polling thread
		// itself, we can end up with strange race conditions on shutdown where the polling
		// thread is releasing a (temporary) reference count on the conduit while that
		// conduiting holds the last reference to the PollingThread
		std::shared_ptr<PollingThread> _pollingThread;
		std::shared_ptr<Conduit> _conduit;
		std::shared_ptr<ConduitConsumer> _conduitConsumer;
	};

	void RawFSMonitor::Attach(
		StringSection<utf16> filename,
		std::shared_ptr<OnChangeCallback> callback)
	{
		assert(0);      // not implemented
	}

	void RawFSMonitor::Attach(
		StringSection<utf8> filename,
		std::shared_ptr<OnChangeCallback> callback)
	{
		bool startMonitoring = false;

		auto split = MakeFileNameSplitter(filename);
		utf8 directoryName[MaxPath];
		MakeSplitPath(split.StemAndPath()).Simplify().Rebuild(directoryName);
		auto hash = HashFilenameAndPath(MakeStringSection(directoryName));

		{
			ScopedLock(_pimpl->_conduitConsumer->_monitoredDirectoriesLock);			
			auto i = std::lower_bound(
				_pimpl->_conduitConsumer->_monitoredDirectories.cbegin(), _pimpl->_conduitConsumer->_monitoredDirectories.cend(),
				hash, CompareFirst<uint64_t, std::unique_ptr<MonitoredDirectory>>());
			if (i != _pimpl->_conduitConsumer->_monitoredDirectories.cend() && i->first == hash) {
				i->second->AttachCallback(HashFilename(split.FileAndExtension()), std::move(callback));
				return;
			}

			if (_pimpl->_conduit->_inotify_fd == -1) {
				_pimpl->_conduit->_inotify_fd = inotify_init1(IN_NONBLOCK);		// requires Linux 2.6.27
				assert(_pimpl->_conduit->_inotify_fd > 0);
				startMonitoring = true;
			}

			auto wd = inotify_add_watch(_pimpl->_conduit->_inotify_fd, directoryName, IN_CLOSE_WRITE | IN_CREATE | IN_DELETE | IN_MOVED_FROM | IN_MOVED_TO);
			assert(wd > 0);

			auto i2 = _pimpl->_conduitConsumer->_monitoredDirectories.insert(
				i, std::make_pair(hash, std::make_unique<MonitoredDirectory>(directoryName, wd)));
			i2->second->AttachCallback(HashFilename(split.FileAndExtension()), std::move(callback));
		}

		if (startMonitoring) {
			_pimpl->_pollingThread->Connect(_pimpl->_conduit, _pimpl->_conduitConsumer);
		}
	}

	void    RawFSMonitor::FakeFileChange(StringSection<utf16> filename)
	{
		assert(0);      // not implemented
	}

	void    RawFSMonitor::FakeFileChange(StringSection<utf8> filename)
	{
		auto split = MakeFileNameSplitter(filename);
		utf8 directoryName[MaxPath];
		MakeSplitPath(split.StemAndPath()).Simplify().Rebuild(directoryName);

		{
			ScopedLock(_pimpl->_conduitConsumer->_monitoredDirectoriesLock);
			auto hash = HashFilenameAndPath(MakeStringSection(directoryName));
			auto i = std::lower_bound(
				_pimpl->_conduitConsumer->_monitoredDirectories.cbegin(), _pimpl->_conduitConsumer->_monitoredDirectories.cend(),
				hash, CompareFirst<uint64_t, std::unique_ptr<MonitoredDirectory>>());
			if (i != _pimpl->_conduitConsumer->_monitoredDirectories.cend() && i->first == hash) {
				i->second->OnChange(split.FileAndExtension());
				return;
			}
		}
	}

	RawFSMonitor::RawFSMonitor(const std::shared_ptr<PollingThread>& pollingThread)
	{
		_pimpl = std::make_shared<Pimpl>();
		_pimpl->_conduit = std::make_shared<Pimpl::Conduit>();
		_pimpl->_conduitConsumer = std::make_shared<Pimpl::ConduitConsumer>();
		_pimpl->_pollingThread = pollingThread;
	}

	RawFSMonitor::~RawFSMonitor()
	{
		if (_pimpl->_conduit->_inotify_fd != -1) {
			_pimpl->_pollingThread->Disconnect(_pimpl->_conduit);
			close(_pimpl->_conduit->_inotify_fd);
		}
		_pimpl->_conduit.reset();
	}

}
