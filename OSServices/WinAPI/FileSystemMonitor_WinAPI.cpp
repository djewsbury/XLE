// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "System_WinAPI.h"
#include "IncludeWindows.h"
#include "../FileSystemMonitor.h"
#include "../PollingThread.h"
#include "../Log.h"
#include "../../Utility/Streams/PathUtils.h"
#include "../../Core/Prefix.h"
#include "../../Core/Types.h"
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
#include <thread>
#include <any>

// Character set note -- we're using utf16 characters in this file. We're going to
// assume that Windows is working with utf16 as well (as opposed to ucs2). This windows documentation
// suggest that it is expecting utf16 behaviour... But maybe there seems to be a lack of clarity as
// to how complete that support is.

namespace OSServices
{
	class MonitorDirectoryConduit : public IConduitProducer_CompletionRoutine
	{
	public:
		struct ConduitResult
		{
			std::vector<std::u16string> _changedFiles;
		};

		void BeginOperation(OVERLAPPED* overlapped, ConduitCompletionRoutine completionRoutine) override
		{
			assert(!_cancelled);
			if (_directoryHandle == INVALID_HANDLE_VALUE) {
				_directoryHandle = CreateFileW(
					(LPCWSTR)_directoryName.c_str(), FILE_LIST_DIRECTORY,
					FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,
					nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OVERLAPPED, nullptr);
			}

			if (_directoryHandle != INVALID_HANDLE_VALUE) {
				_bytesReturned = 0;
				XlSetMemory(_resultBuffer, dimof(_resultBuffer), 0);

				auto hresult = ReadDirectoryChangesW(
					_directoryHandle, _resultBuffer, sizeof(_resultBuffer),
					FALSE, FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_FILE_NAME,
					nullptr, overlapped, completionRoutine);
				if (!hresult) {
					auto errorAsString = SystemErrorCodeAsString(GetLastError());
					Throw(std::runtime_error("ReadDirectoryChangesW failed with the error: " + errorAsString));
				}
			}
		}

		CancelOperationType CancelOperation(OVERLAPPED* overlapped) override
		{
			_cancelled = true;
			if (_directoryHandle != INVALID_HANDLE_VALUE) {
				#if _WIN32_WINNT >= _WIN32_WINNT_VISTA
					CancelIoEx(_directoryHandle, overlapped);
				#endif
				CloseHandle(_directoryHandle);
				_directoryHandle = INVALID_HANDLE_VALUE;
				return CancelOperationType::CancelIoWasCalled;
			}
			return CancelOperationType::ImmediateCancel;
		}

		std::any GeneratePayload(unsigned numberOfBytesReturned) override
		{
			assert(!_cancelled);
			assert(numberOfBytesReturned != 0);
			FILE_NOTIFY_INFORMATION* notifyInformation = 
				(FILE_NOTIFY_INFORMATION*)_resultBuffer;
			auto* end = PtrAdd(notifyInformation, numberOfBytesReturned);
			ConduitResult results;
			while (notifyInformation < end) {
					//  Most editors just change the last write date when a file changes
					//  But some (like Visual Studio) will actually rename and replace the
					//  file (for error safety). To catch these cases, we need to look for
					//  file renames (also creation/deletion events would be useful)
				if (    notifyInformation->Action == FILE_ACTION_MODIFIED
					||  notifyInformation->Action == FILE_ACTION_ADDED
					||  notifyInformation->Action == FILE_ACTION_REMOVED
					||  notifyInformation->Action == FILE_ACTION_RENAMED_OLD_NAME
					||  notifyInformation->Action == FILE_ACTION_RENAMED_NEW_NAME) {
					results._changedFiles.push_back({
						(const utf16*)notifyInformation->FileName,
						(const utf16*)PtrAdd(notifyInformation->FileName, notifyInformation->FileNameLength)});
				}

				if (!notifyInformation->NextEntryOffset) {
					break;
				}
				notifyInformation = PtrAdd(notifyInformation, notifyInformation->NextEntryOffset);
			}

			assert(!results._changedFiles.empty());
			return results;
		}

