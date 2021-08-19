// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "IFileSystem.h"
#include "../Utility/UTFUtils.h"		// for utf8, utf16
#include "../Utility/StringUtils.h"		// for StringSection

namespace Utility { class FilenameRules; }

namespace Assets
{
	class IFileSystem;
	class IFileInterface;
	class FileDesc;

	/// <summary>Manages a tree of mounted IFileSystems</summary>
	/// This is similar to a file system "namespace" in linux. It contains the tree of all mount
	/// points. Typically each application will only need one.
	///
	/// The system supports overlapping mount points. Multiple different filesystems can have
	/// objects with the exact same name and path.
	/// This is useful when using archive files -- because "free" files in the OS filesystem can
	/// be mounted in the same place as the archive, and override the files within the archive.
	/// So, if there are multiple filesystems mounted, a single query can return multiple possible 
	/// target objects. This is returned in the form of an EnumerableLookup.
	/// Note that an EnumerableLookup will become invalidated if any filesystems are mounted or
	/// unmounted (in the same way that a vector iterator becomes invalidated if the vector changes).
	///
	/// Clients can use the FilenameRules object to define the expected format for filenames. 
	/// <seealso cref="IFileSystem"/>
	class MountingTree
	{
	public:
		class CandidateObject;
		class EnumerableLookup;

		EnumerableLookup	Lookup(StringSection<utf8> filename);
		EnumerableLookup	Lookup(StringSection<utf16> filename);

		// todo -- consider a "cached lookup" that should return the single most ideal candidate
		// (perhaps at a higher level).
		// We want avoid having to check for an existing free file before every archive access
		// If will have multiple high-priority but "sparse" filesystems, we could get multiple
		// failed file operations before each successful one...?

		using MountID = uint32_t;
		MountID			Mount(StringSection<utf8> mountPoint, std::shared_ptr<IFileSystem> system);
		void			Unmount(MountID mountId);
		IFileSystem*	GetMountedFileSystem(MountID);
		std::basic_string<utf8>		GetMountPoint(MountID);

		enum class AbsolutePathMode { MountingTree, RawOS };
		void        SetAbsolutePathMode(AbsolutePathMode newMode);
		AbsolutePathMode GetAbsolutePathMode();
		bool 		LooksLikeAbsolutePath(StringSection<utf8> filename);

		FileSystemWalker BeginWalk(StringSection<utf8> initialSubDirectory);

		MountingTree(Utility::FilenameRules& rules);
		~MountingTree();

		MountingTree(const MountingTree& cloneFrom);
		MountingTree& operator=(const MountingTree& cloneFrom);
	private:
		class Pimpl;
		std::unique_ptr<Pimpl> _pimpl;
	};

	/// <summary>Represents a candidate resolution from a MountingTree query</summary>
	/// Note that the candidate may not exist, or may be invalid. The filesystem must be
	/// accessed to find the state of the object.
	class MountingTree::CandidateObject
	{
	public:
		std::shared_ptr<IFileSystem>	_fileSystem;
		IFileSystem::Marker				_marker;
		std::basic_string<utf8>			_mountPoint;
	};

	class MountingTree::EnumerableLookup
	{
	public:
		// note -- "invalidated" means the EnumerableLookup has been invalidated by a change
		//			to the MountingTree
		enum class Result { Success, NoCandidates, Invalidated };
		Result TryGetNext(CandidateObject& result) const;
		bool IsGood() const { return _pimpl != nullptr; }
		bool IsAbsolutePath() const { return _isAbsolutePath; }

		EnumerableLookup(const EnumerableLookup&) = delete;
		EnumerableLookup& operator=(const EnumerableLookup&) = delete;

		#if defined(COMPILER_DEFAULT_IMPLICIT_OPERATORS)
			EnumerableLookup(EnumerableLookup&&) = default;
			EnumerableLookup& operator=(EnumerableLookup&&) = default;
		#endif
	private:
		std::vector<uint8_t>		_request;
		enum Encoding { UTF8, UTF16 };
		Encoding				_encoding;
		mutable uint32_t		_nextMountToTest;
		mutable uint32_t		_changeId;
		MountingTree::Pimpl *	_pimpl;			// raw pointer; client must be careful

		mutable uint64			_cachedHashValues[8] = { 0,0,0,0,0,0,0,0 };
		mutable IteratorRange<const uint8_t*> _segments[8];
		unsigned 				_segmentCount;
		mutable unsigned		_nextHashValueToBuild;
		bool					_isAbsolutePath;

		EnumerableLookup(std::vector<uint8_t>&& request, Encoding encoding, MountingTree::Pimpl* pimpl);
		EnumerableLookup();

		template<typename CharType>
			Result TryGetNext_Internal(CandidateObject& result) const;

		friend class MountingTree;
	};
}

