// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../ResourceDesc.h"
#include "../../Utility/StringUtils.h"
#include <functional>
#include <memory>

namespace RenderCore { namespace BufferUploads { class IAsyncDataSource; }}

namespace RenderCore { namespace Assets
{
    namespace TextureLoaderFlags
    {
        enum Enum { GenerateMipmaps = 1<<0 };
        using BitField = unsigned;
    }
    
    using TextureLoaderSignature = std::shared_ptr<BufferUploads::IAsyncDataSource>(StringSection<>, TextureLoaderFlags::BitField);
    std::function<TextureLoaderSignature> CreateDDSTextureLoader();
    std::function<TextureLoaderSignature> CreateWICTextureLoader();
    std::function<TextureLoaderSignature> CreateHDRTextureLoader();

    struct DDSBreakdown
	{
		TextureDesc _textureDesc;

		struct Subresource
		{
			const void* _data;
			TexturePitches _pitches;
		};
		// _subresources indexed by (arrayLayer * _textureDesc._mipCount + mip)
		std::vector<Subresource> _subresources;
	};

	std::optional<DDSBreakdown> BuildDDSBreakdown(IteratorRange<const void*> data, StringSection<> filename);
}}