		MonitorDirectoryConduit(const std::basic_string<utf16>& directoryName)
		: _directoryName(directoryName)
		, _cancelled(false)
		{
			_directoryHandle = INVALID_HANDLE_VALUE;
			_bytesReturned = 0;
			std::memset(_resultBuffer, 0, dimof(_resultBuffer));
		}

		~MonitorDirectoryConduit()
		{
			_cancelled = true;
			if (_directoryHandle != INVALID_HANDLE_VALUE)
				CloseHandle(_directoryHandle);
		}

		MonitorDirectoryConduit(const MonitorDirectoryConduit&) = delete;
		MonitorDirectoryConduit& operator=(const MonitorDirectoryConduit&) = delete;

	private:
		std::basic_string<utf16>	_directoryName;
		XlHandle			_directoryHandle;
		uint8				_resultBuffer[1024];
		DWORD				_bytesReturned;
		bool				_cancelled;
	};

	class MonitoredDirectory : public IConduitConsumer
	{
	public:
		MonitoredDirectory();
		~MonitoredDirectory();

		void AttachCallback(uint64 filenameHash, std::shared_ptr<OnChangeCallback> callback);

		void OnEvent(std::any&& payload) override;
		void OnException(const std::exception_ptr& exception) override;

		void OnChange(uint64_t filenameHash);
	private:
		Threading::Mutex	_callbacksLock;
		std::vector<std::pair<uint64, std::weak_ptr<OnChangeCallback>>>  _callbacks;
	};

	MonitoredDirectory::MonitoredDirectory()
	{
	}

	MonitoredDirectory::~MonitoredDirectory()
	{
	}
	
	void MonitoredDirectory::AttachCallback(
		uint64 filenameHash, 
		std::shared_ptr<OnChangeCallback> callback)
	{
		ScopedLock(_callbacksLock);
		_callbacks.insert(
			LowerBound(_callbacks, filenameHash),
			std::make_pair(filenameHash, std::move(callback)));
	}

	void MonitoredDirectory::OnEvent(std::any&& inputData)
	{
		const auto& conduitResult = std::any_cast<const MonitorDirectoryConduit::ConduitResult&>(inputData);
		for (const auto&f:conduitResult._changedFiles)
			OnChange(HashFilename(MakeStringSection(f)));
	}

	void MonitoredDirectory::OnException(const std::exception_ptr& exception)
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

