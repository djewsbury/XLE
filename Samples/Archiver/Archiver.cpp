// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "../../Assets/XPak_Internal.h"
#include "../../Utility/Streams/PathUtils.h"
#include "../../Utility/StringUtils.h"
#include "../../Formatters/TextFormatter.h"
#include "../../Formatters/StreamDOM.h"
#include "../../OSServices/RawFS.h"
#include "../../OSServices/WinAPI/IncludeWindows.h"
#include "../../Foreign/FastLZ/fastlz.h"
#include <filesystem>
#include <iostream>

FilenameRules s_filenameRules('/', true);

struct CmdLine
{
	std::string _input = "./";
	std::string _output = "out.pak";

	CmdLine(int argc, char** argv)
	{
		if (argc > 1) {
			StringSection<> cmdLine = argv[1];
			Formatters::TextInputFormatter<char> formatter(cmdLine);
			auto doc = Formatters::MakeStreamDOM(formatter);
			if (auto i = doc.RootElement().Attribute("in"))
				_input = i.Value().AsString();
			if (auto i = doc.RootElement().Attribute("out"))
				_output = i.Value().AsString();
		}
	}
};

int main(int argc, char** argv)
{
	TRY {

		CmdLine cmdLine { argc, argv };

		std::cout << "Creating archive " << cmdLine._output << " from source files in " << cmdLine._input << std::endl;

		struct PendingFile
		{
			uint64_t _size;
			std::filesystem::path _path;
			unsigned _idx;
			uint64_t _hash;
			std::string _archiveName;
		};
		std::vector<PendingFile> pendingFiles;
		unsigned stringTableIterator = 0;
		
		// Collect up the list of input files and start generating the string and hash tables
		auto root = std::filesystem::path(cmdLine._input);
		auto baseInSrc = MakeSplitPath(root.lexically_normal().string()).Rebuild(s_filenameRules);
		auto baseInSrcSplit = MakeSplitPath(baseInSrc);

		for (const auto& entry:std::filesystem::recursive_directory_iterator(root)) {
			if (!entry.is_regular_file()) continue;
			auto fn = entry.path().lexically_normal().string();
			if (XlEqStringI(MakeStringSection(fn), MakeStringSection(cmdLine._output))) continue;

			auto normalizedEntry = MakeSplitPath(fn).Rebuild(s_filenameRules);
			auto normalizedEntryRelative = MakeRelativePath(baseInSrcSplit, MakeSplitPath(normalizedEntry), s_filenameRules);

			auto hash = HashFilenameAndPath(MakeStringSection(normalizedEntryRelative), s_filenameRules);
			pendingFiles.push_back(PendingFile { (uint64_t)entry.file_size(), entry, (unsigned)pendingFiles.size(), hash, normalizedEntryRelative });
			stringTableIterator += unsigned(normalizedEntryRelative.size()) + 1;
		}

		// sort the file entries to put the largest first (though this is the largest decompressed size, not compressed size)
		std::sort(pendingFiles.begin(), pendingFiles.end(), [](const auto& lhs, const auto& rhs) { return lhs._size > rhs._size; });

		// Start writing the output file, beginning with spacing out some room for the headers

		std::vector<uint8_t> headers; 
		headers.resize(sizeof(Assets::Internal::XPakStructures::Header) + sizeof(Assets::Internal::XPakStructures::FileEntry) * pendingFiles.size() + sizeof(uint64_t) * pendingFiles.size() + stringTableIterator);

		while ((headers.size() % 8) != 0)
			headers.push_back(0);

		OSServices::BasicFile out { cmdLine._output.c_str(), "wb", 0 };
		out.Write(headers.data(), headers.size(), 1);

		std::vector<uint8_t> compressionBuffer;
		uint64_t outIterator = headers.size();

		auto* outFileEntry = (Assets::Internal::XPakStructures::FileEntry*)PtrAdd(headers.data(), sizeof(Assets::Internal::XPakStructures::Header));
		auto* outHashTable = (uint64_t*)PtrAdd(headers.data(), sizeof(Assets::Internal::XPakStructures::Header) + sizeof(Assets::Internal::XPakStructures::FileEntry) * pendingFiles.size());
		auto* outStringTable = (char*)PtrAdd(headers.data(), sizeof(Assets::Internal::XPakStructures::Header) + sizeof(Assets::Internal::XPakStructures::FileEntry) * pendingFiles.size() + sizeof(uint64_t) * pendingFiles.size());
		
		stringTableIterator = 0;
		uint64_t wholeArchiveHashValue = DefaultSeed64;

		std::vector<uint64_t> sortedHashes;
		sortedHashes.reserve(pendingFiles.size());
		for (const auto& entry:pendingFiles) sortedHashes.push_back(entry._hash);
		std::sort(sortedHashes.begin(), sortedHashes.end());

		// For each file, do the compression and append to the file
		for (const auto& entry:pendingFiles) {

			OSServices::MemoryMappedFile input { entry._path.string().c_str(), 0, "rb", 0 };
			auto data = input.GetData();
			wholeArchiveHashValue = Hash64(data, wholeArchiveHashValue);

			size_t requiredBufferSize = std::max(66ull, input.GetData().size() + input.GetData().size()/8ull);
			if (compressionBuffer.size() < requiredBufferSize)
				compressionBuffer.resize(requiredBufferSize);

			auto compressedSize = (uint64_t)fastlz_compress_level(2, data.begin(), (int)data.size(), compressionBuffer.data());
			if (compressedSize && compressedSize < data.size()) {

				// todo -- consider compressing large files in blocks so we can do some progressive decompression
				out.Write(compressionBuffer.data(), 1, compressedSize);

			} else {

				compressedSize = data.size();
				out.Write(data.data(), 1, data.size());

			}

			auto i = std::lower_bound(sortedHashes.begin(), sortedHashes.end(), entry._hash);
			assert(i != sortedHashes.end() && *i == entry._hash);
			auto idxSortedOrder = std::distance(sortedHashes.begin(), i);

			outFileEntry[idxSortedOrder]._offset = outIterator;
			outFileEntry[idxSortedOrder]._decompressedSize = data.size();
			outFileEntry[idxSortedOrder]._compressedSize = compressedSize;
			outFileEntry[idxSortedOrder]._stringTableOffset = stringTableIterator; 
			outFileEntry[idxSortedOrder]._flags = 0;
			outIterator += compressedSize;

			for (auto c:entry._archiveName)
				outStringTable[stringTableIterator++] = c;
			outStringTable[stringTableIterator++] = 0;

			outHashTable[idxSortedOrder] = entry._hash;

		}

		auto& hdr = *(Assets::Internal::XPakStructures::Header*)headers.data();
		hdr._majik = 'KAPX';
		hdr._version = 0;
		hdr._fileCount = (uint32_t)pendingFiles.size();
		hdr._wholeArchiveHashValue = wholeArchiveHashValue;
		hdr._fileEntriesOffset = (unsigned)sizeof(Assets::Internal::XPakStructures::Header);
		hdr._hashTableOffset = sizeof(Assets::Internal::XPakStructures::Header) + sizeof(Assets::Internal::XPakStructures::FileEntry) * pendingFiles.size();
		hdr._stringTableOffset = sizeof(Assets::Internal::XPakStructures::Header) + sizeof(Assets::Internal::XPakStructures::FileEntry) * pendingFiles.size() + sizeof(uint64_t) * pendingFiles.size();

		out.Seek(0);
		out.Write(headers.data(), 1, headers.size());

	} CATCH(const std::exception& e) {

		std::cout << "Failed with error: " << e.what() << std::endl;

	} CATCH_END

	return 0;
}
