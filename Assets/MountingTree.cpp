// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "MountingTree.h"
#include "../Utility/MemoryUtils.h"
#include "../Utility/Threading/Mutex.h"
#include "../Utility/Streams/PathUtils.h"
#include <atomic>

namespace Assets
{
	using HashValue = uint64;

	class MountingTree::Pimpl
	{
	public:
		FilenameRules _rules;

		class Mount
		{
		public:
			HashValue	_hash;
			unsigned	_depth;	// number of different path sections combined into the hash value
			std::shared_ptr<IFileSystem> _fileSystem;
			MountID		_id;
			SplitPath<utf8>	_mountPoint;
			std::basic_string<utf8> _mountPointBuffer;

			Mount& operator=(const Mount&) = delete;
			Mount(const Mount&) = delete;
			Mount& operator=(Mount&&);
			Mount(Mount&&);
			Mount();
			Mount(HashValue hash, unsigned depth, const std::shared_ptr<IFileSystem>& fileSystem, MountID id, SplitPath<utf8>&&	mountPoint, std::basic_string<utf8>&& mountPointBuffer);
		};

		std::vector<Mount>	_mounts;	// ordered from highest to lowest priority
		uint32_t			_changeId = 1;	// start at one so we can use 0 as a sentinel
		Threading::RecursiveMutex	_mountsLock;
		bool				_hasAtLeastOneMount;
		AbsolutePathMode    _absolutePathMode;

		Pimpl(const FilenameRules &rules) : _rules(rules), _hasAtLeastOneMount(false),
		                                    _absolutePathMode(AbsolutePathMode::RawOS) {}
	};

	template<typename CharType>
		static bool	IsSeparator(CharType chr)
	{
		return chr == CharType('/') || chr == CharType('\\');
	}

	template<typename CharType>
		static const CharType* FindFirstSeparator(StringSection<CharType> section)
	{
		const CharType seps[] = { (CharType)'\\', (CharType)'/' };
		return std::find_first_of(section.begin(), section.end(), seps, ArrayEnd(seps));
	}

	template<typename CharType>
		static const CharType* SkipSeparators(StringSection<CharType> section)
	{
		auto i = section.begin();
		for (;i!=section.end(); ++i)
			if (!IsSeparator(*i)) break;
		return i;
	}

	template<typename CharType>
		const CharType* XlFindChar(StringSection<CharType> section, CharType chr)
		{
				// This is a simple implementation that doesn't support searching for multibyte character
				// types in UTF8 or pairs in UTF16.
				// Due to the way UTF8 and UTF16 are defined, we will never get incorrect matches -- but
				// we can't use this to search for code points that are larger than the size of a single "CharType"
				//		-- that can only be done with a version of this function that searches for a string
				// Note; it's different from strchr in that we return "section.end()" if the chr isn't found
			return std::find(section.begin(), section.end(), chr);
		}

