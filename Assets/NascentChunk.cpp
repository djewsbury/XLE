// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "NascentChunk.h"
#include "BlockSerializer.h"

namespace Assets
{
	Blob AsBlob(IteratorRange<const void*> copyFrom)
	{
		return std::make_shared<std::vector<uint8_t>>((const uint8_t*)copyFrom.begin(), (const uint8_t*)copyFrom.end());
	}

	Blob AsBlob(const BlockSerializer& serializer)
	{
		auto block = serializer.AsMemoryBlock();
		size_t size = Block_GetSize(block.get());
		return AsBlob(MakeIteratorRange(block.get(), PtrAdd(block.get(), size)));
	}

	Blob AsBlob(const std::string& str)
	{
		return AsBlob(MakeIteratorRange(str));
	}

	Blob AsBlob(StringSection<char> str)
	{
		return AsBlob(MakeIteratorRange(str.begin(), str.end()));
	}

	Blob AsBlob(const char* str)
	{
		return AsBlob(MakeStringSectionNullTerm(str));
	}

	std::string AsString(const Blob& blob)
	{
		if (!blob) return {};
		return std::string((const char*)AsPointer(blob->begin()), (const char*)AsPointer(blob->end()));
	}
}

