// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "DepVal.h"
#include "IFileSystem.h"
#include "../OSServices/FileSystemMonitor.h"
#include "../ConsoleRig/AttachablePtr.h"
#include "../Utility/Threading/Mutex.h"
#include "../Utility/Streams/PathUtils.h"
#include "../Utility/HeapUtils.h"

namespace Assets
{
	#define SEALED sealed

	class DependencyValidationSystem : public IDependencyValidationSystem
	{
	public:
		using MonitoredFileId = unsigned;
		class MonitoredFile : public IFileMonitor
		{
		public:
			MonitoredFileId _marker;
			std::vector<FileSnapshot> _snapshots;
			unsigned _mostRecentSnapshotIdx =0 ;
			std::string _filename;

			virtual void OnChange() override;
		};

		struct Entry
		{
			unsigned _refCount = 0;
			unsigned _validationIndex = 0;
		};

		DependencyValidation Make(IteratorRange<const StringSection<>*> filenames) override SEALED
		{
			ScopedLock(_lock);
			DependencyValidation result = MakeAlreadyLocked();
			for (const auto& fn:filenames)
				RegisterFileDependencyAlreadyLocked(result._marker, fn);
			return result;
		}
		
		DependencyValidation Make(IteratorRange<const DependentFileState*> filestates) override SEALED
		{
			ScopedLock(_lock);
			DependencyValidation result = MakeAlreadyLocked();
			for (const auto& state:filestates)
				RegisterFileDependencyAlreadyLocked(result._marker, state._filename, state._snapshot);
			return result;
		}

		DependencyValidation MakeOrReuse(IteratorRange<const DependencyValidationMarker*> dependencyAssets) override SEALED
		{
			unsigned validCount = 0;
			for (auto marker:dependencyAssets)
				if (marker != DependencyValidationMarker_Invalid)
					++validCount;
			if (!validCount) return {};
			
			ScopedLock(_lock);
			if (validCount == 1)
				for (auto marker:dependencyAssets)
					if (marker != DependencyValidationMarker_Invalid) {
						++_entries[marker]._refCount;
						return marker;
					}

			DependencyValidation result = MakeAlreadyLocked();
			for (auto marker:dependencyAssets)
				if (marker != DependencyValidationMarker_Invalid)
					RegisterAssetDependencyAlreadyLocked(result._marker, marker);
			return result;
		}

		DependencyValidation Make() override SEALED
		{
			ScopedLock(_lock);
			return MakeAlreadyLocked();
		}
		
		DependencyValidation MakeAlreadyLocked()
		{
			auto newDepVal = (DependencyValidationMarker)_markerHeap.Allocate(1);
			if (newDepVal == ~0u)
				newDepVal = (DependencyValidationMarker)_markerHeap.AppendNewBlock(1);
			assert(newDepVal != ~0u);
			if (newDepVal >= _entries.size())
				_entries.resize(newDepVal+1);

			_entries[newDepVal]._refCount = 1;
			_entries[newDepVal]._validationIndex = 0;
			return newDepVal;
		}

		unsigned GetValidationIndex(DependencyValidationMarker marker) override SEALED
		{
			ScopedLock(_lock);
			assert(marker < _entries.size());
			assert(_entries[marker]._refCount != 0);
			return _entries[marker]._validationIndex;
		}

		void AddRef(DependencyValidationMarker marker) override SEALED
		{
			ScopedLock(_lock);
			assert(marker < _entries.size());
			assert(_entries[marker]._refCount != 0);
			++_entries[marker]._refCount;
		}

		void Release(DependencyValidationMarker marker) override SEALED
		{
			ScopedLock(_lock);
			ReleaseAlreadyLocked(marker);
		}

		void ReleaseAlreadyLocked(DependencyValidationMarker marker)
		{
			assert(marker < _entries.size());
			assert(_entries[marker]._refCount != 0);
			--_entries[marker]._refCount;
			if (_entries[marker]._refCount == 0) {
				auto assetLinks = EqualRange(_assetLinks, marker);
				std::vector<std::pair<DependencyValidationMarker, DependencyValidationMarker>> assetLinksToDestroy { assetLinks.first, assetLinks.second };
				_assetLinks.erase(assetLinks.first, assetLinks.second);
				auto fileLinks = EqualRange(_fileLinks, marker);
				_fileLinks.erase(fileLinks.first, fileLinks.second);
				// Release ref on our dependencies after we've finished changing _assetLinks & _fileLinks
				for (const auto& c:assetLinksToDestroy)
					ReleaseAlreadyLocked(c.second);
				_markerHeap.Deallocate(marker, 1);
			}
		}