	template<typename CharType>
		auto MountingTree::EnumerableLookup::TryGetNext_Internal(CandidateObject& result) const -> Result
	{
		// Since we don't hold the _mountsLock after returning from this function, we will 
		// use a "change id" system to detect any changes to the mounted file systems as we've been 
		// iterating through. If the mounted filesystems change (ie, mounts added or removed), it
		// potentially invalidates the iteration, and we should start again from the top
		ScopedLock(_pimpl->_mountsLock);
		_changeId = _changeId ?: _pimpl->_changeId;
		if (!_pimpl || _pimpl->_changeId != _changeId)
			return Result::Invalidated;

		auto requestString = MakeStringSection(
			(const CharType*)AsPointer(_request.cbegin()), 
			(const CharType*)AsPointer(_request.cend()));

		for (;;) {
			if (_nextMountToTest >= (uint32_t)_pimpl->_mounts.size())
				return Result::NoCandidates;

			const auto& mt = _pimpl->_mounts[_nextMountToTest];
			++_nextMountToTest;

			// simple case for mount depth 0
			if (mt._depth == 0) {
				IFileSystem::Marker marker;
				auto transResult = mt._fileSystem->TryTranslate(marker, requestString);
				if (transResult == IFileSystem::TranslateResult::Success) {
					result._fileSystem = mt._fileSystem;
					result._marker = std::move(marker);
					result._mountPoint = mt._mountPointBuffer;
					return Result::Success;
				}
				continue;
			}

			// If the mount point is too deep, we can't match it.
			// It's a limitation of the system, but helps us write a little optimisation.
			if (mt._depth >= _segmentCount || mt._depth > dimof(_segments)) continue;

			// build the cached value for this depth
			for (auto d = _nextHashValueToBuild; d < mt._depth; ++d) {
				assert(_cachedHashValues[d] == 0);
				auto segment = MakeStringSection((const CharType*)_segments[d].begin(), (const CharType*)_segments[d].end());
				auto rootHash = (d == 0) ? s_FNV_init64 : _cachedHashValues[d - 1];
				_cachedHashValues[d] = HashFilename(segment, _pimpl->_rules, rootHash);
			}
			_nextHashValueToBuild = std::max(_nextHashValueToBuild, mt._depth);

			if (_cachedHashValues[mt._depth-1] == mt._hash) {
				// We got a match. We have to pass this onto the filesystem to try to translate 
				// it into a "Marker" which can later be used for file operations.
				// Note that if the filesystem is still mounting, we can get a "pending/mounting" state for
				// some files that will later become available.
				assert(mt._depth < _segmentCount);
				auto remainderSection = MakeStringSection(
					(const CharType*)_segments[mt._depth].begin(), 
					requestString.end());

				IFileSystem::Marker marker;
				auto transResult = mt._fileSystem->TryTranslate(marker, remainderSection);
				if (transResult == IFileSystem::TranslateResult::Success) {
					result._fileSystem = mt._fileSystem;
					result._marker = std::move(marker);
					result._mountPoint = mt._mountPointBuffer;
					return Result::Success;
				}
			}
		}
	}

	auto MountingTree::EnumerableLookup::TryGetNext(CandidateObject& result) const -> Result
	{
		if (IsGood()) {
			if (_encoding == Encoding::UTF8) {
				return TryGetNext_Internal<utf8>(result);
			} else if (_encoding == Encoding::UTF16) {
				return TryGetNext_Internal<utf16>(result);
			}
		}

		return Result::NoCandidates;
	}

	template<typename CharType>
		static CharType IsRawFilesystem(StringSection<CharType> filename)
	{
		bool isRawFilesystem = IsSeparator(*filename.begin());
		auto* firstSep = FindFirstSeparator(filename);
		auto* driveMarker = std::find(filename.begin(), firstSep, (CharType)':');
		isRawFilesystem |= driveMarker != firstSep;
		return isRawFilesystem;
	}

