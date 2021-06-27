// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "ObjectFactory.h"
#include "../../StateDesc.h"
#include "../../Types.h"
#include "../../IDevice.h"

namespace RenderCore { namespace Metal_AppleMetal
{
    class GraphicsEncoder;
    class ObjectFactory;

    class SamplerState : public ISampler
    {
    public:
        // --------------- Apple Metal specific interface ---------------
        void Apply(GraphicsEncoder& encoder, unsigned samplerIndex, ShaderStage stage) const never_throws;
        SamplerDesc GetDesc() const;

        SamplerState(ObjectFactory&, const SamplerDesc&);
        SamplerState();
        ~SamplerState();
    private:
        OCPtr<AplMtlSamplerState> _underlying; // <MTLSamplerState>
        SamplerDesc _desc;
    };

}}

