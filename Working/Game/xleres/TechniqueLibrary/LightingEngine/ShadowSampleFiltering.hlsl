// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#if !defined(SAMPLE_FILTERING_H)
#define SAMPLE_FILTERING_H

#include "../Framework/Binding.hlsl"

SamplerComparisonState		ShadowSampler   BIND_SHARED_LIGHTING_S2;
SamplerState				ShadowDepthSampler    BIND_SHARED_LIGHTING_S3;

////////////////////////////////////////////////////////////////////////////
        //   A M D   s h a d o w   f i l t e r i n g   //
////////////////////////////////////////////////////////////////////////////

    //
    //  This is AMD's method of shadow filtering (from the D3D11 SDK)
    //  It has been mixed with a similar implementation from nvidia.
    //
    //  It uses a fixed size filter, with weights that change based
    //  on the blocker distance. It seems that the advantage of this over
    //  poisson disk methods is that poisson disk methods can end up
    //  sampling the same pixels multiple times, particularly for small
    //  filter sizes. This method will always sample a fixed box around
    //  the target point. This method might also have slightly smoother
    //  results -- because a poisson disk can introduce artefacts where
    //  the samples in the disk are more or less densely packed.
    //
    //  This method seems to generally sample more points. I wonder if the
    //  regular sampling pattern will improve performance much? Probably
    //  only if one method causes cache misses less frequently.
    //

#define AMD_FILTER_SIZE 11
#define FS              AMD_FILTER_SIZE
#define FS2             (AMD_FILTER_SIZE/2)
#define BMS             (FS + 2)

    //  Change to 1 to reuse the blocker search gather in the filter (for the nvidia method)
    //  To get this to work, we need to use the same fixed pattern for the blocker search
#define REUSE_GATHER 0

///////////////////////////////////////////////////////////////////////////////////////////

static const float C3[11][11] =
               {   { 1.0,1.0,1.0,1.0,1.0,1.0,1.0,1.0,1.0,1.0,1.0 },
                   { 1.0,1.0,1.0,1.0,1.0,1.0,1.0,1.0,1.0,1.0,1.0 },
                   { 1.0,1.0,1.0,1.0,1.0,1.0,1.0,1.0,1.0,1.0,1.0 },
                   { 1.0,1.0,1.0,1.0,1.0,1.0,1.0,1.0,1.0,1.0,1.0 },
                   { 1.0,1.0,1.0,1.0,1.0,1.0,1.0,1.0,1.0,1.0,1.0 },
                   { 1.0,1.0,1.0,1.0,1.0,1.0,1.0,1.0,1.0,1.0,1.0 },
                   { 1.0,1.0,1.0,1.0,1.0,1.0,1.0,1.0,1.0,1.0,1.0 },
                   { 1.0,1.0,1.0,1.0,1.0,1.0,1.0,1.0,1.0,1.0,1.0 },
                   { 1.0,1.0,1.0,1.0,1.0,1.0,1.0,1.0,1.0,1.0,1.0 },
                   { 1.0,1.0,1.0,1.0,1.0,1.0,1.0,1.0,1.0,1.0,1.0 },
                   { 1.0,1.0,1.0,1.0,1.0,1.0,1.0,1.0,1.0,1.0,1.0 }
                   };

static const float C2[11][11] =
               {   { 0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0 },
                   { 0.0,0.2,0.2,0.2,0.2,0.2,0.2,0.2,0.2,0.2,0.0 },
                   { 0.0,0.2,1.0,1.0,1.0,1.0,1.0,1.0,1.0,0.2,0.0 },
                   { 0.0,0.2,1.0,1.0,1.0,1.0,1.0,1.0,1.0,0.2,0.0 },
                   { 0.0,0.2,1.0,1.0,1.0,1.0,1.0,1.0,1.0,0.2,0.0 },
                   { 0.0,0.2,1.0,1.0,1.0,1.0,1.0,1.0,1.0,0.2,0.0 },
                   { 0.0,0.2,1.0,1.0,1.0,1.0,1.0,1.0,1.0,0.2,0.0 },
                   { 0.0,0.2,1.0,1.0,1.0,1.0,1.0,1.0,1.0,0.2,0.0 },
                   { 0.0,0.2,1.0,1.0,1.0,1.0,1.0,1.0,1.0,0.2,0.0 },
                   { 0.0,0.2,0.2,0.2,0.2,0.2,0.2,0.2,0.2,0.2,0.0 },
                   { 0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0 }
                   };

