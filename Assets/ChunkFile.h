// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "ICompileOperation.h"
#include "../OSServices/RawFS.h"
#include "../Utility/StringUtils.h"
#include "../Core/Types.h"
#include "../Core/SelectConfiguration.h"
#include <algorithm>
#include <vector>

namespace Utility { class OutputStream; }
namespace Assets { class IFileInterface; }
namespace OSServices { class LibVersionDesc; }

namespace Assets { namespace ChunkFile
{
    using TypeIdentifier = uint64_t;
    using SizeType = uint32_t;

    static const TypeIdentifier TypeIdentifier_Unknown = 0;

#pragma pack(push)
#pragma pack(1)
    class ChunkHeader
    {
    public:
        TypeIdentifier  _chunkTypeCode;
        unsigned        _chunkVersion;
        char            _name[32];      // fixed size for serialisation convenience
        SizeType        _fileOffset;
        SizeType        _size;

        ChunkHeader()
        {
            _chunkTypeCode = TypeIdentifier_Unknown;
            _chunkVersion = 0;
            std::fill(_name, &_name[dimof(_name)], '\0');
            _fileOffset = _size = 0;
        }

        ChunkHeader(TypeIdentifier type, unsigned version, 
                    const char name[], SizeType size = 0)
        {
            _chunkTypeCode = type;
            _chunkVersion = version;
            XlCopyString(_name, name);
            _fileOffset = 0;        // (not yet decided)
            _size = size;
        }
    }
	#if COMPILER_ACTIVE != COMPILER_TYPE_MSVC
		__attribute__((packed, aligned(1)))
	#endif
	;

    static const unsigned MagicHeader = uint32_t('X') | (uint32_t('L') << 8) | (uint32_t('E') << 16) | (uint32_t('~') << 24);
    static const unsigned ChunkFileVersion = 0;

    class ChunkFileHeader
    {
    public:
        uint32_t      _magic;
        unsigned    _fileVersionNumber;
        char        _buildVersion[64];
        char        _buildDate[64];
        unsigned    _chunkCount;
    }
	#if COMPILER_ACTIVE != COMPILER_TYPE_MSVC
		__attribute__((packed, aligned(1)))
	#endif
	;
#pragma pack(pop)

    ChunkFileHeader MakeChunkFileHeader(unsigned chunkCount, const char buildVersionString[], const char buildDateString[]);
    std::vector<ChunkHeader> LoadChunkTable(::Assets::IFileInterface& file);

    ChunkHeader FindChunk(
        const utf8 filename[], std::vector<ChunkHeader>& hdrs,
        TypeIdentifier chunkType, unsigned expectedVersion);

    std::unique_ptr<uint8[]> RawChunkAsMemoryBlock(
        const utf8 filename[], TypeIdentifier chunkType, unsigned expectedVersion);

    void BuildChunkFile(
		IFileInterface& file,
		IteratorRange<const ICompileOperation::SerializedArtifact*> chunks,
		const OSServices::LibVersionDesc& versionInfo,
		std::function<bool(const ICompileOperation::SerializedArtifact&)> predicate = {});

    namespace Internal
    {
        template<typename Writer>
            class SimpleChunkFileWriterT
        {
        public:
            SimpleChunkFileWriterT(
                Writer&& writer, 
				unsigned chunkCount,
                const char buildVersionString[], const char buildDateString[]);
            ~SimpleChunkFileWriterT();

            void BeginChunk(
                ChunkFile::TypeIdentifier type,
                unsigned version, const char name[]);
            void FinishCurrentChunk();

			size_t Write(const void *buffer, size_t size, size_t count) never_throws;
			size_t Seek(size_t offset, OSServices::FileSeekAnchor anchor = OSServices::FileSeekAnchor::Start) never_throws;
			size_t TellP() const never_throws;
			void Flush() never_throws;

        protected:
			Writer _writer;
            ChunkFile::ChunkHeader _activeChunk;
            size_t _activeChunkStart;
            bool _hasActiveChunk;
            unsigned _chunkCount;
            unsigned _activeChunkIndex;
        };
    }

    using SimpleChunkFileWriter = Internal::SimpleChunkFileWriterT<OSServices::BasicFile>;

}}

