// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "PlacementsDisplay.h"
#include "../../Math/Geometry.h"
#include "../../Utility/PtrUtils.h"

namespace PlatformRig { namespace Overlays
{

    void    PlacementsDisplay::Render(IOverlayContext& context, Layout& layout, Interactables&interactables, InterfaceState& interfaceState)
    {
#if 0
        Float3x4 placementsToWorld = Truncate(Float4x4(
            0.f, 1.f, 0.f, 0.f,
            1.f, 0.f, 0.f, 0.f,
            0.f, 0.f, 1.f, 0.f,
            0.f, 0.f, 0.f, 1.f));

        {
            unsigned objectCount = _placements->GetObjectReferenceCount();
            auto* objRef = _placements->GetObjectReferences();
            for (unsigned c=0; c<objectCount; ++c) {
                auto& obj = objRef[c];
                DrawBoundingBox(context, obj._worldSpaceBoundary, placementsToWorld, ColorB(0xff7fffff), 0x1);
            }
            for (unsigned c=0; c<objectCount; ++c) {
                auto& obj = objRef[c];
                DrawBoundingBox(context, obj._worldSpaceBoundary, placementsToWorld, ColorB(0xff7fffff), 0x2);
            }
        }
        {
            unsigned objectCount = _placements->GetVegetationReferenceCount();
            auto* objRef = _placements->GetVegetationReferences();
            for (unsigned c=0; c<objectCount; ++c) {
                auto& obj = objRef[c];
                DrawBoundingBox(context, obj._worldSpaceBoundary, placementsToWorld, ColorB(0xffffff7f), 0x1);
            }
            for (unsigned c=0; c<objectCount; ++c) {
                auto& obj = objRef[c];
                DrawBoundingBox(context, obj._worldSpaceBoundary, placementsToWorld, ColorB(0xffffff7f), 0x2);
            }
        }
#endif
    }

    PlacementsDisplay::PlacementsDisplay()
    {}

    PlacementsDisplay::~PlacementsDisplay()
    {}

}}