static const float C1[11][11] =
               {   { 0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0 },
                   { 0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0 },
                   { 0.0,0.0,0.2,0.2,0.2,0.2,0.2,0.2,0.2,0.0,0.0 },
                   { 0.0,0.0,0.2,1.0,1.0,1.0,1.0,1.0,0.2,0.0,0.0 },
                   { 0.0,0.0,0.2,1.0,1.0,1.0,1.0,1.0,0.2,0.0,0.0 },
                   { 0.0,0.0,0.2,1.0,1.0,1.0,1.0,1.0,0.2,0.0,0.0 },
                   { 0.0,0.0,0.2,1.0,1.0,1.0,1.0,1.0,0.2,0.0,0.0 },
                   { 0.0,0.0,0.2,1.0,1.0,1.0,1.0,1.0,0.2,0.0,0.0 },
                   { 0.0,0.0,0.2,0.2,0.2,0.2,0.2,0.2,0.2,0.0,0.0 },
                   { 0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0 },
                   { 0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0 }
                   };

static const float C0[11][11] =
               {   { 0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0 },
                   { 0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0 },
                   { 0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0 },
                   { 0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0 },
                   { 0.0,0.0,0.0,0.0,0.8,0.8,0.8,0.0,0.0,0.0,0.0 },
                   { 0.0,0.0,0.0,0.0,0.8,1.0,0.8,0.0,0.0,0.0,0.0 },
                   { 0.0,0.0,0.0,0.0,0.8,0.8,0.8,0.0,0.0,0.0,0.0 },
                   { 0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0 },
                   { 0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0 },
                   { 0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0 },
                   { 0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0 }
                   };

// Matrices of bezier control points used to dynamically generate a filter matrix.
// The generated matrix is sized FS x FS; the bezier control matrices are zero padded
// to simplify the dynamic filter loop.
static const float P0[BMS][BMS] =
{
	{ 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0 },
	{ 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0 },
	{ 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0 },
	{ 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0 },
	{ 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0 },
	{ 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.8f, 0.8f, 0.8f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0 },
	{ 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.8f, 1.0f, 0.8f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0 },
	{ 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.8f, 0.8f, 0.8f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0 },
	{ 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0 },
	{ 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0 },
	{ 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0 },
	{ 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0 },
	{ 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0 }
};

static const float P1[BMS][BMS] =
{
	{ 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0 },
	{ 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0 },
	{ 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0 },
	{ 0.0f, 0.0f, 0.0f, 0.2f, 0.2f, 0.2f, 0.2f, 0.2f, 0.2f, 0.2f, 0.0f, 0.0f, 0.0 },
	{ 0.0f, 0.0f, 0.0f, 0.2f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0.2f, 0.0f, 0.0f, 0.0 },
	{ 0.0f, 0.0f, 0.0f, 0.2f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0.2f, 0.0f, 0.0f, 0.0 },
	{ 0.0f, 0.0f, 0.0f, 0.2f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0.2f, 0.0f, 0.0f, 0.0 },
	{ 0.0f, 0.0f, 0.0f, 0.2f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0.2f, 0.0f, 0.0f, 0.0 },
	{ 0.0f, 0.0f, 0.0f, 0.2f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0.2f, 0.0f, 0.0f, 0.0 },
	{ 0.0f, 0.0f, 0.0f, 0.2f, 0.2f, 0.2f, 0.2f, 0.2f, 0.2f, 0.2f, 0.0f, 0.0f, 0.0 },
	{ 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0 },
	{ 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0 },
	{ 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0 }
};