		void DestroyAlreadyLocked(DependencyValidationMarker marker)
		{
			assert(_entries[marker]._refCount == 0);
		}

		MonitoredFile& GetMonitoredFileAlreadyLocked(StringSection<> filename)
		{
			auto hash = HashFilenameAndPath(filename);
			auto existing = LowerBound(_monitoredFiles, hash);

			if (existing == _monitoredFiles.end() || existing->first != hash) {
				auto newMonitoredFile = std::make_shared<MonitoredFile>();
				FileSnapshot snapshot{FileSnapshot::State::DoesNotExist, 0};
				auto monitoringResult = MainFileSystem::TryMonitor(snapshot, filename, newMonitoredFile);
				(void)monitoringResult;		// allow this to fail silently
				newMonitoredFile->_snapshots.push_back(snapshot);
				newMonitoredFile->_marker = (MonitoredFileId)_monitoredFiles.size();
				newMonitoredFile->_filename = filename.AsString();
				newMonitoredFile->_mostRecentSnapshotIdx = 0;
				existing = _monitoredFiles.insert(existing, std::make_pair(hash, newMonitoredFile));
			}

			return *existing->second.get();
		}

		void RegisterFileDependency(
			DependencyValidationMarker validationMarker, 
			const DependentFileState& fileState) override
		{
			ScopedLock(_lock);
			RegisterFileDependencyAlreadyLocked(validationMarker, fileState._filename, fileState._snapshot);
		}

		static unsigned FindOrAddSnapshot(std::vector<FileSnapshot>& snapshots, const FileSnapshot& search)
		{
			for (unsigned c=0; c<snapshots.size(); ++c)
				if (snapshots[c] == search) return c;
			snapshots.push_back(search);
			return (unsigned)snapshots.size()-1;
		}

		void RegisterFileDependencyAlreadyLocked(
			DependencyValidationMarker validationMarker, 
			StringSection<> filename,
			const FileSnapshot& snapshot)
		{
			auto& fileMonitor = GetMonitoredFileAlreadyLocked(filename);
			unsigned snapshotIndex = FindOrAddSnapshot(fileMonitor._snapshots, snapshot);
			auto insertRange = EqualRange(_fileLinks, validationMarker);
			bool alreadyRegistered = false;
			for (auto r=insertRange.first; r!=insertRange.second; ++r)
				if (r->second.first == fileMonitor._marker) {
					// pick the snapshot with the earlier modification time
					if (fileMonitor._snapshots[snapshotIndex]._modificationTime < fileMonitor._snapshots[r->second.second]._modificationTime)
						r->second.second = snapshotIndex;
					alreadyRegistered = true;
				}
			if (!alreadyRegistered)
				_fileLinks.insert(insertRange.second, std::make_pair(validationMarker, std::make_pair(fileMonitor._marker, snapshotIndex)));

			if (snapshotIndex != fileMonitor._mostRecentSnapshotIdx) {
				// registering a snapshot that is already invalidated -- we must increase the validation index
				IncreaseValidationIndexAlreadyLocked(validationMarker);
			}
		}

		void RegisterFileDependencyAlreadyLocked(
			DependencyValidationMarker validationMarker, 
			StringSection<> filename)
		{
			auto& fileMonitor = GetMonitoredFileAlreadyLocked(filename);
			auto insertRange = EqualRange(_fileLinks, validationMarker);
			for (auto r=insertRange.first; r!=insertRange.second; ++r)
				if (r->second.first == fileMonitor._marker)
					return;	// already registered
			_fileLinks.insert(insertRange.second, std::make_pair(validationMarker, std::make_pair(fileMonitor._marker, fileMonitor._mostRecentSnapshotIdx)));
		}

