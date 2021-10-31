// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#if !defined(GBUFFER_H)
#define GBUFFER_H

#include "../Framework/Binding.hlsl"
#include "../Framework/CommonResources.hlsl"    // for ClampingSampler

Texture2D       NormalsFittingTexture BIND_SEQ_T10;

static const bool SignedNormalOutput = true;

float ManualInterpolate(Texture2D tex, float2 texCoords)
{
    uint2 dims; tex.GetDimensions(dims.x, dims.y);
    float2 exploded = texCoords*float2(dims.xy);
    float result00 = tex[uint2(exploded) + uint2(0,0)].a;
    float result10 = tex[uint2(exploded) + uint2(1,0)].a;
    float result01 = tex[uint2(exploded) + uint2(0,1)].a;
    float result11 = tex[uint2(exploded) + uint2(1,1)].a;
    float2 fracPart = frac(exploded);
    return
          result00 * (1.0f-fracPart.x) * (1.0f-fracPart.y)
        + result10 * (     fracPart.x) * (1.0f-fracPart.y)
        + result01 * (1.0f-fracPart.x) * (     fracPart.y)
        + result11 * (     fracPart.x) * (     fracPart.y)
        ;
}

float3 GBuffer_CalculateBestFitNormal(float3 inputNormal)
{
	//
	//		Calculate the normal value that will
	//		quantize down to 8 bit best and still
	//		best represent this normal...
	//
	//		This requires a little more calculation
	//		in our shader, but it means we can write
	//		to a R8G8B8A8 output normal buffer, instead
	//		of a 16 bit floating point buffer.
	//
	//		see:
	//			http://advances.realtimerendering.com/s2010/Kaplanyan-CryEngine3(SIGGRAPH%202010%20Advanced%20RealTime%20Rendering%20Course).pdf
	//

		//	(assuming input normal is already normalized)

	// get unsigned normal for the cubemap lookup
	float3 vNormalUns = abs(inputNormal.rgb);
	// get the main axis for cubemap lookup
	float maxNAbs = max(vNormalUns.z, max(vNormalUns.x, vNormalUns.y));
	// get the texture coordinates in a collapsed cubemap
	float2 vTexCoord = vNormalUns.z<maxNAbs?(vNormalUns.y<maxNAbs?vNormalUns.yz:vNormalUns.xz):vNormalUns.xy;
	vTexCoord = vTexCoord.x < vTexCoord.y ? vTexCoord.yx : vTexCoord.xy;
    if (vTexCoord.x != 0.f)   // DavidJ -- when x & y are zero, can get a divide by zero here
        vTexCoord.y /= vTexCoord.x;
	// fit normal into the edge of the unit cube
	float3 result = inputNormal.xyz / maxNAbs;
	// look-up fitting length and scale the normal to get the best fit
    #if defined(_CS)
        const bool interpolateBetweenSamples = false;       // non interpolating version seems accurate enough
        float fFittingScale;
        if (interpolateBetweenSamples) {
            fFittingScale = ManualInterpolate(NormalsFittingTexture, vTexCoord);
        } else {
            uint2 dims; NormalsFittingTexture.GetDimensions(dims.x, dims.y);
            fFittingScale = NormalsFittingTexture[uint2(saturate(vTexCoord)*dims.xy + 0.5.xx)%dims].a;
            // fFittingScale = NormalsFittingTexture[uint2(vTexCoord*dims.xy)].a;
        }
    #else
	    float fFittingScale = NormalsFittingTexture.Sample(ClampingSampler, vTexCoord).a;
    #endif
	// scale the normal to get the best fit
	result.rgb *= fFittingScale;
	// wrap to [0;1] unsigned form
	if (!SignedNormalOutput)
        result.rgb = result.rgb * .5f + .5f;
	return result;
}

