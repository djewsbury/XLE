// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include <cstdint>

namespace Assets { namespace Internal { namespace XPakStructures
{

	#pragma pack(push)
	#pragma pack(1)

	struct Header
	{
		uint32_t _majik;
		uint32_t _version;
		uint32_t _fileCount;
		uint64_t _fileEntriesOffset;
		uint64_t _hashTableOffset;;
		uint64_t _stringTableOffset;
		uint64_t _reserved[8];
	};

	struct FileEntry
	{
		uint64_t _offset;
		uint64_t _compressedSize;
		uint64_t _decompressedSize;
		uint64_t _contentsHash;
		uint32_t _stringTableOffset;
		uint32_t _flags;
	};

	#pragma pack(pop)

}}}