	MountingTree::EnumerableLookup::EnumerableLookup(
		std::vector<uint8_t>&& request, Encoding encoding, MountingTree::Pimpl* pimpl)
	: _request(std::move(request))
	, _encoding(encoding)
	, _nextMountToTest(0)
	, _isAbsolutePath(false)
	, _pimpl(pimpl)
	, _changeId(0)
	, _nextHashValueToBuild(0)
	{
		// We must split the input string into segments (ie, separated by slashes)
		// while we're doing this, we'll resolve segments such as "./" or "../"
		// It would be nice to be able to find the correct mount without having
		// to resolve these just yet; but as "../" can happen anywhere in the input
		// string we effectively have to iterate over the entire thing...
		_segmentCount = 0;
		bool firstSegmentContainsColon = false;
		if (_encoding == Encoding::UTF8) {
			StringSection<char> totalSection { (char*)AsPointer(_request.begin()), (char*)AsPointer(_request.end()) };
			const char* iterator = totalSection.begin();
			while (iterator < totalSection.end()) {
				iterator = SkipSeparators<char>({iterator, totalSection.end()});
				auto segmentBegin = iterator;
				iterator = FindFirstSeparator<char>({iterator, totalSection.end()});

				if (iterator != segmentBegin) {
					if (*segmentBegin == '.') {
						if (segmentBegin+1 == iterator) {
							// If we find "./", we can ignore that entirely
							// This also applies to "./" at the start of the input -- we just skip it
							// It's not relevant here; because "./" refers to a directory, not a file
							// and the mounting tree system only handles files, not directories
							continue;
						} else if (*(segmentBegin+1) == '.' && (segmentBegin+2) == iterator) {
							// this is exactly ".."
							// we should ignore the last segment
							if (!_segmentCount) {
								// If there are more '..' than specified segments, we consider this an
								// absolute path and don't try to apply the mounting tree syste,
								_isAbsolutePath = true;
								break;
							} else {
								--_segmentCount;
								continue;
							}
						}
					}

					if (_segmentCount < dimof(_segments))
						_segments[_segmentCount] = { (const uint8_t*)segmentBegin, (const uint8_t*)iterator };
					++_segmentCount;
				}
			}

			// If the filename begins with a "/" or a Windows-style drive (eg, c:/) then we can't
			// use the mounting system, and we must drop back to the raw OS filesystem.
			_isAbsolutePath |= !totalSection.IsEmpty() && IsSeparator(totalSection[0]);
			if (_segmentCount >= 2) {
				_isAbsolutePath |= std::find((const char*)_segments[0].begin(), (const char*)_segments[0].end(), ':') != (const char*)_segments[0].end();
			}
		} else {
			StringSection<utf16> totalSection { (utf16*)AsPointer(_request.begin()), (utf16*)AsPointer(_request.end()) };
			const utf16* iterator = totalSection.begin();
			while (iterator < totalSection.end()) {
				iterator = SkipSeparators<utf16>({iterator, totalSection.end()});
				auto segmentBegin = iterator;
				iterator = FindFirstSeparator<utf16>({iterator, totalSection.end()});

				if (iterator != segmentBegin) {
					if (*segmentBegin == '.') {
						if (segmentBegin+1 == iterator) {
							// If we find "./", we can ignore that entirely
							// This also applies to "./" at the start of the input -- we just skip it
							// It's not relevant here; because "./" refers to a directory, not a file
							// and the mounting tree system only handles files, not directories
							continue;
						} else if (*(segmentBegin+1) == '.' && (segmentBegin+2) == iterator) {
							// this is exactly ".."
							// we should ignore the last segment
							if (!_segmentCount) {
								// If there are more '..' than specified segments, we consider this an
								// absolute path and don't try to apply the mounting tree syste,
								_isAbsolutePath = true;
								break;
							} else {
								--_segmentCount;
								continue;
							}
						}
					}

					if (_segmentCount < dimof(_segments))
						_segments[_segmentCount] = { (const uint8_t*)segmentBegin, (const uint8_t*)iterator };
					++_segmentCount;
				}
			}

			// If the filename begins with a "/" or a Windows-style drive (eg, c:/) then we can't
			// use the mounting system, and we must drop back to the raw OS filesystem.
			_isAbsolutePath |= !totalSection.IsEmpty() && IsSeparator(totalSection[0]);
			if (_segmentCount >= 2) {
				_isAbsolutePath |= std::find((const utf16*)_segments[0].begin(), (const utf16*)_segments[0].end(), ':') != (const utf16*)_segments[0].end();
			}
		}
	}

	MountingTree::EnumerableLookup::EnumerableLookup()
	: _encoding(Encoding::UTF8)
	, _nextMountToTest(0)
	, _changeId(0)
	, _pimpl(nullptr)
	{}

	auto MountingTree::Lookup(StringSection<utf8> filename) -> EnumerableLookup
	{
		if (filename.IsEmpty()) return {};

		// We need to find all possible matching candidates for this filename. There are a number
		// of possible ways to this.
		//
		// Consider a filename like:
		//		one/two/three/filename.ext
		// and a filesystem mounted at:
		//		one/two
		//
		// We need to compare the "one" and "two" against the filesystem mounting point.
		//
		// There are couple of approaches...
		// We maintain a linear list of filesystem, ordered by priority. In this case, we store a
		// single hash value and a depth value for each filesystem.
		// We must calculate a comparison hash value from "filename" that matches the correct depth.
		// Then we just compare that with the filesystem hash value.
		//
		// Another possibility is to arrange the filesystems in a tree (like a directory tree). We walk
		// through the tree, comparing the path section against the values in the tree.
		// After finding all candidates, we have to sort by priority order.
		//
		// In most cases, we should have only a few filesystems (let's say, less than 10). Maybe for
		// final production games we might only have 3 or 4.
		// So, given this, it seems like maybe the linear list could be the ideal option? Anyway, it 
		// gives the fastest resolution when the highest priority filesystem is the one selected.

		std::vector<uint8_t> request(filename.begin(), filename.end());
		return EnumerableLookup { std::move(request), EnumerableLookup::Encoding::UTF8, _pimpl.get() };
	}

