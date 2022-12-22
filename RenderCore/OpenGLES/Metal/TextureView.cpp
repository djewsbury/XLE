// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "TextureView.h"
#include "../../RenderUtils.h"
#include "IncludeGLES.h"

namespace RenderCore { namespace Metal_OpenGLES
{
    static std::shared_ptr<Resource> AsResource(const std::shared_ptr<IResource>& resource)
    {
        auto* result = (Resource*)resource->QueryInterface(TypeHashCode<Resource>);
        if (!result || result != resource.get())
            Throw(::Exceptions::BasicLabel("Unexpected resource type passed to texture view"));
        return std::static_pointer_cast<Resource>(resource);
    }

    ShaderResourceView::ShaderResourceView() : _hasMipMaps(false) {}

    ShaderResourceView::ShaderResourceView(const ObjectFactory& factory, const std::shared_ptr<IResource>& resource, const TextureViewDesc& window)
    : _resource(AsResource(resource))
    , _window(window)
    {
        _hasMipMaps = _resource->GetDesc()._textureDesc._mipCount > 1;
        // todo -- handle "view" transformation
    }

    ShaderResourceView::ShaderResourceView(const std::shared_ptr<IResource>& resource, const TextureViewDesc& window)
    : ShaderResourceView(GetObjectFactory(*resource.get()), resource, window)
    {
        _hasMipMaps = _resource->GetDesc()._textureDesc._mipCount > 1;
    }

////////////////////////////////////////////////////////////////////////////////////////////////////

    RenderTargetView::RenderTargetView() {}
    RenderTargetView::RenderTargetView(const ObjectFactory& factory, const std::shared_ptr<IResource>& resource, const TextureViewDesc& window)
    : _resource(AsResource(resource))
    , _window(window)
    {
        // todo -- handle "view" transformation
    }

    RenderTargetView::RenderTargetView(const std::shared_ptr<IResource>& resource, const TextureViewDesc& window)
    : RenderTargetView(GetObjectFactory(*resource.get()), resource, window)
    {
    }

////////////////////////////////////////////////////////////////////////////////////////////////////

    DepthStencilView::DepthStencilView() {}
    DepthStencilView::DepthStencilView(const ObjectFactory& factory, const std::shared_ptr<IResource>& resource, const TextureViewDesc& window)
    : _resource(AsResource(resource))
    , _window(window)
    {
        // todo -- handle "view" transformation
    }

    DepthStencilView::DepthStencilView(const std::shared_ptr<IResource>& resource, const TextureViewDesc& window)
    : DepthStencilView(GetObjectFactory(*resource.get()), resource, window)
    {
    }
}}