		void RegisterAssetDependencyAlreadyLocked(
			DependencyValidationMarker dependentResource, 
			DependencyValidationMarker dependency)
		{
			assert(dependentResource < _entries.size());
			assert(dependency < _entries.size());
			assert(dependency != DependencyValidationMarker_Invalid);
			assert(dependentResource != DependencyValidationMarker_Invalid);
			assert(_entries[dependentResource]._refCount > 0);
			assert(_entries[dependency]._refCount > 0);

			auto insertRange = EqualRange(_assetLinks, dependentResource);
			for (auto r=insertRange.first; r!=insertRange.second; ++r)
				if (r->second == dependency)
					return;	// already registered

			// The dependency gets a ref count bump, but not the dependentResource
			++_entries[dependency]._refCount;
			_assetLinks.insert(insertRange.first, std::make_pair(dependentResource, dependency));
		}

		void RegisterAssetDependency(
			DependencyValidationMarker dependentResource, 
			DependencyValidationMarker dependency) override
		{
			ScopedLock(_lock);
			RegisterAssetDependencyAlreadyLocked(dependentResource, dependency);
		}

		static bool InSortedRange(IteratorRange<const DependencyValidationMarker*> range, DependencyValidationMarker marker)
		{
			auto i = std::lower_bound(range.begin(), range.end(), marker);
			return i != range.end() && *i == marker;
		}

		void PropagateFileChange(MonitoredFileId marker)
		{
			// With these data structures, this operation can be a little expensive (but it means
			// everything else should be pretty cheap)
			ScopedLock(_lock);
			std::vector<DependencyValidationMarker> newMarkers;
			for (const auto&l:_fileLinks)
				if (l.second.first == marker)
					newMarkers.push_back(l.first);
			std::sort(newMarkers.begin(), newMarkers.end());
			
			std::vector<DependencyValidationMarker> recursedMarkers;
			std::vector<DependencyValidationMarker> nextNewMarkers;
			while (!newMarkers.empty()) {
				for (const auto&l:_assetLinks) {
					if (InSortedRange(newMarkers, l.second)) {
						if (!InSortedRange(newMarkers, l.first) && !InSortedRange(recursedMarkers, l.first))
							nextNewMarkers.push_back(l.first);
					}
				}
				auto middle = recursedMarkers.insert(recursedMarkers.end(), newMarkers.begin(), newMarkers.end());
				std::inplace_merge(recursedMarkers.begin(), middle, recursedMarkers.end());
				std::swap(newMarkers, nextNewMarkers);
				nextNewMarkers.clear();
			}

			// Finally update the validation index on all of the entries we reached
			for (auto marker:recursedMarkers) {
				assert(marker < _entries.size());
				assert(_entries[marker]._refCount != 0);
				++_entries[marker]._validationIndex;
			}

			++_globalChangeIndex;	// ensure this is done last
		}

		void IncreaseValidationIndex(DependencyValidationMarker marker) override
		{
			ScopedLock(_lock);
			IncreaseValidationIndexAlreadyLocked(marker);
		}

		void IncreaseValidationIndexAlreadyLocked(DependencyValidationMarker marker)
		{
			std::vector<DependencyValidationMarker> newMarkers;
			for (const auto&l:_assetLinks)
				if (l.second == marker)
					newMarkers.push_back(l.first);
			std::sort(newMarkers.begin(), newMarkers.end());
			
			std::vector<DependencyValidationMarker> recursedMarkers;
			std::vector<DependencyValidationMarker> nextNewMarkers;
			while (!newMarkers.empty()) {
				for (const auto&l:_assetLinks) {
					if (InSortedRange(newMarkers, l.second)) {
						if (!InSortedRange(newMarkers, l.first) && !InSortedRange(recursedMarkers, l.first))
							nextNewMarkers.push_back(l.first);
					}
				}
				auto middle = recursedMarkers.insert(recursedMarkers.end(), newMarkers.begin(), newMarkers.end());
				std::inplace_merge(recursedMarkers.begin(), middle, recursedMarkers.end());
				std::swap(newMarkers, nextNewMarkers);
				nextNewMarkers.clear();
			}

			// Finally update the validation index on all of the entries we reached
			// (also increase for the specific marker passed in)
			for (auto marker:recursedMarkers) {
				assert(marker < _entries.size());
				assert(_entries[marker]._refCount != 0);
				++_entries[marker]._validationIndex;
			}
			++_entries[marker]._validationIndex;

			++_globalChangeIndex;	// ensure this is done last
		}

