// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../../RenderOverlays/DebuggingDisplay.h"
#include "../../RenderOverlays/IOverlayContext.h"

namespace SceneEngine 
{ 
    class IntersectionTestContext;
    class IIntersectionScene;
}
namespace RenderCore { namespace Techniques { class CameraDesc; class ParsingContext; } }

namespace ToolsRig
{
    using namespace RenderOverlays;
    using namespace RenderOverlays::DebuggingDisplay;

///////////////////////////////////////////////////////////////////////////////////////////////////

    class IManipulator;

    Rect DrawManipulatorControls(
        IOverlayContext& context, DebuggingDisplay::Layout& layout, Interactables&interactables, InterfaceState& interfaceState,
        IManipulator& manipulator, const char title[]);

    bool HandleManipulatorsControls(
        InterfaceState& interfaceState, const OSServices::InputSnapshot& input, 
        IManipulator& manipulator);

///////////////////////////////////////////////////////////////////////////////////////////////////

    template<typename T, typename T2> size_t ManipulatorParameterOffset(T2 T::*member)
    {
            //  we want the offset of the member relative to a casted IManipulator pointer
            //  (here, t should normally be equal to basePtr)
        T* t = (T*)nullptr;
        IManipulator* basePtr = (IManipulator*)t;
        return size_t(&(t->*member)) - size_t(basePtr);
    }

    std::pair<Float3, bool> FindTerrainIntersection(
        const SceneEngine::IntersectionTestContext& context, const SceneEngine::IIntersectionScene& scene,
        const Int2 screenCoords);

    inline Float2 RoundDownToInteger(Float2 input) { return Float2(XlFloor(input[0] + 0.5f), XlFloor(input[1] + 0.5f)); }
    inline Float2 RoundUpToInteger(Float2 input)   { return Float2(XlCeil(input[0] - 0.5f), XlCeil(input[1] - 0.5f)); }
}

