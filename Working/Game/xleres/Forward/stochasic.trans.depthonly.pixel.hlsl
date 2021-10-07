// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "../TechniqueLibrary/Framework/MainGeometry.hlsl"
#include "../Objects/IllumShader/PerPixel.h"
#include "../TechniqueLibrary/Framework/Surface.hlsl"
#include "Transparency/depthonlyutil.hlsl"

#if (STOCHASTIC_TRANS)

	void main(
		VSOUT geo, uint primitiveID : SV_PrimitiveID,
		out uint oCoverage : SV_Coverage
		#if (STOCHASTIC_TRANS_PRIMITIVEID==1)
			, out uint oPrimId : SV_Target0
			#if (STOCHASTIC_TRANS_OPACITY==1)
				, out float oOpacity : SV_Target1
			#endif
		#elif (STOCHASTIC_TRANS_OPACITY==1)
			, out float oOpacity : SV_Target0
		#endif

		)
	{
		float alpha = IllumShader_PerPixel(geo).blendingAlpha;
		oCoverage = StochasticTransMask(uint2(geo.position.xy), alpha, primitiveID);
		#if (STOCHASTIC_TRANS_PRIMITIVEID==1)
			oPrimId = primitiveID;
		#endif
		#if (STOCHASTIC_TRANS_OPACITY==1)
			oOpacity = alpha;
		#endif
	}

#else

	#if !(VSOUT_HAS_TEXCOORD && ((MAT_ALPHA_TEST==1)||(MAT_ALPHA_TEST_PREDEPTH==1))) && (VULKAN!=1)
		[earlydepthstencil]
	#endif
	void main(VSOUT geo)
	{
		DoAlphaTest(geo, GetAlphaThreshold());
	}

#endif
