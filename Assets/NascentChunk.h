// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "AssetsCore.h"
#include "ChunkFile.h"
#include "BlockSerializer.h"
#include "../Utility/IteratorUtils.h"
#include <memory>

namespace Assets { class BlockSerializer; }

namespace Assets
{
	// todo -- NascentChunk client should use ICompileOperation::SerializedArtifact instead
	
	class NascentChunk
	{
	public:
		ChunkFile::ChunkHeader _hdr;
		Blob _data;
	};

	using NascentChunkArray = std::shared_ptr<std::vector<NascentChunk>>;
	NascentChunkArray MakeNascentChunkArray(const std::initializer_list<NascentChunk>& inits);

	Blob AsBlob(const BlockSerializer& serializer);
	Blob AsBlob(IteratorRange<const void*>);

	template<typename Char>
		static Blob AsBlob(std::basic_stringstream<Char>& stream)
	{
		auto str = stream.str();
		return AsBlob(MakeIteratorRange(AsPointer(str.begin()), AsPointer(str.end())));
	}

	template<typename Type>
		static Blob SerializeToBlob(const Type& obj)
	{
		BlockSerializer serializer;
		SerializationOperator(serializer, obj);
		return AsBlob(serializer);
	}
}