	auto MountingTree::Lookup(StringSection<utf16> filename) -> EnumerableLookup
	{
		if (filename.IsEmpty()) return {};
		std::vector<uint8_t> request((const uint8_t*)filename.begin(), (const uint8_t*)filename.end());
		return EnumerableLookup { std::move(request), EnumerableLookup::Encoding::UTF16, _pimpl.get() };
	}

	static std::string SimplifyMountPoint(StringSection<> input, const FilenameRules& fnRules)
    {
        auto split = MakeSplitPath(input);
		// We should avoid beginning with a separator, because this would mean that the "mounted path" returned from GetDesc, or GetMountPoint will also begin with a separator
		// The runs into issues with AbsolutePathMode::RawOS, because it means that those returned paths can't be fed back into the mounting tree 
        split.BeginsWithSeparator() = false;
        split.EndsWithSeparator() = true;
        return split.Simplify().Rebuild(fnRules);
    }

	auto MountingTree::Mount(StringSection<utf8> mountPointInput, std::shared_ptr<IFileSystem> system) -> MountID
	{
			// Note that we're going to be ignoring slashs at the beginning or end. These have no effect 
			// on how we interpret the mount point.
			// Let's do some normalization of the input to avoid any edge cases
		auto mountPoint = SimplifyMountPoint(mountPointInput, _pimpl->_rules);
		auto split = MakeSplitPath(mountPoint);

		uint64 hash = s_FNV_init64;
		for (auto i:split.GetSections())
			hash = HashFilename(i, _pimpl->_rules, hash);
		
		ScopedLock(_pimpl->_mountsLock);
		MountID id = _pimpl->_changeId++;
		auto sectionCount = split.GetSectionCount();
		_pimpl->_mounts.emplace_back(Pimpl::Mount{hash, sectionCount, std::move(system), id, std::move(split), std::move(mountPoint)});
		_pimpl->_hasAtLeastOneMount = true;
		return id;
	}

	void MountingTree::Unmount(MountID mountId)
	{
		// just search for the mount with the same id, and remove it
		ScopedLock(_pimpl->_mountsLock);
		auto i = std::find_if(
			_pimpl->_mounts.begin(), _pimpl->_mounts.end(),
			[mountId](const Pimpl::Mount& m) { return m._id == mountId; });
		if (i != _pimpl->_mounts.end())
			_pimpl->_mounts.erase(i);

		_pimpl->_hasAtLeastOneMount = !_pimpl->_mounts.empty();
	}

	IFileSystem* MountingTree::GetMountedFileSystem(MountID mountId)
	{
		ScopedLock(_pimpl->_mountsLock);
		auto i = std::find_if(
			_pimpl->_mounts.begin(), _pimpl->_mounts.end(),
			[mountId](const Pimpl::Mount& m) { return m._id == mountId; });
		if (i != _pimpl->_mounts.end())
			return i->_fileSystem.get();
		return nullptr;
	}

	std::basic_string<utf8> MountingTree::GetMountPoint(MountID mountId)
	{
		ScopedLock(_pimpl->_mountsLock);
		auto i = std::find_if(
			_pimpl->_mounts.begin(), _pimpl->_mounts.end(),
			[mountId](const Pimpl::Mount& m) { return m._id == mountId; });
		if (i != _pimpl->_mounts.end())
			return i->_mountPointBuffer;
		return {};
	}

	void MountingTree::SetAbsolutePathMode(AbsolutePathMode newMode)
	{
		_pimpl->_absolutePathMode = newMode;
	}

	auto MountingTree::GetAbsolutePathMode() -> AbsolutePathMode
	{
		return _pimpl->_absolutePathMode;
	}

	bool MountingTree::LooksLikeAbsolutePath(StringSection<utf8> filename)
	{
		bool isAbsolutePath = !filename.IsEmpty() && IsSeparator(filename[0]);
		isAbsolutePath |= !MakeSplitPath(filename).GetDrive().IsEmpty();
		return isAbsolutePath;
	}