		DependentFileState GetDependentFileState(StringSection<> filename) override
		{
			ScopedLock(_lock);
			auto& fileMonitor = GetMonitoredFileAlreadyLocked(filename);
			assert(!fileMonitor._snapshots.empty());
			const auto& snapshot = fileMonitor._snapshots[fileMonitor._mostRecentSnapshotIdx];
			return { fileMonitor._filename, snapshot };
		}

		void ShadowFile(StringSection<ResChar> filename) override
		{
			assert(0);
			/*ScopedLock(_lock);
			auto& fileMonitor = GetMonitoredFileAlreadyLocked(filename);
			DependentFileState newState = *(fileMonitor._states.end()-1);
			newState._status = DependentFileState::Status::Shadowed;
			fileMonitor._states.push_back(newState);
			MainFileSystem::TryFakeFileChange(filename);
			PropagateFileChange(fileMonitor._marker);*/
		}

		void CollateDependentFileStates(std::vector<DependentFileState>& result, DependencyValidationMarker marker) override
		{
			// track down the files in the tree underneath the given marker
			ScopedLock(_lock);
			std::vector<std::pair<MonitoredFileId, unsigned>> fileList;
			std::vector<DependencyValidationMarker> searchQueue;
			searchQueue.push_back(marker);
			while (!searchQueue.empty()) {
				auto node = searchQueue.back();
				searchQueue.erase(searchQueue.end()-1);

				auto dependencies = EqualRange(_assetLinks, node);
				for (const auto& d:MakeIteratorRange(dependencies.first, dependencies.second)) searchQueue.push_back(d.second);

				auto files = EqualRange(_fileLinks, node);
				for (const auto& d:MakeIteratorRange(files.first, files.second)) fileList.push_back(d.second);
			}

			// Tiny bit of processing to ensure we can support the same file being referenced mutliple times, possibly with
			// different snapshots. Since we could be looking at a complex tree of assets, it's possible we might hit these
			// edge conditions sometimes
			result.reserve(fileList.size());
			std::sort(fileList.begin(), fileList.end(), CompareFirst2{});

			for (auto i=fileList.begin(); i!=fileList.end();) {
				auto endi = i+1;
				while (endi!=fileList.end() && endi->first == i->first) ++endi;

				auto file = _monitoredFiles.begin();
				for (; file != _monitoredFiles.end(); ++file)
					if (file->second->_marker == i->first) break;

				if (file != _monitoredFiles.end()) {
					// We might end up with multiple references to the same file -- if so, back only the oldest one
					// If there are multiples, they must all have the same state
					uint64_t modificationTime = ~0ull;
					for (auto i2=i; i2!=endi; ++i2) {
						modificationTime = std::min(modificationTime, file->second->_snapshots[i2->second]._modificationTime);
						assert(file->second->_snapshots[i2->second]._state == file->second->_snapshots[i->second]._state);
					}
					result.emplace_back(file->second->_filename, FileSnapshot{file->second->_snapshots[i->second]._state, modificationTime});
				}

				i = endi;
			}
		}

