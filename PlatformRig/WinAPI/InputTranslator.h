// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../InputListener.h"
#include "../../Math/Vector.h"
#include "../../Utility/UTFUtils.h"
#include <vector>

namespace PlatformRig
{
    class InputTranslator
    {
    public:
        InputSnapshot    OnMouseMove         (signed newX,       signed newY);
        InputSnapshot    OnMouseButtonChange (signed newX, signed newY, unsigned index,    bool newState);
        InputSnapshot    OnMouseButtonDblClk (signed newX, signed newY, unsigned index);
        InputSnapshot    OnKeyChange         (unsigned keyCode,  bool newState);
        InputSnapshot    OnChar              (ucs2 chr);
        InputSnapshot    OnMouseWheel        (signed wheelDelta);
        InputSnapshot    OnFocusChange       ();

        Int2    GetMousePosition();

        InputTranslator(const void* hwnd);
        ~InputTranslator();

    private:
        class Pimpl;
        std::unique_ptr<Pimpl> _pimpl;

        unsigned        GetMouseButtonState() const;
        const char*     AsKeyName(unsigned keyCode);
    };
}