	FileSystemWalker MountingTree::BeginWalk(StringSection<utf8> initialSubDirectory)
	{
		// Find each filesystem that matches the 
		auto splitInitial = MakeSplitPath(initialSubDirectory).Simplify();
		std::vector<FileSystemWalker::StartingFS> result;
		ScopedLock(_pimpl->_mountsLock);
		for (const auto&mount:_pimpl->_mounts) {
			auto searchingFs = std::dynamic_pointer_cast<ISearchableFileSystem>(mount._fileSystem);
			if (!searchingFs) continue;

			bool match = true;
			auto minCount = std::min(mount._depth, splitInitial.GetSectionCount());
			for (unsigned c=0; c<minCount; ++c)
				if (HashFilename(splitInitial.GetSections()[c]) != HashFilename(mount._mountPoint.GetSections()[c])) {
					match = false;
					break;
				}
			if (!match) continue;

			utf8 remainingPath[MaxPath];
			if (splitInitial.GetSectionCount() > mount._depth) {
				SplitPath<utf8>{
					std::vector<SplitPath<utf8>::Section>{
						&splitInitial.GetSections()[minCount],
						splitInitial.GetSections().end()}}.Rebuild(remainingPath);
				result.emplace_back(FileSystemWalker::StartingFS{{}, remainingPath, std::move(searchingFs), mount._id});	// (note that we use the mount id as the filesystem ie, due to behaviour in MainFileSystem::GetFileSystem)
			} else {
				SplitPath<utf8>{
					std::vector<SplitPath<utf8>::Section>{
						&mount._mountPoint.GetSections()[minCount],
						mount._mountPoint.GetSections().end()}}.Rebuild(remainingPath);
				result.emplace_back(FileSystemWalker::StartingFS{remainingPath, {}, std::move(searchingFs), mount._id});
			}
		}

		return result;
	}

	MountingTree::MountingTree(FilenameRules& rules)
	{
		_pimpl = std::make_unique<Pimpl>(rules);
		_pimpl->_changeId = 0;
	}

	MountingTree::~MountingTree() {}

	MountingTree::MountingTree(const MountingTree& cloneFrom) { assert(0);  }
	MountingTree& MountingTree::operator=(const MountingTree& cloneFrom) { assert(0); return *this; }


	MountingTree::Pimpl::Mount::Mount() 
	{
		_hash = 0;
		_depth = 0;
		_id = ~0u;
	}

	auto MountingTree::Pimpl::Mount::operator=(Mount&& moveFrom) -> Mount&
	{
		_hash = moveFrom._hash;
		_depth = moveFrom._depth;
		_fileSystem = std::move(moveFrom._fileSystem);
		_id = moveFrom._id;
		void* originalBaseAddress = moveFrom._mountPointBuffer.data();
		_mountPointBuffer = std::move(moveFrom._mountPointBuffer);
		if (_mountPointBuffer.data() != originalBaseAddress) {
			_mountPoint = MakeSplitPath(_mountPointBuffer);
		} else {
			_mountPoint = std::move(moveFrom._mountPoint);
		}

		moveFrom._hash = 0;
		moveFrom._depth = 0;
		moveFrom._id = ~0u;

		return *this;
	}

	MountingTree::Pimpl::Mount::Mount(Mount&& moveFrom)
	{
		_hash = moveFrom._hash;
		_depth = moveFrom._depth;
		_fileSystem = std::move(moveFrom._fileSystem);
		_id = moveFrom._id;
		void* originalBaseAddress = moveFrom._mountPointBuffer.data();
		_mountPointBuffer = std::move(moveFrom._mountPointBuffer);
		if (_mountPointBuffer.data() != originalBaseAddress) {
			_mountPoint = MakeSplitPath(_mountPointBuffer);
		} else {
			_mountPoint = std::move(moveFrom._mountPoint);
		}

		moveFrom._hash = 0;
		moveFrom._depth = 0;
		moveFrom._id = ~0u;
	}

	MountingTree::Pimpl::Mount::Mount(HashValue hash, unsigned depth, const std::shared_ptr<IFileSystem>& fileSystem, MountID id, SplitPath<utf8>&&	mountPoint, std::basic_string<utf8>&& mountPointBuffer)
	{
		_hash = hash;
		_depth = depth;
		_fileSystem = fileSystem;
		_id = id;
		_mountPoint = std::move(mountPoint);
		_mountPointBuffer = std::move(mountPointBuffer);
	}


}