	void MonitoredDirectory::OnChange(uint64_t filenameHash)
	{
		ScopedLock(_callbacksLock);
		auto range = std::equal_range(
			_callbacks.begin(), _callbacks.end(), 
			filenameHash, CompareFirst<uint64, std::weak_ptr<OnChangeCallback>>());

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

	class RawFSMonitor::Pimpl
	{
	public:
		std::shared_ptr<PollingThread> _pollingThread;

		Utility::Threading::Mutex _monitoredDirectoriesLock;
		struct DirAndConduit
		{
			std::shared_ptr<MonitoredDirectory> _monitoredDirectory;
			std::shared_ptr<MonitorDirectoryConduit> _conduit;
		};
		std::vector<std::pair<uint64_t, DirAndConduit>> _monitoredDirectories;
	};

	void    RawFSMonitor::Attach(StringSection<utf8> filenameWithPath, std::shared_ptr<OnChangeCallback> callback) 
	{
		auto splitter = MakeFileNameSplitter(filenameWithPath);
		auto directoryName = splitter.DriveAndPath();
		auto dirHash = HashFilenameAndPath(directoryName);

		ScopedLock(_pimpl->_monitoredDirectoriesLock);
		auto i = LowerBound(_pimpl->_monitoredDirectories, dirHash);
		if (i != _pimpl->_monitoredDirectories.cend() && i->first == dirHash) {
			i->second._monitoredDirectory->AttachCallback(HashFilename(splitter.FileAndExtension()), std::move(callback));
			return;
		}

		Pimpl::DirAndConduit newDir;
		newDir._monitoredDirectory = std::make_unique<MonitoredDirectory>();
		newDir._monitoredDirectory->AttachCallback(HashFilename(splitter.FileAndExtension()), std::move(callback));
		newDir._conduit = std::make_shared<MonitorDirectoryConduit>(Conversion::Convert<std::u16string>(directoryName));
		_pimpl->_pollingThread->Connect(newDir._conduit, newDir._monitoredDirectory);
		_pimpl->_monitoredDirectories.insert(i, std::make_pair(dirHash, std::move(newDir)));
	}

	void    RawFSMonitor::Attach(StringSection<utf16> filenameWithPath, std::shared_ptr<OnChangeCallback> callback)
	{
		auto splitter = MakeFileNameSplitter(filenameWithPath);
		auto directoryName = splitter.DriveAndPath();
		auto dirHash = HashFilenameAndPath(directoryName);

		ScopedLock(_pimpl->_monitoredDirectoriesLock);
		auto i = LowerBound(_pimpl->_monitoredDirectories, dirHash);
		if (i != _pimpl->_monitoredDirectories.cend() && i->first == dirHash) {
			i->second._monitoredDirectory->AttachCallback(HashFilename(splitter.FileAndExtension()), std::move(callback));
			return;
		}

		Pimpl::DirAndConduit newDir;
		newDir._monitoredDirectory = std::make_unique<MonitoredDirectory>();
		newDir._monitoredDirectory->AttachCallback(HashFilename(splitter.FileAndExtension()), std::move(callback));
		newDir._conduit = std::make_shared<MonitorDirectoryConduit>(directoryName.AsString());
		_pimpl->_pollingThread->Connect(newDir._conduit, newDir._monitoredDirectory);
		_pimpl->_monitoredDirectories.insert(i, std::make_pair(dirHash, std::move(newDir)));
	}

	void    RawFSMonitor::FakeFileChange(StringSection<utf8> filenameWithPath)
	{
		auto splitter = MakeFileNameSplitter(filenameWithPath);
		auto directoryName = splitter.DriveAndPath();
		auto dirHash = HashFilenameAndPath(directoryName);

		ScopedLock(_pimpl->_monitoredDirectoriesLock);
		auto i = LowerBound(_pimpl->_monitoredDirectories, dirHash);
		if (i != _pimpl->_monitoredDirectories.cend() && i->first == dirHash)
			i->second._monitoredDirectory->OnChange(HashFilename(splitter.FileAndExtension()));
	}

	void    RawFSMonitor::FakeFileChange(StringSection<utf16> filenameWithPath) 
	{
		auto splitter = MakeFileNameSplitter(filenameWithPath);
		auto directoryName = splitter.DriveAndPath();
		auto dirHash = HashFilenameAndPath(directoryName);

		ScopedLock(_pimpl->_monitoredDirectoriesLock);
		auto i = LowerBound(_pimpl->_monitoredDirectories, dirHash);
		if (i != _pimpl->_monitoredDirectories.cend() && i->first == dirHash)
			i->second._monitoredDirectory->OnChange(HashFilename(splitter.FileAndExtension()));
	}

	RawFSMonitor::RawFSMonitor(const std::shared_ptr<PollingThread>& pollingThread)
	{
		_pimpl = std::make_shared<Pimpl>();
		_pimpl->_pollingThread = pollingThread;
	}

	RawFSMonitor::~RawFSMonitor()
	{
		std::vector<std::future<void>> futuresToWaitOn;
		{
			ScopedLock(_pimpl->_monitoredDirectoriesLock);
			futuresToWaitOn.reserve(_pimpl->_monitoredDirectories.size());
			for (const auto& dir:_pimpl->_monitoredDirectories)
				futuresToWaitOn.push_back(
					_pimpl->_pollingThread->Disconnect(dir.second._conduit));
		}

		for (const auto& wait:futuresToWaitOn)
			wait.wait();
	}
}

