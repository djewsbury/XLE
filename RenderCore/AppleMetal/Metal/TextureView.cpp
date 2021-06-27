// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "TextureView.h"

namespace RenderCore { namespace Metal_AppleMetal
{
////////////////////////////////////////////////////////////////////////////////////////////////////

    std::shared_ptr<IResource> ResourceView::GetResource() const
    {
        return _resource;
    }

    AplMtlTexture* ResourceView::GetTexture() const
    {
        return _resource->GetTexture();
    }

    AplMtlBuffer* ResourceView::GetBuffer() const
    {
        return _resource->GetBuffer();
    }

    ResourceView::ResourceView(ObjectFactory& factory, const std::shared_ptr<IResource>& image, BindFlag::Enum usage, const TextureViewDesc& window)
    : _resource(std::static_pointer_cast<Resource>(image)), _window(window)
    {
    }

    ResourceView::ResourceView(ObjectFactory& factory, const std::shared_ptr<IResource>& buffer, Format texelBufferFormat, unsigned rangeOffset, unsigned rangeSize)
    {
        Throw(std::runtime_error("Texel buffer objects are not supported on Apple Metal"));
    }

    ResourceView::ResourceView(ObjectFactory& factory, const std::shared_ptr<IResource>& buffer, unsigned rangeOffset, unsigned rangeSize)
    : _resource(std::static_pointer_cast<Resource>(buffer))
    , _bufferRange(rangeOffset, rangeSize)
    {
    }

    ResourceView::ResourceView()
    {}

    ResourceView::~ResourceView()
    {}

}}