static const float P2[BMS][BMS] =
{
	{ 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0 },
	{ 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0 },
	{ 0.0f, 0.0f, 0.2f, 0.2f, 0.2f, 0.2f, 0.2f, 0.2f, 0.2f, 0.2f, 0.2f, 0.0f, 0.0 },
	{ 0.0f, 0.0f, 0.2f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0.2f, 0.0f, 0.0 },
	{ 0.0f, 0.0f, 0.2f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0.2f, 0.0f, 0.0 },
	{ 0.0f, 0.0f, 0.2f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0.2f, 0.0f, 0.0 },
	{ 0.0f, 0.0f, 0.2f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0.2f, 0.0f, 0.0 },
	{ 0.0f, 0.0f, 0.2f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0.2f, 0.0f, 0.0 },
	{ 0.0f, 0.0f, 0.2f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0.2f, 0.0f, 0.0 },
	{ 0.0f, 0.0f, 0.2f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0.2f, 0.0f, 0.0 },
	{ 0.0f, 0.0f, 0.2f, 0.2f, 0.2f, 0.2f, 0.2f, 0.2f, 0.2f, 0.2f, 0.2f, 0.0f, 0.0 },
	{ 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0 },
	{ 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0 }
};

static const float P3[BMS][BMS] =
{
	{ 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0 },
	{ 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0.0 },
	{ 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0.0 },
	{ 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0.0 },
	{ 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0.0 },
	{ 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0.0 },
	{ 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0.0 },
	{ 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0.0 },
	{ 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0.0 },
	{ 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0.0 },
	{ 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0.0 },
	{ 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0.0 },
	{ 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0 }
};

///////////////////////////////////////////////////////////////////////////////////////////

float BezierFilter( int r, int c, float fL )
{
    return (1.0-fL)*(1.0-fL)*(1.0-fL) * C0[r][c] +
           fL*fL*fL * C3[r][c] +
           3.0f * (1.0-fL)*(1.0-fL)*fL * C1[r][c]+
           3.0f * fL*fL*(1.0-fL) * C2[r][c];
}

float BezierFilter_1( int row, int col, float t )
{
        //  the nVidia implementation of this is slightly different
        //  But it seems mostly because the nVidia method pads out the
        //  bezier matrices with zeroes on the edges. They say this is
        //  to simplify the loop code below
	int r = row + 1;
	int c = col + 1;
	return (
		(1.0f - t) * (1.0f - t) * (1.0f - t) * P0[r][c] +
		3.0f * (1.0 - t) * (1.0f - t) * t * P1[r][c] +
		3.0f * t * t * (1.0f - t) * P2[r][c] +
		t * t * t * P3[r][c]);
}

float AbsMax(float lhs, float rhs)
{
	if (abs(lhs) > abs(rhs)) { return lhs; }
	return rhs;
}

float2 CalculateFilterPlane_ScreenSpaceDerivatives(float comparisonDistance, float2 texCoords)
{
            //	Using the chain rule, we can find an approximation for the change in sample depth
			//	as test texture coordinates change. We can use to to bias the depth sample and avoid
			//	a lot of acne artefacts with large sampling filters.
			//	It really does help to reduce acne artefacts greatly. But it maybe adding some artefacts of
			//	it's own (particularly along cascade edges and in areas with large depth discontinuities)
			//	So there might be a trade-off
    float2 depthddTC = 0.0.xx;
    float2 depthDDXY = float2(ddx(comparisonDistance), ddy(comparisonDistance));
    float2 tcxDDXY = float2(ddx(texCoords.x), ddy(texCoords.x));
    float2 tcyDDXY = float2(ddx(texCoords.y), ddy(texCoords.y));

        //  On areas of large depth discrepancy, this method can create weird outlines
        //  We need to disable this on areas with large changes in depth.
    // float t = max(abs(depthDDXY.x), abs(depthDDXY.y));
    /*if (t < 0.00005f)*/ {

        int method = 2;
        if (method==0) {
                // we need to protect against d4ivide by zero
                // this constant makes a big difference. We can get some very wierd artefacts
                // when the texture coordinate derivatives are very small, but the larger the
                // clamping constant, the less improvement this method provides.
            const float zeroClampConstant = 1e-4f;
            if (abs(tcxDDXY.x) < zeroClampConstant) tcxDDXY.x = zeroClampConstant;
            if (abs(tcxDDXY.y) < zeroClampConstant) tcxDDXY.y = zeroClampConstant;
            if (abs(tcyDDXY.x) < zeroClampConstant) tcyDDXY.x = zeroClampConstant;
            if (abs(tcyDDXY.y) < zeroClampConstant) tcyDDXY.y = zeroClampConstant;
            float depthddTCX = AbsMax(depthDDXY.x / tcxDDXY.x, depthDDXY.y / tcxDDXY.y);
            float depthddTCY = AbsMax(depthDDXY.x / tcyDDXY.x, depthDDXY.y / tcyDDXY.y);
            depthddTC = float2(depthddTCX, depthddTCY);
        } else if (method == 1) {
                //	use the largest texture coordinate difference to try to get the most
                //	accurate answer. This seems to give a much more accurate result than
                //	the above in a wider range of cases.
            if (abs(tcxDDXY.x) > abs(tcxDDXY.y)){ depthddTC.x = depthDDXY.x / tcxDDXY.x; }
            else								{ depthddTC.x = depthDDXY.y / tcxDDXY.y; }

            if (abs(tcyDDXY.x) > abs(tcyDDXY.y)){ depthddTC.y = depthDDXY.x / tcyDDXY.x; }
            else								{ depthddTC.y = depthDDXY.y / tcyDDXY.y; }
        } else {
            depthddTC = 0.5f * float2(
                depthDDXY.x / tcxDDXY.x + depthDDXY.y / tcxDDXY.y, 
                depthDDXY.x / tcyDDXY.x + depthDDXY.y / tcyDDXY.y);
        }
    }

    // if (max(abs(ddx(arrayIndex)), abs(ddy(arrayIndex))) != 0) {
    // 	depthddTC = 0.0.xx;		// discontinuity when adjacent pixels are in a different shadow split
    // }

    return depthddTC;
}