// From "A Survey of Efficient Representations for Independent Unit Vectors", Cigolle et. al
// Separate the square into 8 parts and use these to encode the faces of an octant in 3d space
float2 signNotZero(float2 v) { return float2((v.x >= 0.0) ? +1.0 : -1.0, (v.y >= 0.0) ? +1.0 : -1.0); }
float2 float32x3_to_oct(float3 v)
{
	// note; requires normalized input
	float2 p = v.xy * rcp(abs(v.x) + abs(v.y) + abs(v.z));
	return (v.z <= 0.0) ? ((1.0 - abs(p.yx)) * signNotZero(p)) : p;
}
float3 oct_to_float32x3(float2 e) 
{
	float3 v = float3(e.xy, 1.0 - abs(e.x) - abs(e.y));
	if (v.z < 0) v.xy = (1.0 - abs(v.yx)) * signNotZero(v.xy);
	return normalize(v);
}

// Normal compression modes
//   NORMAL_COMPRESSION_BEST_FIT_TABLE 
//          should be able to store and recover a normal at quite high precision, but
//          requires 3 components in the gbuffer and a lookup table texture
//          Writing the normal is a little expensive, but loading it is just a normalize
//  NORMAL_COMPRESSION_OCTANT
//          stores a normal in two components, and little lower precision
//          loading and storing are approximately the same cost and medium cost
#define NORMAL_COMPRESSION_BEST_FIT_TABLE 1
#define NORMAL_COMPRESSION_OCTANT 2
#if !NORMAL_COMPRESSION
    #define NORMAL_COMPRESSION NORMAL_COMPRESSION_OCTANT
#endif

float3 CompressGBufferNormal(float3 inputNormal)
{
    #if NORMAL_COMPRESSION == NORMAL_COMPRESSION_OCTANT
        // assuming that the input is pre-normalized here!
        return float3(float32x3_to_oct(inputNormal), 0);
    #else
    	return GBuffer_CalculateBestFitNormal(inputNormal);
    #endif
}

float3 DecompressGBufferNormal(float3 gBufferNormalSample)
{
    #if NORMAL_COMPRESSION == NORMAL_COMPRESSION_OCTANT
        return oct_to_float32x3(gBufferNormalSample.xy);
    #else
        float3 rangeAdj;
        if (!SignedNormalOutput) {
            rangeAdj = -1.0.xxx + 2.f * gBufferNormalSample;
        } else {
            rangeAdj = gBufferNormalSample;
        }

        float lengthSq = dot(rangeAdj, rangeAdj);

        // note -- we're getting issues with zero length normals in the gbuffer
        //          this can occur when the texture mapping bends around geometry
        //          in an extreme way.
        float mult;
        [flatten] if (lengthSq < 1e-4f) mult = 1.f;
        else mult = rsqrt(lengthSq);
        return rangeAdj * mult;
    #endif
}

#if !defined(GBUFFER_TYPE)
    #define GBUFFER_TYPE 1
#endif

#if GBUFFER_TYPE & 1
    #define HAS_PROPERTIES_BUFFER 1
#else
    #define HAS_PROPERTIES_BUFFER 0
#endif

#if GBUFFER_TYPE & 2
    #define NORMAL_BUFFER_8BIT 0
#else
    #define NORMAL_BUFFER_8BIT 1
#endif


struct GBufferEncoded
{
    float4 diffuseBuffer    : SV_Target0;
    float4 normalBuffer     : SV_Target1;
    #if HAS_PROPERTIES_BUFFER == 1
        float4 propertiesBuffer : SV_Target2;
    #endif
};

struct PerPixelMaterialParam
{
    float   roughness;
    float   specular;
    float   metal;
};

struct GBufferValues
{
        // Basic geometry parameters
    float3 diffuseAlbedo;      //  (expressed here in RGB format)
    float3 worldSpaceNormal;   //  (here, in full 3D format)

        // Per-pixel material parameters
    PerPixelMaterialParam material;

        // Special case parameters used by some shaders
    float blendingAlpha;
    float normalMapAccuracy;
    float cookedAmbientOcclusion;
    float cookedLightOcclusion; // this is precalculated occlusion of the dominant light source

    float3 transmission;
};

PerPixelMaterialParam PerPixelMaterialParam_Default()
{
    PerPixelMaterialParam result;
    result.roughness = 0.5f;
    result.specular = 0.05f;
    result.metal = 0.f;
    return result;
}

