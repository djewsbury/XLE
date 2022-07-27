// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "AssetsCore.h"
#include "IArtifact.h"
#include "../OSServices/AttachableLibrary.h"		// for LibVersionDesc
#include "../Utility/Threading/Mutex.h"
#include "../Utility/UTFUtils.h"
#include "../Core/Types.h"

#include <memory>
#include <vector>
#include <string>
#include <functional>

namespace Assets
{
	class ArtifactDirectoryBlock;
	class CollectionDirectoryBlock;
	class IFileSystem;

	class ArchiveCache
	{
	public:
		void Commit(
			uint64_t objectId,
			const std::string& attachedStringName,
			IteratorRange<const ICompileOperation::SerializedArtifact*> artifacts,
			::Assets::AssetState state,
			IteratorRange<const DependentFileState*> dependentFiles,
			std::function<void()>&& onFlush = {});
		std::shared_ptr<IArtifactCollection> TryOpenFromCache(uint64_t id);
		void FlushToDisk();
		
		class BlockMetrics
		{
		public:
			uint64_t _objectId;
			unsigned _offset, _size;
			std::string _attachedString;
		};
		class Metrics
		{
		public:
			unsigned _allocatedFileSize;
			unsigned _usedSpace;
			std::vector<BlockMetrics> _blocks;
		};

		/// <summary>Return profiling related breakdown</summary>
		/// Designed to be used for profiling archive usage and stats.
		Metrics GetMetrics() const;

		ArchiveCache(
			std::shared_ptr<IFileSystem> filesystem,
			StringSection<char> archiveName, const OSServices::LibVersionDesc&);
		~ArchiveCache();

		ArchiveCache(const ArchiveCache&) = delete;
		ArchiveCache& operator=(const ArchiveCache&) = delete;
		ArchiveCache(ArchiveCache&&) = delete;
		ArchiveCache& operator=(ArchiveCache&&) = delete;

	protected:
		class PendingCommit;
		class ComparePendingCommit;

		mutable Threading::Mutex _pendingCommitsLock;
		std::vector<PendingCommit> _pendingCommits;
		std::basic_string<utf8> _mainFileName, _directoryFileName;
		std::shared_ptr<IFileSystem> _filesystem;

		std::string _buildVersionString, _buildDateString;

		mutable std::vector<ArtifactDirectoryBlock> _cachedBlockList;
		mutable bool _cachedBlockListValid;
		const std::vector<ArtifactDirectoryBlock>* GetArtifactBlockList() const;

		mutable std::vector<CollectionDirectoryBlock> _cachedCollectionBlockList;
		mutable bool _cachedCollectionBlockListValid;
		const std::vector<CollectionDirectoryBlock>* GetCollectionBlockList() const;

		using DependencyTable = std::vector<std::pair<uint64_t, DependentFileState>>;
		mutable DependencyTable _cachedDependencyTable;
		mutable bool _cachedDependencyTableValid;
		const DependencyTable* GetDependencyTable() const;

		class ArchivedFileArtifactCollection;
		std::vector<std::pair<uint64_t, unsigned>> _changeIds;
	};

	class ArchiveCacheSet
	{
	public:
		std::shared_ptr<::Assets::ArchiveCache> GetArchive(StringSection<char> archiveFilename);
		void FlushToDisk();
		
		ArchiveCacheSet(std::shared_ptr<IFileSystem> filesystem, const OSServices::LibVersionDesc&);
		~ArchiveCacheSet();
	protected:
		typedef std::pair<uint64, std::shared_ptr<::Assets::ArchiveCache>> Archive;
		std::vector<Archive>    _archives;
		Threading::Mutex        _archivesLock;
		std::shared_ptr<IFileSystem> _filesystem;
		OSServices::LibVersionDesc	_versionDesc;
	};

}
