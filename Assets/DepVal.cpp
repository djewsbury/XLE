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
	class DependencyValidationSystem : public IDependencyValidationSystem
	{
	public:
		using MonitoredFileId = unsigned;
		class MonitoredFile : public IFileMonitor
		{
		public:
			MonitoredFileId _marker;
			std::vector<DependentFileState> _states;

			virtual void OnChange() override;
		};

		struct Entry
		{
			unsigned _refCount = 0;
			unsigned _validationIndex = 0;
		};

		DependencyValidation Make(IteratorRange<const StringSection<>*> filenames) override
		{
			ScopedLock(_lock);
			DependencyValidation result = MakeAlreadyLocked();
			for (const auto& fn:filenames)
				RegisterFileDependencyAlreadyLocked(result._marker, fn);
			return result;
		}
		
		DependencyValidation Make(IteratorRange<const DependentFileState*> filestates) override
		{
			ScopedLock(_lock);
			DependencyValidation result = MakeAlreadyLocked();
			for (const auto& state:filestates)
				RegisterFileDependencyAlreadyLocked(result._marker, state._filename);
			return result;
		}

		DependencyValidation Make() override
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

		unsigned GetValidationIndex(DependencyValidationMarker marker) override
		{
			ScopedLock(_lock);
			assert(marker < _entries.size());
			assert(_entries[marker]._refCount != 0);
			return _entries[marker]._validationIndex;
		}

		void AddRef(DependencyValidationMarker marker) override
		{
			ScopedLock(_lock);
			assert(marker < _entries.size());
			assert(_entries[marker]._refCount != 0);
			++_entries[marker]._refCount;
		}

		void Release(DependencyValidationMarker marker) override
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
				MainFileSystem::TryMonitor(filename, newMonitoredFile);
				auto fileDesc = MainFileSystem::TryGetDesc(filename);
				assert(fileDesc._state != FileDesc::State::Invalid);
				DependentFileState fs;
				fs._filename = filename.AsString();		// (consider using the mounted name instead? translated marker & fs idx would be better)
				fs._timeMarker = fileDesc._modificationTime;
				fs._status = (fileDesc._state == FileDesc::State::DoesNotExist) ? DependentFileState::Status::DoesNotExist : DependentFileState::Status::Normal;
				newMonitoredFile->_states.push_back(fs);
				newMonitoredFile->_marker = (MonitoredFileId)_monitoredFiles.size();
				existing = _monitoredFiles.insert(existing, std::make_pair(hash, newMonitoredFile));
			}

			return *existing->second.get();
		}

		void RegisterFileDependency(
			DependencyValidationMarker validationMarker, 
			StringSection<> filename) override
		{
			ScopedLock(_lock);
			RegisterFileDependencyAlreadyLocked(validationMarker, filename);
		}

		void RegisterFileDependencyAlreadyLocked(
			DependencyValidationMarker validationMarker, 
			StringSection<> filename)
		{
			auto& fileMonitor = GetMonitoredFileAlreadyLocked(filename);
			unsigned mostRecentState = unsigned(fileMonitor._states.size()-1);
			auto insertRange = EqualRange(_fileLinks, validationMarker);
			for (auto r=insertRange.first; r!=insertRange.second; ++r)
				if (r->second.first == fileMonitor._marker) {
					r->second.second = mostRecentState;
					return;	// already registered
				}
			_fileLinks.insert(insertRange.second, std::make_pair(validationMarker, std::make_pair(fileMonitor._marker, mostRecentState)));
		}

		void RegisterAssetDependency(
			DependencyValidationMarker dependentResource, 
			DependencyValidationMarker dependency) override
		{
			ScopedLock(_lock);
			assert(dependentResource < _entries.size());
			assert(dependency < _entries.size());
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
		}

		void IncreaseValidationIndex(DependencyValidationMarker marker) override
		{
			ScopedLock(_lock);
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
			for (auto marker:recursedMarkers) {
				assert(marker < _entries.size());
				assert(_entries[marker]._refCount != 0);
				++_entries[marker]._validationIndex;
			}
		}

		DependentFileState GetDependentFileState(StringSection<> filename) override
		{
			ScopedLock(_lock);
			auto& fileMonitor = GetMonitoredFileAlreadyLocked(filename);
			assert(!fileMonitor._states.empty());
			return *(fileMonitor._states.end()-1);
		}

		void ShadowFile(StringSection<ResChar> filename) override
		{
			ScopedLock(_lock);
			auto& fileMonitor = GetMonitoredFileAlreadyLocked(filename);
			DependentFileState newState = *(fileMonitor._states.end()-1);
			newState._status = DependentFileState::Status::Shadowed;
			fileMonitor._states.push_back(newState);
			MainFileSystem::TryFakeFileChange(filename);
			PropagateFileChange(fileMonitor._marker);
		}

		DependencyValidationSystem()
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
	};

	static ConsoleRig::WeakAttachablePtr<IDependencyValidationSystem> s_depValSystem;

	void    DependencyValidationSystem::MonitoredFile::OnChange()
	{
			// on change, update the modification time record
		auto filename = (_states.end()-1)->_filename;
		auto fileDesc = MainFileSystem::TryGetDesc(filename);
		DependentFileState newState;
		newState._filename = filename;
		newState._timeMarker = fileDesc._modificationTime;
		newState._status = (fileDesc._state == FileDesc::State::DoesNotExist) ? DependentFileState::Status::DoesNotExist : DependentFileState::Status::Normal;
		_states.push_back(newState);
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

	void            DependencyValidation::RegisterDependency(StringSection<> filename)
	{
		assert(_marker != DependencyValidationMarker_Invalid);
		return checked_cast<DependencyValidationSystem*>(&GetDepValSys())->RegisterFileDependency(_marker, filename);
	}

	void            DependencyValidation::RegisterDependency(DependentFileState& state)
	{
		RegisterDependency(state._filename);
	}

	void            DependencyValidation::IncreaseValidationIndex()
	{
		assert(_marker != DependencyValidationMarker_Invalid);
		return checked_cast<DependencyValidationSystem*>(&GetDepValSys())->IncreaseValidationIndex(_marker);
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