GBufferValues GBufferValues_Default()
{
    GBufferValues result;
    result.diffuseAlbedo     = 1.0.xxx;
    result.worldSpaceNormal  = 0.0.xxx;

    result.material = PerPixelMaterialParam_Default();

    result.blendingAlpha = 1.f;
    result.normalMapAccuracy = 1.f;
    result.cookedAmbientOcclusion = 1.f;
    result.cookedLightOcclusion = 1.f;

    result.transmission = 0.0.xxx;
    return result;
}


GBufferEncoded Encode(GBufferValues values)
{
        //
        //      Take the raw gbuffer input values and
        //      generate the encoded values
        //
    GBufferEncoded result;
    result.diffuseBuffer.rgb = values.diffuseAlbedo;
    #if NORMAL_BUFFER_8BIT == 1
        result.normalBuffer.xyz = CompressGBufferNormal(values.worldSpaceNormal.xyz);
    #else
        result.normalBuffer.xyz = values.worldSpaceNormal.xyz;
    #endif

    #if defined(DECAL_BLEND)
        const bool decalBlend = (DECAL_BLEND == 1);
    #else
        const bool decalBlend = false;
    #endif
    if (decalBlend) {
        result.diffuseBuffer.a = values.blendingAlpha;
        result.normalBuffer.a = values.blendingAlpha;
    } else {
            // todo -- "normal map accuracy" can be used to soften the roughness here...
        result.diffuseBuffer.a = values.material.specular;
        result.normalBuffer.a = values.material.roughness;
    }

    #if HAS_PROPERTIES_BUFFER == 1
        result.propertiesBuffer.r = values.material.metal;
        result.propertiesBuffer.g = values.cookedAmbientOcclusion;
        result.propertiesBuffer.b = values.cookedLightOcclusion;
        result.propertiesBuffer.a = values.blendingAlpha;
    #endif

    return result;
}

GBufferValues Decode(GBufferEncoded values)
{
    GBufferValues result = GBufferValues_Default();
	result.diffuseAlbedo = values.diffuseBuffer.rgb;
	result.worldSpaceNormal  = DecompressGBufferNormal(values.normalBuffer.xyz);

    result.material.roughness = values.normalBuffer.a;
    result.material.specular = values.diffuseBuffer.a;

	result.blendingAlpha = 1.f;
	result.normalMapAccuracy = 1.f;

	#if HAS_PROPERTIES_BUFFER==1
        result.material.metal = values.propertiesBuffer.r;
		result.cookedAmbientOcclusion = values.propertiesBuffer.g;
        result.cookedLightOcclusion = values.propertiesBuffer.b;
        result.blendingAlpha = values.propertiesBuffer.a;
	#endif
	return result;
}

GBufferValues GetSystemStruct_GBufferValues()
{
        //  called by the shader nodes editor to get the
        //  gbuffer values for preview
    return GBufferValues_Default();
}

struct DepthPlusEncoded
{
    #if DEPTH_PLUS_MOTION
        int2 motionBuffer       : SV_Target0;
        #if DEPTH_PLUS_NORMAL && DEPTH_PLUS_ROUGHNESS
            float4 normalBuffer     : SV_Target1;
        #elif DEPTH_PLUS_NORMAL
            float3 normalBuffer     : SV_Target1;
        #endif
    #endif
};

DepthPlusEncoded EncodeDepthPlus(GBufferValues values, int2 motion)
{
        //
        //      Take the raw gbuffer input values and
        //      generate the encoded values
        //
    DepthPlusEncoded result;
    #if DEPTH_PLUS_MOTION
        result.motionBuffer = motion;
    #endif
    #if DEPTH_PLUS_NORMAL
        result.normalBuffer.xyz = CompressGBufferNormal(values.worldSpaceNormal.xyz);
    #endif
    #if DEPTH_PLUS_ROUGHNESS
        result.normalBuffer.a = values.material.roughness;
    #endif
    return result;
}

#endif