		void CollateDependentFileUpdates(std::vector<DependencyUpdateReport>& result, DependencyValidationMarker marker) override
		{
			// track down the files in the tree underneath the given marker, and find which of them are not at their most
			// recent snapshot
			ScopedLock(_lock);
			std::vector<std::pair<MonitoredFileId, unsigned>> fileList;
			std::vector<DependencyValidationMarker> searchQueue;
			searchQueue.push_back(marker);
			while (!searchQueue.empty()) {
				auto node = searchQueue.back();
				searchQueue.erase(searchQueue.end()-1);

				auto dependencies = EqualRange(_assetLinks, node);
				for (const auto& d:MakeIteratorRange(dependencies.first, dependencies.second)) searchQueue.push_back(d.second);

				auto files = EqualRange(_fileLinks, node);
				for (const auto& d:MakeIteratorRange(files.first, files.second)) fileList.push_back(d.second);
			}

			// Tiny bit of processing to ensure we can support the same file being referenced mutliple times, possibly with
			// different snapshots. Since we could be looking at a complex tree of assets, it's possible we might hit these
			// edge conditions sometimes
			result.reserve(fileList.size());
			std::sort(fileList.begin(), fileList.end(), CompareFirst2{});

			for (auto i=fileList.begin(); i!=fileList.end();) {
				auto endi = i+1;
				while (endi!=fileList.end() && endi->first == i->first) ++endi;

				auto file = _monitoredFiles.begin();
				for (; file != _monitoredFiles.end(); ++file)
					if (file->second->_marker == i->first) break;

				if (file != _monitoredFiles.end()) {
					// We might end up with multiple references to the same file -- if so, back only the oldest one
					// If there are multiples, they must all have the same state
					uint64_t modificationTime = ~0ull;
					for (auto i2=i; i2!=endi; ++i2) {
						modificationTime = std::min(modificationTime, file->second->_snapshots[i2->second]._modificationTime);
						assert(file->second->_snapshots[i2->second]._state == file->second->_snapshots[i->second]._state);
					}
					FileSnapshot dependentSnapshot{file->second->_snapshots[i->second]._state, modificationTime};
					if (!(dependentSnapshot == file->second->_snapshots[file->second->_mostRecentSnapshotIdx]))
						result.push_back({file->second->_filename, dependentSnapshot, file->second->_snapshots[file->second->_mostRecentSnapshotIdx]});
				}

				i = endi;
			}
		}

		unsigned GlobalChangeIndex() override
		{
			return _globalChangeIndex.load();
		}

		DependencyValidationSystem()
		: _globalChangeIndex(0)
		{
		}

		~DependencyValidationSystem()
		{

		}
	private:
		SpanningHeap<DependencyValidationMarker> _markerHeap;
		
		std::vector<std::pair<uint64_t, std::shared_ptr<MonitoredFile>>> _monitoredFiles;
		std::vector<Entry> _entries;

		std::vector<std::pair<DependencyValidationMarker, DependencyValidationMarker>> _assetLinks;
		std::vector<std::pair<DependencyValidationMarker, std::pair<MonitoredFileId, unsigned>>> _fileLinks;
		Threading::Mutex _lock;
		std::atomic<unsigned> _globalChangeIndex;
	};

	static ConsoleRig::WeakAttachablePtr<IDependencyValidationSystem> s_depValSystem;

	void    DependencyValidationSystem::MonitoredFile::OnChange()
	{
			// on change, update the modification time record
		auto fileDesc = MainFileSystem::TryGetDesc(_filename);
		_mostRecentSnapshotIdx = FindOrAddSnapshot(_snapshots, fileDesc._snapshot);
		checked_cast<DependencyValidationSystem*>(&GetDepValSys())->PropagateFileChange(_marker);
	}

	unsigned        DependencyValidation::GetValidationIndex() const
	{
		if (_marker == DependencyValidationMarker_Invalid) return 0;
		return checked_cast<DependencyValidationSystem*>(&GetDepValSys())->GetValidationIndex(_marker);
	}

	void            DependencyValidation::RegisterDependency(const DependencyValidation& dependency)
	{
		assert(_marker != DependencyValidationMarker_Invalid);
		assert(dependency._marker != DependencyValidationMarker_Invalid);
		return checked_cast<DependencyValidationSystem*>(&GetDepValSys())->RegisterAssetDependency(_marker, dependency._marker);
	}

	void            DependencyValidation::RegisterDependency(const DependentFileState& state)
	{
		assert(_marker != DependencyValidationMarker_Invalid);
		return checked_cast<DependencyValidationSystem*>(&GetDepValSys())->RegisterFileDependency(_marker, state);
	}

	void            DependencyValidation::IncreaseValidationIndex()
	{
		assert(_marker != DependencyValidationMarker_Invalid);
		return checked_cast<DependencyValidationSystem*>(&GetDepValSys())->IncreaseValidationIndex(_marker);
	}

