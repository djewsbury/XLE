// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "Resource.h"

namespace RenderCore { namespace Metal_AppleMetal
{
    class ShaderResourceView : public Resource
    {
    public:
        bool                        IsGood() const { return false; }
    };

}}