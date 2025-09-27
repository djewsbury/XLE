// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "AssetsCore.h"
#include "BlockSerializer.h"
#include "../Utility/IteratorUtils.h"

namespace Assets
{
	class BlockSerializer;
	Blob AsBlob(const BlockSerializer& serializer);
	Blob AsBlob(IteratorRange<const void*>);

	template<typename Char>
		static Blob AsBlob(std::basic_stringstream<Char>& stream)
	{
		#if __cplusplus >= 202002L
			return ::Assets::AsBlob(MakeIteratorRange(strm.view().begin(), strm.view.end()));
		#else
			auto str = stream.str();
			return AsBlob(MakeIteratorRange(AsPointer(str.begin()), AsPointer(str.end())));
		#endif
	}

	template<typename Type>
		static Blob SerializeToBlob(const Type& obj)
	{
		BlockSerializer serializer;
		SerializationOperator(serializer, obj);
		return AsBlob(serializer);
	}
}
