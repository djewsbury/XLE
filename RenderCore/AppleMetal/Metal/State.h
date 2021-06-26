// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../../Types.h"
#include "../../StateDesc.h"
#include "../../../Core/Exceptions.h"

namespace RenderCore { namespace Metal_AppleMetal
{
    class GraphicsEncoder;

////////////////////////////////////////////////////////////////////////////////////////////////////

    class SamplerState
    {
    public:
        SamplerState(   FilterMode filter,
                        AddressMode addressU = AddressMode::Wrap,
                        AddressMode addressV = AddressMode::Wrap,
                        AddressMode addressW = AddressMode::Wrap,
                        CompareOp comparison = CompareOp::Never,
                        bool enableMipmaps = true);
        SamplerState(); // inexpensive default constructor

        void Apply(GraphicsEncoder& encoder, bool textureHasMipmaps, unsigned samplerIndex, ShaderStage stage) const never_throws;

        typedef SamplerState UnderlyingType;
        UnderlyingType GetUnderlying() const never_throws { return *this; }

    private:
        class Pimpl;
        std::shared_ptr<Pimpl> _pimpl;
    };

    class BlendState
    {
    public:
        BlendState();
        void Apply() const never_throws;
    };

////////////////////////////////////////////////////////////////////////////////////////////////////

}}

