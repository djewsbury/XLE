// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "ChunkFile.h"
#include "AssetsCore.h"
#include "IFileSystem.h"
#include "../OSServices/AttachableLibrary.h"
#include "../OSServices/RawFS.h"
#include "../Utility/Streams/Stream.h"
#include "../Utility/PtrUtils.h"
#include "../Utility/MemoryUtils.h"
#include <tuple>

namespace Assets { namespace ChunkFile
{
    ChunkFileHeader MakeChunkFileHeader(unsigned chunkCount, const char buildVersionString[], const char buildDateString[])
    {
        ChunkFileHeader header;
        XlZeroMemory(header);
        header._magic = MagicHeader;
        header._fileVersionNumber = ChunkFileVersion;
        XlCopyString(header._buildVersion, buildVersionString);
        XlCopyString(header._buildDate, buildDateString);
        header._chunkCount = chunkCount;
        return header;
    }

    std::vector<ChunkHeader> LoadChunkTable(::Assets::IFileInterface& file)
    {
        ChunkFileHeader fileHeader;
        if (file.Read(&fileHeader, sizeof(ChunkFileHeader), 1) != 1) {
            throw ::Exceptions::BasicLabel("Incomplete file header");
        }

        if (fileHeader._magic != MagicHeader) {
            throw ::Exceptions::BasicLabel("Unrecognised format");
        }

        if (fileHeader._fileVersionNumber != ChunkFileVersion) {
            throw ::Exceptions::BasicLabel("Bad chunk file format");
        }

        std::vector<ChunkHeader> result;
        result.resize(fileHeader._chunkCount);
        auto readCount = file.Read(AsPointer(result.begin()), sizeof(ChunkHeader), fileHeader._chunkCount);
        if (readCount != fileHeader._chunkCount) {
            throw ::Exceptions::BasicLabel("Incomplete file header");
        }

        return result;
    }

    ChunkFile::ChunkHeader FindChunk(
        const utf8 filename[],
        std::vector<ChunkFile::ChunkHeader>& hdrs,
        ChunkFile::TypeIdentifier chunkType,
        unsigned expectedVersion)
    {
        ChunkFile::ChunkHeader scaffoldChunk;
        for (auto i=hdrs.begin(); i!=hdrs.end(); ++i) {
            if (i->_chunkTypeCode == chunkType) {
                scaffoldChunk = *i;
                break;
            }
        }

        if (!scaffoldChunk._fileOffset) {
            throw ::Exceptions::BasicLabel("Missing could not find chunk in chunk file: %s", filename);
        }

        if (scaffoldChunk._chunkVersion != expectedVersion) {
            throw ::Exceptions::BasicLabel("Incorrect chunk version: %s", filename);
        }

        return scaffoldChunk;
    }

    std::unique_ptr<uint8[]> RawChunkAsMemoryBlock(
        const utf8 filename[],
        ChunkFile::TypeIdentifier chunkType,
        unsigned expectedVersion)
    {
        auto file = Assets::MainFileSystem::OpenFileInterface(filename, "rb");
        auto chunks = ChunkFile::LoadChunkTable(*file);

        auto scaffoldChunk = FindChunk(filename, chunks, chunkType, expectedVersion);
        auto rawMemoryBlock = std::make_unique<uint8[]>(scaffoldChunk._size);
        file->Seek(scaffoldChunk._fileOffset);
        file->Read(rawMemoryBlock.get(), 1, scaffoldChunk._size);
        return rawMemoryBlock;
    }

