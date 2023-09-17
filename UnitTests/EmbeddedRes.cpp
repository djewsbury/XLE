// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "../Assets/MemoryFile.h"
#define INCBIN_PREFIX
#include "../Foreign/incbin/incbin.h"

#define _INCBIN(FN, ID) INCBIN(FN, ID)
#define X(FN, ID) _INCBIN(INCBIN_CAT(EmbedFile, ID), "xleres/core/" INCBIN_STRINGIZE(FN));
#define X2(FN, ID) _INCBIN(INCBIN_CAT(EmbedFile, ID), "Working/Game/xleres/" INCBIN_STRINGIZE(FN));
#include "EmbeddedResFileList.h"
#undef X
#undef X2

#define X(FN, ID) { INCBIN_STR(FN), IteratorRange<const uint8_t*>{                              												\
	INCBIN_CONCATENATE( INCBIN_CONCATENATE(INCBIN_PREFIX, INCBIN_CONCATENATE(EmbedFile, ID)), INCBIN_STYLE_IDENT(DATA) ),      					\
	(const uint8_t*)&INCBIN_CONCATENATE( INCBIN_CONCATENATE(INCBIN_PREFIX, INCBIN_CONCATENATE(EmbedFile, ID)), INCBIN_STYLE_IDENT(END) )        \
	}},                                                                                         												\
	/**/
#define X2(FN, ID) X(FN, ID)

static const std::pair<const char*, IteratorRange<const uint8_t*>> s_embeddedResourceList[] = {
	#include "EmbeddedResFileList.h"
};
#undef X
#undef X2

namespace UnitTests
{
	std::shared_ptr<::Assets::IFileSystem> CreateEmbeddedResFileSystem()
	{
		// note that we end up creating a copy of every file here -- which really isn't ideal
		std::unordered_map<std::string, IteratorRange<const void*>> filesAndContents;
		filesAndContents.reserve(dimof(s_embeddedResourceList));
		for (const auto& s:s_embeddedResourceList)
			filesAndContents.insert(std::make_pair(s.first, s.second));
		return ::Assets::CreateFileSystem_Memory(
			filesAndContents, FilenameRules { '/', true }, 
			::Assets::FileSystemMemoryFlags::EnableChangeMonitoring | ::Assets::FileSystemMemoryFlags::UseModuleModificationTime);
	}
}
