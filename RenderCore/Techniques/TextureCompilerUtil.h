// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include <memory>
#include <vector>

namespace BufferUploads { class IAsyncDataSource; }
namespace Assets { class DependentFileState; }
namespace RenderCore { class TextureDesc; }

namespace RenderCore { namespace Techniques
{
    struct ProcessedTexture
    {
    public:
        std::shared_ptr<BufferUploads::IAsyncDataSource> _newDataSource;
        std::vector<::Assets::DependentFileState> _depFileStates;
    };
    ProcessedTexture EquRectToCube(BufferUploads::IAsyncDataSource& dataSrc, const TextureDesc& targetDesc);
}}
