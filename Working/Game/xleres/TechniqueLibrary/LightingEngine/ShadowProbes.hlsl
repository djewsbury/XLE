// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#define SHADOW_PROBE_BIQUADRATIC 1

#if SHADOW_PROBE
	struct StaticShadowProbeDesc
	{
		MiniProjZW _miniProjZW;
	};

	#if defined(LIGHT_RESOLVE_SHADER)
		TextureCubeArray<float> StaticShadowProbeDatabase : register(t10, space1);
		StructuredBuffer<StaticShadowProbeDesc> StaticShadowProbeProperties : register(t11, space1);
	#else
		TextureCubeArray<float> StaticShadowProbeDatabase : register(t6, space3);
		StructuredBuffer<StaticShadowProbeDesc> StaticShadowProbeProperties : register(t7, space3);
	#endif

	uint MajorAxisIndex(float3 input)
	{
		if (abs(input.x) > abs(input.y) && abs(input.x) > abs(input.z)) return 0;
		else if (abs(input.y) > abs(input.z)) return 1;
		return 2;
	}

	uint MajorAxisDistance(float3 input)
	{
		if (abs(input.x) > abs(input.y) && abs(input.x) > abs(input.z)) return abs(input.x);
		else if (abs(input.y) > abs(input.z)) return abs(input.y);
		return abs(input.z);
	}

	#if SHADOW_PROBE_BIQUADRATIC

		float SampleStaticDatabase(uint databaseEntry, float3 offset, LightScreenDest screenDesc)
		{
			float distance;
			float2 texCoord;
			uint faceIdx = 0;
			uint majorAxisIndex = MajorAxisIndex(offset);

			if (majorAxisIndex == 0) {
				distance = (offset.x < 0) ? -offset.x : offset.x;
				texCoord.x = (offset.x < 0) ? offset.z : -offset.z;
				texCoord.y = -offset.y;
				faceIdx = (offset.x < 0) ? 1 : 0;
			} else if (majorAxisIndex == 1) {
				distance = (offset.y < 0) ? -offset.y : offset.y;
				texCoord.x = offset.x;
				texCoord.y = (offset.y < 0) ? -offset.z : offset.z;
				faceIdx = (offset.y < 0) ? 3 : 2;
			} else {
				distance = (offset.z < 0) ? -offset.z : offset.z;
				texCoord.x = (offset.z < 0) ? -offset.x : offset.x;
				texCoord.y = -offset.y;
				faceIdx = (offset.z < 0) ? 5 : 4;
			}

			// See https://www.shadertoy.com/view/wtXXDl for "biquadratic" texture sampling hack
			// It's not quiet perfect, but does help reduce the impact of tell-tale bilinear problems,
			// and is sort of neat mathematically.
			//
			// This method tries to estimate cubic interpolation by placing extra bilinear samples
			// in such a way that the combination of them is a quadratic.
			// 
			// It's simple for flat textures, but we have a few different ways to extend this to
			// cubemaps. Even bilinear cubemap interpolation is done in a non-angular way, so we'll
			// do the same here (unless we we where using equiangular cubemaps, this would be a hassle)
			// anyway. So following the existing spirit of approximations, we'll just shift the sampling
			// vector such that we get the same results in the middle of cubemap faces, and hope that it
			// looks fine for the cubemaps edges.
			//
			// We can actually pretty easily figure out when all of our samples are going to be on the same
			// face -- and in theory, if we had a customized cubemap sample method, we could use this 
			// to optimize the lookup. But that would require writing the full bilinear cubemap sample including
			// all of the edge logic... So instead let's just do it the simple way...

			#if 1

				float twiceDistance = 2.0 * distance;
				texCoord = texCoord / twiceDistance;		// (simplified)
				float3 probeDatabaseDims;
				StaticShadowProbeDatabase.GetDimensions(probeDatabaseDims.x, probeDatabaseDims.y, probeDatabaseDims.z);
				float2 q = frac(texCoord * probeDatabaseDims.xy);
				float2 c = (q*(q - 1.0) + 0.5);		// biquadratic trick
				float extraBias = (abs(c.x)+abs(c.y)) * 32 / 65535.f;		// since we're expanding the sampling area, we will need some extra bias
				c /= probeDatabaseDims.xy;

				float3 A, B, C, D;
				if (majorAxisIndex == 0) {
					A = float3(offset.x, offset.y-c.y*twiceDistance, offset.z-c.x*twiceDistance);
					B = float3(offset.x, offset.y+c.y*twiceDistance, offset.z-c.x*twiceDistance);
					C = float3(offset.x, offset.y+c.y*twiceDistance, offset.z+c.x*twiceDistance);
					D = float3(offset.x, offset.y-c.y*twiceDistance, offset.z+c.x*twiceDistance);
				} else if (majorAxisIndex == 1) {
					A = float3(offset.x-c.x*twiceDistance, offset.y, offset.z-c.y*twiceDistance);
					B = float3(offset.x+c.x*twiceDistance, offset.y, offset.z-c.y*twiceDistance);
					C = float3(offset.x+c.x*twiceDistance, offset.y, offset.z+c.y*twiceDistance);
					D = float3(offset.x-c.x*twiceDistance, offset.y, offset.z+c.y*twiceDistance);
				} else {
					A = float3(offset.x-c.x*twiceDistance, offset.y-c.y*twiceDistance, offset.z);
					B = float3(offset.x+c.x*twiceDistance, offset.y-c.y*twiceDistance, offset.z);
					C = float3(offset.x+c.x*twiceDistance, offset.y+c.y*twiceDistance, offset.z);
					D = float3(offset.x-c.x*twiceDistance, offset.y+c.y*twiceDistance, offset.z);
				}

				distance = extraBias + WorldSpaceDepthToNDC_Perspective(distance, StaticShadowProbeProperties[databaseEntry]._miniProjZW);
				float result 
					= StaticShadowProbeDatabase.SampleCmpLevelZero(ShadowSampler, float4(A, float(databaseEntry)), distance)
					+ StaticShadowProbeDatabase.SampleCmpLevelZero(ShadowSampler, float4(B, float(databaseEntry)), distance)
					+ StaticShadowProbeDatabase.SampleCmpLevelZero(ShadowSampler, float4(C, float(databaseEntry)), distance)
					+ StaticShadowProbeDatabase.SampleCmpLevelZero(ShadowSampler, float4(D, float(databaseEntry)), distance);
				return result * 0.25;

			#else

				// This has an more optimized path when all of the clustered sample points are on the same face
				// -- but it requires a full cubemap bilinear sample to work
				texCoord = 0.5 + 0.5f * texCoord / distance;
				float3 probeDatabaseDims;
				StaticShadowProbeDatabase.GetDimensions(probeDatabaseDims.x, probeDatabaseDims.y, probeDatabaseDims.z);
				float2 q = frac(texCoord * probeDatabaseDims.xy);
				float2 c = (q*(q - 1.0) + 0.5) / probeDatabaseDims.xy;		// biquadratic trick
				float2 w0 = texCoord - c;
				float2 w1 = texCoord + c;
				distance = WorldSpaceDepthToNDC_Perspective(distance, StaticShadowProbeProperties[databaseEntry]._miniProjZW);
				[branch] if (all(w0 == saturate(w0) && w1 == saturate(w1))) {
					// cheap -- all samples in the one face
					float result 
						= StaticShadowProbeDatabase.SampleCmpLevelZero(ShadowSampler, float3(w0.x, w0.y, float(databaseEntry*6+faceIdx)), distance)
						+ StaticShadowProbeDatabase.SampleCmpLevelZero(ShadowSampler, float3(w0.x, w1.y, float(databaseEntry*6+faceIdx)), distance)
						+ StaticShadowProbeDatabase.SampleCmpLevelZero(ShadowSampler, float3(w1.x, w1.y, float(databaseEntry*6+faceIdx)), distance)
						+ StaticShadowProbeDatabase.SampleCmpLevelZero(ShadowSampler, float3(w1.x, w0.y, float(databaseEntry*6+faceIdx)), distance);
					return result * 0.25;
				} else {
					// expensive -- samples cross face boundaries
					return 1;
				}
			#endif

		}

	#else

		float SampleStaticDatabase(uint databaseEntry, float3 offset)
		{
			float distance = WorldSpaceDepthToNDC_Perspective(MajorAxisDistance(offset), StaticShadowProbeProperties[databaseEntry]._miniProjZW);
			// distance += 0.5f / 65535.f;     // bias half precision
			return StaticShadowProbeDatabase.SampleCmpLevelZero(ShadowSampler, float4(offset, float(databaseEntry)), distance);
		}
	#endif

#endif