    void BuildChunkFile(
        IFileInterface& file,
        IteratorRange<const ICompileOperation::SerializedArtifact*> chunks,
        const OSServices::LibVersionDesc& versionInfo,
        std::function<bool(const ICompileOperation::SerializedArtifact&)> predicate)
    {
        unsigned chunksForMainFile = 0;
		for (const auto& c:chunks)
            if (!predicate || predicate(c))
                ++chunksForMainFile;

        using namespace Assets::ChunkFile;
        auto header = MakeChunkFileHeader(
            chunksForMainFile, 
            versionInfo._versionString, versionInfo._buildDateString);
        file.Write(&header, sizeof(header), 1);

        unsigned trackingOffset = unsigned(file.TellP() + sizeof(ChunkHeader) * chunksForMainFile);
        for (const auto& c:chunks)
            if (!predicate || predicate(c)) {
				ChunkFile::ChunkHeader hdr;
				hdr._chunkTypeCode = c._chunkTypeCode;
				hdr._chunkVersion = c._version;
				XlCopyString(hdr._name, c._name);
                hdr._fileOffset = trackingOffset;
				hdr._size = (ChunkFile::SizeType)c._data->size();
                file.Write(&hdr, sizeof(hdr), 1);
                trackingOffset += hdr._size;
            }

        for (const auto& c:chunks)
            if (!predicate || predicate(c))
                file.Write(AsPointer(c._data->begin()), c._data->size(), 1);
    }

///////////////////////////////////////////////////////////////////////////////////////////////////
    namespace Internal
    {
        template<typename Writer>
            SimpleChunkFileWriterT<Writer>::SimpleChunkFileWriterT(
				Writer&& writer,
                unsigned chunkCount, const char buildVersionString[], const char buildDateString[])
        : _writer(std::move(writer))
        , _chunkCount(chunkCount)
        {
            _activeChunkStart = 0;
            _hasActiveChunk = false;
            _activeChunkIndex = 0;
            
            ChunkFileHeader fileHeader = MakeChunkFileHeader(
                chunkCount, buildVersionString, buildDateString);
			_writer.Write(&fileHeader, sizeof(fileHeader), 1);
            for (unsigned c=0; c<chunkCount; ++c) {
                ChunkHeader t;
				_writer.Write(&t, sizeof(ChunkHeader), 1);
            }
        }

        template<typename Writer>
            SimpleChunkFileWriterT<Writer>::~SimpleChunkFileWriterT()
        {
            if (_hasActiveChunk) {
                FinishCurrentChunk();
            }
            assert(_activeChunkIndex == _chunkCount);
        }

        template<typename Writer>
            void SimpleChunkFileWriterT<Writer>::BeginChunk(   
                ChunkFile::TypeIdentifier type,
                unsigned version, const char name[])
        {
            if (_hasActiveChunk) {
                FinishCurrentChunk();
            }

            _activeChunk._chunkTypeCode = type;
            _activeChunk._chunkVersion = version;
            XlCopyString(_activeChunk._name, name);
            _activeChunkStart = _writer.TellP();
            _activeChunk._fileOffset = (ChunkFile::SizeType)_activeChunkStart;
            _activeChunk._size = 0; // unknown currently

            _hasActiveChunk = true;
        }

        template<typename Writer>
            void SimpleChunkFileWriterT<Writer>::FinishCurrentChunk()
        {
            using namespace Assets::ChunkFile;
            auto oldLoc = _writer.TellP();
            auto chunkHeaderLoc = sizeof(ChunkFileHeader) + _activeChunkIndex * sizeof(ChunkHeader);
            _writer.Seek(chunkHeaderLoc);
            _activeChunk._size = (ChunkFile::SizeType)std::max(size_t(0), oldLoc - _activeChunkStart);
			_writer.Write(&_activeChunk, sizeof(ChunkHeader), 1);
            _writer.Seek(oldLoc);
            ++_activeChunkIndex;
            _hasActiveChunk = false;
        }

		template<typename Writer>
			size_t SimpleChunkFileWriterT<Writer>::Write(const void *buffer, size_t size, size_t count) never_throws
		{
			return _writer.Write(buffer, size, count);
		}

		template<typename Writer>
			size_t SimpleChunkFileWriterT<Writer>::Seek(size_t offset, OSServices::FileSeekAnchor anchor) never_throws
		{
			return _writer.Seek(offset, anchor);
		}

		template<typename Writer>
			size_t SimpleChunkFileWriterT<Writer>::TellP() const never_throws
		{
			return _writer.TellP();
		}

		template<typename Writer>
			void SimpleChunkFileWriterT<Writer>::Flush() never_throws
		{
			return _writer.Flush();
		}
        
    }

    template class Internal::SimpleChunkFileWriterT<OSServices::BasicFile>;

}}