	void DependencyValidation::CollateDependentFileStates(std::vector<DependentFileState>& result) const
	{
		if (_marker == DependencyValidationMarker_Invalid) return;
		checked_cast<DependencyValidationSystem*>(&GetDepValSys())->CollateDependentFileStates(result, _marker);
	}

	void DependencyValidation::CollateDependentFileUpdates(std::vector<DependencyUpdateReport>& result) const
	{
		if (_marker == DependencyValidationMarker_Invalid) return;
		checked_cast<DependencyValidationSystem*>(&GetDepValSys())->CollateDependentFileUpdates(result, _marker);
	}

	DependencyValidation::DependencyValidation() : _marker(DependencyValidationMarker_Invalid) {}
	DependencyValidation::DependencyValidation(DependencyValidation&& moveFrom) never_throws
	{
		_marker = moveFrom._marker;
		moveFrom._marker = DependencyValidationMarker_Invalid;
	}
	DependencyValidation& DependencyValidation::operator=(DependencyValidation&& moveFrom) never_throws
	{
		if (_marker != DependencyValidationMarker_Invalid)
			checked_cast<DependencyValidationSystem*>(&GetDepValSys())->Release(_marker);
		_marker = moveFrom._marker;
		moveFrom._marker = DependencyValidationMarker_Invalid;
		return *this;
	}
	DependencyValidation::DependencyValidation(const DependencyValidation& copyFrom)
	{
		_marker = copyFrom._marker;
		if (_marker != DependencyValidationMarker_Invalid)
			checked_cast<DependencyValidationSystem*>(&GetDepValSys())->AddRef(_marker);
	}
	DependencyValidation& DependencyValidation::operator=(const DependencyValidation& copyFrom)
	{
		if (_marker == copyFrom._marker) return *this;
		if (_marker != DependencyValidationMarker_Invalid)
			checked_cast<DependencyValidationSystem*>(&GetDepValSys())->Release(_marker);
		_marker = copyFrom._marker;
		if (_marker != DependencyValidationMarker_Invalid)
			checked_cast<DependencyValidationSystem*>(&GetDepValSys())->AddRef(_marker);
		return *this;
	}
	DependencyValidation::~DependencyValidation()
	{
		if (_marker != DependencyValidationMarker_Invalid) {
			// Be a little tolerant here, because the dep val system may have already been shutdown
			// It shouldn't be too big of an issue if the shutdown order is not perfect, and just a 
			// bit of hassle to ensure that all DependencyValidation are destroyed before the system
			// is shutdown
			auto sys = s_depValSystem.lock();
			if (sys)
				checked_cast<DependencyValidationSystem*>(sys.get())->Release(_marker);
		}
	}

	DependencyValidation::DependencyValidation(DependencyValidationMarker marker) : _marker(marker)
	{}

	DependencyValidation DependencyValidation::SafeCopy(const DependencyValidation& copyFrom)
	{
		auto sys = s_depValSystem.lock();
		if (sys) {
			DependencyValidation result;
			result._marker = copyFrom._marker;
			if (result._marker != DependencyValidationMarker_Invalid)
				checked_cast<DependencyValidationSystem*>(&GetDepValSys())->AddRef(result._marker);
			return result;
		}
		return {};
	}

	DependencyValidation IDependencyValidationSystem::Make(StringSection<> filename)
	{
		return Make(MakeIteratorRange(&filename, &filename+1));
	}
	DependencyValidation IDependencyValidationSystem::Make(const DependentFileState& filestate)
	{
		return Make(MakeIteratorRange(&filestate, &filestate+1));
	}

	IDependencyValidationSystem& GetDepValSys()
	{
		return *s_depValSystem.lock();
	}

	#if defined(_DEBUG)
		DependencyValidationSystem* g_depValSys = nullptr;

		std::shared_ptr<IDependencyValidationSystem> CreateDepValSys()
		{
			// this exists so we can look at the dep val tree through the debugger watch window.
			// Watch "::Assets::g_depValSys" 
			auto result = std::make_shared<DependencyValidationSystem>();
			g_depValSys = result.get();
			return result;
		}
	#else
		std::shared_ptr<IDependencyValidationSystem> CreateDepValSys()
		{
			return std::make_shared<DependencyValidationSystem>();
		}
	#endif

	
}

