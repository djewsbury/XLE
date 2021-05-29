// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "ShadowGenGeometryConfiguration.hlsl"
#include "../../LightingEngine/ShadowProjection.hlsl"
#include "../../Framework/SystemUniforms.hlsl"
#include "../../Framework/VSOUT.hlsl"
#include "../../Framework/VSShadowOutput.hlsl"
#include "../../Math/ProjectionMath.hlsl"

	// support up to 6 output frustums (for cube map point light source projections)
[maxvertexcount(18)]
	void main(	triangle VSShadowOutput input[3],
				uint primitiveId : SV_PrimitiveID,
				inout TriangleStream<VSOUT> outputStream)
{
		// todo -- perhaps do backface removal here to
		//		quickly reject half of the triangles
		//		-- but we have to enable/disable it for double sided geometry
	//if (BackfaceSign(float4(input[0].position,1), float4(input[1].position,1), float4(input[2].position,1)) > 0)
	//	return;

	uint count = min(GetShadowSubProjectionCount(), VSOUT_HAS_SHADOW_PROJECTION_COUNT);
	uint mask = 0xf;
	uint frustumFlagAnd = input[0].shadowFrustumFlags & input[1].shadowFrustumFlags & input[2].shadowFrustumFlags;
	[unroll] for (uint c=0; c<count; ++c, mask <<= 4) {
		#if defined(FRUSTUM_FILTER)
			if ((FRUSTUM_FILTER & (1<<c)) == 0)
				continue;
		#endif

			//	do some basic culling
			//	-- left, right, top, bottom (but not front/back)
		if ((frustumFlagAnd & mask) == 0) {

			#if SHADOW_CASCADE_MODE==SHADOW_CASCADE_MODE_ARBITRARY || SHADOW_CASCADE_MODE==SHADOW_CASCADE_MODE_CUBEMAP
				// There may not actually be a meaningful advantage to storing the projected shadow position in the vertices
				// Recalculating it here might not be too bad; and if we store it in the vertices, it's just going to
				// make the vertices massive. It also seems to cause problems sometimes either in the HLSL compiler
				// or in the conversion from HLSL to GLSL. Even if we unroll the loop, the indexor 'c' doesn't seem to always
				// apply correctly
				float4 p0 = ShadowProjection_GetOutput(input[0].position.xyz, c);
				float4 p1 = ShadowProjection_GetOutput(input[1].position.xyz, c);
				float4 p2 = ShadowProjection_GetOutput(input[2].position.xyz, c);
			#elif SHADOW_CASCADE_MODE==SHADOW_CASCADE_MODE_ORTHOGONAL
				float4 p0 = float4(AdjustForOrthoCascade(input[0].position.xyz, c), 1.f);
				float4 p1 = float4(AdjustForOrthoCascade(input[1].position.xyz, c), 1.f);
				float4 p2 = float4(AdjustForOrthoCascade(input[2].position.xyz, c), 1.f);
				// When using orthogonal projection, we can flatten geometry that is closer
				// to the light than the view frustum onto the near plane
				// We don't care about the relative depth of geometry within that range, so
				// we can afford to just flatten it against a single plane. This allows us
				// to maximize the depth range available in the opposite direction
				//
				// However, be aware that we pay rasterization costs for geometry in this 
				// range (whereas, if we just clipped it out, we wouldn't). The geometry
				// can end up fairly large in the projection, which can add up. It's
				// particularly can issue if that geometry is alpha tested -- because
				// the pixel shader must do some texture lookups.
				//
				// So we should still be careful to cull geometry in that range and try
				// to only bring in the geometry that's truly needed 
				//
				// The other thing to be careful of is that this really only makes sense
				// for lights with infinite projections (ie, light from the sun). All geometry
				// is assumed to be in front of the light -- so if there's any geometry that's
				// actually behind the light, this won't actually give the correct result. 
				p0.z = max(0, p0.z);
				p1.z = max(0, p1.z);
				p2.z = max(0, p2.z);
			#else
				float4 p0 = 0.0.xxxx;
				float4 p1 = 0.0.xxxx;
				float4 p2 = 0.0.xxxx;
			#endif

				//
				//		Try to offset the final depth buffer
				//		value by half the depth buffer precision.
				//		This seems to get a better result... suggesting
				//		maybe a floor takes place? It's unclear. Actually,
				//		subtracting half a pixel seems to give the most
				//		balanced result. When there is no bias, this approximately
				//		half of pixels shadow themselves, and half don't.
				//		Still some investigation required to find out why that is...
				//
			// const float halfDepthBufferPrecision = .5f/65536.f;
			// p0.z -= halfDepthBufferPrecision * p0.w;
			// p1.z -= halfDepthBufferPrecision * p1.w;
			// p2.z -= halfDepthBufferPrecision * p2.w;

			VSOUT output;
			output.position = p0;
			#if VSOUT_HAS_TEXCOORD>=1
				output.texCoord = input[0].texCoord;
			#endif
			#if VSOUT_HAS_COLOR>=1
				output.color = input[0].color;
			#endif
			output.renderTargetIndex = c;
			#if (VSOUT_HAS_PRIMITIVE_ID==1)
				output.primitiveId = primitiveId;
			#endif
			outputStream.Append(output);

			output.position = p1;
			#if VSOUT_HAS_TEXCOORD>=1
				output.texCoord = input[1].texCoord;
			#endif
			#if VSOUT_HAS_COLOR>=1
				output.color = input[1].color;
			#endif
			output.renderTargetIndex = c;
			#if (VSOUT_HAS_PRIMITIVE_ID==1)
				output.primitiveId = primitiveId;
			#endif
			outputStream.Append(output);

			output.position = p2;
			#if VSOUT_HAS_TEXCOORD>=1
				output.texCoord = input[2].texCoord;
			#endif
			#if VSOUT_HAS_COLOR>=1
				output.color = input[2].color;
			#endif
			output.renderTargetIndex = c;
			#if (VSOUT_HAS_PRIMITIVE_ID==1)
				output.primitiveId = primitiveId;
			#endif
			outputStream.Append(output);
			outputStream.RestartStrip();
		}

			// if the triangle is entirely within this frustum, then skip other frustums
			// note -- maybe this condition doesn't help enough to cover it's cost?
		if (((input[0].shadowFrustumFlags | input[1].shadowFrustumFlags | input[2].shadowFrustumFlags) & mask) == 0)
			break;
	}

	#if (SHADOW_CASCADE_MODE==SHADOW_CASCADE_MODE_ORTHOGONAL) && (SHADOW_ENABLE_NEAR_CASCADE==1)
		// If we have the near shadows enabled, we must write out an additional shadow projection
		// into the final render target.
		// Note that the clip test here is going to be expensive! We ideally enable this only when
		// required.

		uint nearCascadeIndex = GetShadowSubProjectionCount();

		float4 p0 = float4(mul(OrthoNearCascade, float4(input[0].position.xyz, 1.f)), 1.f);
		float4 p1 = float4(mul(OrthoNearCascade, float4(input[1].position.xyz, 1.f)), 1.f);
		float4 p2 = float4(mul(OrthoNearCascade, float4(input[2].position.xyz, 1.f)), 1.f);

		if (TriInFrustum(p0, p1, p2)) {
			// const float halfDepthBufferPrecision = .5f/65536.f;
			// p0.z -= halfDepthBufferPrecision * p0.w;
			// p1.z -= halfDepthBufferPrecision * p1.w;
			// p2.z -= halfDepthBufferPrecision * p2.w;

			VSOUT output;
			output.position = p0;
			#if VSOUT_HAS_TEXCOORD>=1
				output.texCoord = input[0].texCoord;
			#endif
			#if VSOUT_HAS_COLOR>=1
				output.color = input[0].color;
			#endif
			output.renderTargetIndex = nearCascadeIndex;
			#if (VSOUT_HAS_PRIMITIVE_ID==1)
				output.primitiveId = primitiveId;
			#endif
			outputStream.Append(output);

			output.position = p1;
			#if VSOUT_HAS_TEXCOORD>=1
				output.texCoord = input[1].texCoord;
			#endif
			#if VSOUT_HAS_COLOR>=1
				output.color = input[1].color;
			#endif
			output.renderTargetIndex = nearCascadeIndex;
			#if (VSOUT_HAS_PRIMITIVE_ID==1)
				output.primitiveId = primitiveId;
			#endif
			outputStream.Append(output);

			output.position = p2;
			#if VSOUT_HAS_TEXCOORD>=1
				output.texCoord = input[2].texCoord;
			#endif
			#if VSOUT_HAS_COLOR>=1
				output.color = input[2].color;
			#endif
			output.renderTargetIndex = nearCascadeIndex;
			#if (VSOUT_HAS_PRIMITIVE_ID==1)
				output.primitiveId = primitiveId;
			#endif
			outputStream.Append(output);
			outputStream.RestartStrip();
		}

	#endif

}
