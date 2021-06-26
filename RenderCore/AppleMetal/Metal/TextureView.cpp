// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "TextureView.h"

namespace RenderCore { namespace Metal_AppleMetal
{
////////////////////////////////////////////////////////////////////////////////////////////////////

    ShaderResourceView::ShaderResourceView(const ObjectFactory& factory, const std::shared_ptr<IResource>& resource, const TextureViewDesc& window)
    : _resource(std::static_pointer_cast<Resource>(resource)), _window(window)
    {
        _hasMipMaps = _resource->GetDesc()._textureDesc._mipCount > 1;
    }

    ShaderResourceView::ShaderResourceView(const std::shared_ptr<IResource>& resource, const TextureViewDesc& window)
    : ShaderResourceView(GetObjectFactory(), resource, window) {}

    ShaderResourceView::ShaderResourceView()
    : _hasMipMaps(false)
    {}
}}