float FixedSizeShadowFilter(Texture2DArray samplingTexture, float3 baseTC, float comparisonDepth, float fRatio, float2 filterPlane)
{
    float w = 0.f, s = 0.0f;
    int row, col;

    uint3 ShadowMapDims;
    samplingTexture.GetDimensions(ShadowMapDims.x, ShadowMapDims.y, ShadowMapDims.z);

        // (must be done before we change baseTC below)
    float2 largeFilterBias = filterPlane /= float2(ShadowMapDims.xy);    // we're using this with a texel offset, rather than a 0-1 texture coordinate offset

    float2 stc = (float2(ShadowMapDims.xy) * baseTC.xy) + float2(0.5f, 0.5f);
    float2 tcs = floor(stc);
    float2 fc = stc - tcs;
    baseTC.xy = baseTC.xy - (fc / float2(ShadowMapDims.xy));

#if defined(SHADOW_FILTER_AMD_IMPLEMENTATION)

        //  sum up weights of dynamic filter matrix
        //  Is there a better way to do this? We could quantize "filterRatio"
        //  and precompute all of the "w" values
    for( row = 0; row < FS; ++row ) {
       for( col = 0; col < FS; ++col ) {
          w += BezierFilter(row,col,fRatio);
       }
    }

        // filter shadow map samples using the dynamic weights
        //  (this is the original AMD implementation from the DirectX samples)
    float4 v1[ FS2 + 1 ];
    float2 v0[ FS2 + 1 ];
    [unroll(AMD_FILTER_SIZE)] for (row = -FS2; row <= FS2; row += 2) {
        for (col = -FS2; col <= FS2; col += 2) {
            v1[(col+FS2)/2] = samplingTexture.GatherCmpRed(
                ShadowSampler, baseTC.xyz, comparisonDepth, int2( col, row ) );

            if( col == -FS2 )
            {
                s += ( 1 - fc.y ) * ( v1[0].w * ( BezierFilter(row+FS2,0,fRatio) -
                                      BezierFilter(row+FS2,0,fRatio) * fc.x ) + v1[0].z *
                                    ( fc.x * ( BezierFilter(row+FS2,0,fRatio) -
                                      BezierFilter(row+FS2,1,fRatio) ) +
                                      BezierFilter(row+FS2,1,fRatio) ) );
                s += (     fc.y ) * ( v1[0].x * ( BezierFilter(row+FS2,0,fRatio) -
                                      BezierFilter(row+FS2,0,fRatio) * fc.x ) +
                                      v1[0].y * ( fc.x * ( BezierFilter(row+FS2,0,fRatio) -
                                      BezierFilter(row+FS2,1,fRatio) ) +
                                      BezierFilter(row+FS2,1,fRatio) ) );
                if( row > -FS2 )
                {
                    s += ( 1 - fc.y ) * ( v0[0].x * ( BezierFilter(row+FS2-1,0,fRatio) -
                                          BezierFilter(row+FS2-1,0,fRatio) * fc.x ) + v0[0].y *
                                        ( fc.x * ( BezierFilter(row+FS2-1,0,fRatio) -
                                          BezierFilter(row+FS2-1,1,fRatio) ) +
                                          BezierFilter(row+FS2-1,1,fRatio) ) );
                    s += (     fc.y ) * ( v1[0].w * ( BezierFilter(row+FS2-1,0,fRatio) -
                                          BezierFilter(row+FS2-1,0,fRatio) * fc.x ) + v1[0].z *
                                        ( fc.x * ( BezierFilter(row+FS2-1,0,fRatio) -
                                          BezierFilter(row+FS2-1,1,fRatio) ) +
                                          BezierFilter(row+FS2-1,1,fRatio) ) );
                }
            }
            else if( col == FS2 )
            {
                s += ( 1 - fc.y ) * ( v1[FS2].w * ( fc.x * ( BezierFilter(row+FS2,FS-2,fRatio) -
                                      BezierFilter(row+FS2,FS-1,fRatio) ) +
                                      BezierFilter(row+FS2,FS-1,fRatio) ) + v1[FS2].z * fc.x *
                                      BezierFilter(row+FS2,FS-1,fRatio) );
                s += (     fc.y ) * ( v1[FS2].x * ( fc.x * ( BezierFilter(row+FS2,FS-2,fRatio) -
                                      BezierFilter(row+FS2,FS-1,fRatio) ) +
                                      BezierFilter(row+FS2,FS-1,fRatio) ) + v1[FS2].y * fc.x *
                                      BezierFilter(row+FS2,FS-1,fRatio) );
                if( row > -FS2 )
                {
                    s += ( 1 - fc.y ) * ( v0[FS2].x * ( fc.x *
                                        ( BezierFilter(row+FS2-1,FS-2,fRatio) -
                                          BezierFilter(row+FS2-1,FS-1,fRatio) ) +
                                          BezierFilter(row+FS2-1,FS-1,fRatio) ) +
                                          v0[FS2].y * fc.x * BezierFilter(row+FS2-1,FS-1,fRatio) );
                    s += (     fc.y ) * ( v1[FS2].w * ( fc.x *
                                        ( BezierFilter(row+FS2-1,FS-2,fRatio) -
                                          BezierFilter(row+FS2-1,FS-1,fRatio) ) +
                                          BezierFilter(row+FS2-1,FS-1,fRatio) ) +
                                          v1[FS2].z * fc.x * BezierFilter(row+FS2-1,FS-1,fRatio) );
                }
            }
            else
            {
                s += ( 1 - fc.y ) * ( v1[(col+FS2)/2].w * ( fc.x *
                                    ( BezierFilter(row+FS2,col+FS2-1,fRatio) -
                                      BezierFilter(row+FS2,col+FS2+0,fRatio) ) +
                                      BezierFilter(row+FS2,col+FS2+0,fRatio) ) +
                                      v1[(col+FS2)/2].z * ( fc.x *
                                    ( BezierFilter(row+FS2,col+FS2-0,fRatio) -
                                      BezierFilter(row+FS2,col+FS2+1,fRatio) ) +
                                      BezierFilter(row+FS2,col+FS2+1,fRatio) ) );
                s += (     fc.y ) * ( v1[(col+FS2)/2].x * ( fc.x *
                                    ( BezierFilter(row+FS2,col+FS2-1,fRatio) -
                                      BezierFilter(row+FS2,col+FS2+0,fRatio) ) +
                                      BezierFilter(row+FS2,col+FS2+0,fRatio) ) +
                                      v1[(col+FS2)/2].y * ( fc.x *
                                    ( BezierFilter(row+FS2,col+FS2-0,fRatio) -
                                      BezierFilter(row+FS2,col+FS2+1,fRatio) ) +
                                      BezierFilter(row+FS2,col+FS2+1,fRatio) ) );
                if( row > -FS2 )
                {
                    s += ( 1 - fc.y ) * ( v0[(col+FS2)/2].x * ( fc.x *
                                        ( BezierFilter(row+FS2-1,col+FS2-1,fRatio) -
                                          BezierFilter(row+FS2-1,col+FS2+0,fRatio) ) +
                                          BezierFilter(row+FS2-1,col+FS2+0,fRatio) ) +
                                          v0[(col+FS2)/2].y * ( fc.x *
                                        ( BezierFilter(row+FS2-1,col+FS2-0,fRatio) -
                                          BezierFilter(row+FS2-1,col+FS2+1,fRatio) ) +
                                          BezierFilter(row+FS2-1,col+FS2+1,fRatio) ) );
                    s += (     fc.y ) * ( v1[(col+FS2)/2].w * ( fc.x *
                                        ( BezierFilter(row+FS2-1,col+FS2-1,fRatio) -
                                          BezierFilter(row+FS2-1,col+FS2+0,fRatio) ) +
                                          BezierFilter(row+FS2-1,col+FS2+0,fRatio) ) +
                                          v1[(col+FS2)/2].z * ( fc.x *
                                        ( BezierFilter(row+FS2-1,col+FS2-0,fRatio) -
                                          BezierFilter(row+FS2-1,col+FS2+1,fRatio) ) +
                                          BezierFilter(row+FS2-1,col+FS2+1,fRatio) ) );
                }
            }

            if( row != FS2 )
            {
                v0[(col+FS2)/2] = v1[(col+FS2)/2].xy;
            }
        }
    }
#else

        //  sum up weights of dynamic filter matrix
        //  Is there a better way to do this? We could quantize "filterRatio"
        //  and precompute all of the "w" values
    for( row = 0; row < FS; ++row ) {
       for( col = 0; col < FS; ++col ) {
          w += BezierFilter_1(row,col,fRatio);
       }
    }

        //  this is an improvement from a nvidia sample. It seems to be the same,
        //  just cleaned up for code clarity
	float2 prevRow[FS2 + 1];
	[unroll(FS)] for (row = -FS2; row <= FS2; row += 2) {
		[unroll(FS)] for (col = -FS2; col <= FS2; col += 2) {
            #if (REUSE_GATHER)
			    float4 d4 = samplingTexture.GatherRed(ShadowDepthSampler, baseTC.xyz, int2(col, row));
			    float4 sm4 = (comparisonDepth.xxxx <= d4) ? (1.0).xxxx : (0.0).xxxx;
            #else
                float biasedCompareDepth = comparisonDepth + dot(float2(col, row), largeFilterBias.xy);
			    float4 sm4 = samplingTexture.GatherCmpRed(ShadowSampler, baseTC.xyz, biasedCompareDepth, int2(col, row));
            #endif

            #define SM(_row, _col) ((_row) == 0 ? ((_col) == 0 ? sm4.w : sm4.z) \
                                                : ((_col) == 0 ? sm4.x : sm4.y))

			float f10 = BezierFilter_1(row + FS2, col + FS2 - 1, fRatio);
			float f11 = BezierFilter_1(row + FS2, col + FS2, fRatio);
			float f12 = BezierFilter_1(row + FS2, col + FS2 + 1, fRatio);

			s += (1.0f - fc.y) * (SM(0, 0) * (fc.x * (f10 - f11) + f11) + SM(0, 1) * (fc.x * (f11 - f12) + f12));
			s +=         fc.y  * (SM(1, 0) * (fc.x * (f10 - f11) + f11) + SM(1, 1) * (fc.x * (f11 - f12) + f12));

			if (row > -FS2) {
				float f00 = BezierFilter_1(row + FS2 - 1, col + FS2 - 1, fRatio);
				float f01 = BezierFilter_1(row + FS2 - 1, col + FS2, fRatio);
				float f02 = BezierFilter_1(row + FS2 - 1, col + FS2 + 1, fRatio);
				s += (1.0f - fc.y) * (prevRow[(col + FS2) / 2].x * (fc.x * (f00 - f01) + f01) + prevRow[(col + FS2) / 2].y * (fc.x * (f01 - f02) + f02));
				s +=         fc.y  * (SM(0, 0) * (fc.x * (f00 - f01) + f01) + SM(0, 1) * (fc.x * (f01 - f02) + f02));
			}

            #undef SM

			prevRow[(col + FS2) / 2] = sm4.xy;
		}
	}
#endif

    return s/w;
}

#undef FS
#undef FS2
#undef BMS

#endif
