// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "Resource.h"
#include "ObjectFactory.h"
#include "../../IDevice.h"
#include "../../Types.h"
#include "../../ResourceDesc.h"
#include "../../../Utility/OCUtils.h"
#include <memory>

namespace RenderCore { namespace Metal_AppleMetal
{
    class ResourceView : public IResourceView
    {
    public:
        // --------------- Apple Metal specific interface ---------------
        const std::shared_ptr<IResource>& GetResource() const override;
        const OCPtr<AplMtlTexture>& GetTexture() const;
        const OCPtr<AplMtlBuffer>& GetBuffer() const;
        const TextureViewDesc& GetTextureViewDesc() const { return _window; }
        std::pair<unsigned, unsigned> GetBufferRangeOffsetAndSize() const { return _bufferRange; }

        ResourceView(ObjectFactory& factory, const std::shared_ptr<IResource>& image, BindFlag::Enum usage, const TextureViewDesc& window);
        ResourceView(ObjectFactory& factory, const std::shared_ptr<IResource>& buffer, Format texelBufferFormat, unsigned rangeOffset, unsigned rangeSize);
        ResourceView(ObjectFactory& factory, const std::shared_ptr<IResource>& buffer, unsigned rangeOffset, unsigned rangeSize);
        ResourceView();
        ~ResourceView();

    private:
        TextureViewDesc _window;
        std::shared_ptr<Resource> _resource;
        std::pair<unsigned, unsigned> _bufferRange;
    };
}}
