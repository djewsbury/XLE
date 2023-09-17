// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#if MAT_TRANSMITTED_SPECULAR==1
    #include "../SceneEngine/Lighting/Diagrams/GGXTransmission.hlsl"
#endif

float GGXTransmissionFresnel(float3 i, float3 ot, float F0, float iorIncident, float iorOutgoing)
{
    // When calculation the fresnel effect, should we consider the incident direction, or
    // outgoing direction? Or both?
    // Outgoing makes more sense, because this will more closely match the calculation we use
    // for the reflection case. Actually; couldn't we just reuse the result we got with the
    // reflection case?
    // Walter07 paper uses i and Ht. He also uses abs() here, but that produces very strange results.
    // float HdotI = max(0, dot(CalculateHt(i, ot, iorIncident, iorOutgoing), i));
    float HdotI = max(0.0f, dot(CalculateHt(i, ot, iorIncident, iorOutgoing), i));
    // return 1.f - lerp(F0, 1.f, SchlickFresnelCore(HdotI));
    // return lerp(1.f - F0, 0.f, SchlickFresnelCore(HdotI));
    return (1.f - F0) * (1.f - SchlickFresnelCore(HdotI));
}

float3 CalculateSpecularTransmission(
    float3 normal, float3 directionToEye,
    float3 negativeLightDirection, float3 halfVector,
    SpecularParameters parameters)
{

    // Calculate specular light transmitted through
    // For most cases of transmission, there should actually be 2 interfaces
    //		-- 	when the light enters the material, and when it exits it.
    // The light will bend at each interface. So, to calculate the refraction
    // properly, we really need to know the thickness of the object. That will
    // determine how much the light actually bends. If we know the thickness,
    // we can calculate an approximate bending due to refaction. But for now, ignore
    // thickness.
    //
    // It may be ok to consider the microfacet distribution only on a single
    // interface.
    // Walter's implementation is based solving for a surface pointing away from
    // the camera. And, as he mentions in the paper, the higher index of refraction
    // should be inside the material (ie, on the camera side). Infact, this is
    // required to get the transmission half vector, ht, pointing in the right direction.
    // So, in effect we're solving for the microfacets on an imaginary back face where
    // the light first entered the object on it's way to the camera.
    //
    // In theory, we could do the fresnel calculation for r, g & b separately. But we're
    // just going to ignore that and only do a single channel. This might produce an
    // incorrect result for metals; but why would we get a large amount of transmission
    // through metals?
    float transmitted = 0.f;

    #if MAT_TRANSMITTED_SPECULAR==1
            // note -- constant ior. Could be tied to "specular" parameter?
        const float iorIncident = 1.f;
        const float iorOutgoing = SpecularTransmissionIndexOfRefraction;
        GGXTransmission(
            parameters.roughness, iorIncident, iorOutgoing,
            negativeLightDirection, directionToEye, -normal,        // (note flipping normal)
            transmitted);

        transmitted *= GGXTransmissionFresnel(
            negativeLightDirection, directionToEye, parameters.F0.g,
            iorIncident, iorOutgoing);

        #if MAT_DOUBLE_SIDED_LIGHTING
            transmitted *= abs(dot(negativeLightDirection, -normal));
        #else
            transmitted *= max(0, dot(negativeLightDirection, -normal));
        #endif

    #endif

    return parameters.transmission * transmitted;
}
