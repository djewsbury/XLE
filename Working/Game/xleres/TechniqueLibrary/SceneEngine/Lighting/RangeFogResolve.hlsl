// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#if !defined(RANGE_FOG_RESOLVE_H)
#define RANGE_FOG_RESOLVE_H

#include "../../LightingEngine/LightDesc.hlsl"

void LightResolve_RangeFog(RangeFogDesc desc, float distance, out float outscatterScale, out float3 inscatter)
{
    // Based purely on distance, calculate the effect of range fog
    // Attentuation performed using base Beer-Lambert law
    // Just basic linear inscatter currently. This won't correctly deal with the amount of
    // inscattered light that later gets outscattered.
    //
    // Note that we're doing the outscatter in monochrome for efficiency. We can still use
    // inscatter to affect the color of the fog.
    outscatterScale = exp(-desc.MonochromeOpticalThickness * distance);
    // Approximate the amount of inscatter that gets outscattered later on... This helps
    // to give the inscatter a slightly less linear shape (against distance), which can avoid
    // excessive brightening in the distance (but tends to replace the brightening with darkening)
    float inoutApprox = sqrt(outscatterScale);
    // float inoutApprox = exp(-desc.MonochromeOpticalThickness * .5f * distance);
    inscatter = desc.Inscatter * distance * inoutApprox;
}

#endif
