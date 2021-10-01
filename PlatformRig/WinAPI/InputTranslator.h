// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../Math/Vector.h"
#include "../Utility/UTFUtils.h"
#include <vector>

namespace PlatformRig
{
	class IInputListener; class InputContext; class InputSnapshot;

    class InputTranslator
    {
    public:
        void    OnMouseMove         (signed newX,       signed newY);
        void    OnMouseButtonChange (signed newX, signed newY, unsigned index,    bool newState);
        void    OnMouseButtonDblClk (signed newX, signed newY, unsigned index);
        void    OnKeyChange         (unsigned keyCode,  bool newState);
        void    OnChar              (ucs2 chr);
        void    OnMouseWheel        (signed wheelDelta);
        void    OnFocusChange       ();

        void    AddListener         (std::weak_ptr<IInputListener> listener);

        Int2    GetMousePosition();

        InputTranslator(const void* hwnd);
        ~InputTranslator();

    private:
        class Pimpl;
        std::unique_ptr<Pimpl> _pimpl;

        unsigned        GetMouseButtonState() const;

        void            Publish(const InputSnapshot& snapShot);
        const char*     AsKeyName(unsigned keyCode);
    };
}