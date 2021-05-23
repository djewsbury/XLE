// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#if !defined(UTILITY_MISC_H)
#define UTILITY_MISC_H

#if !defined(LOOP_DIRECTIVE)
    #define LOOP_DIRECTIVE [loop]
#endif

#if !defined(BRANCH_DIRECTIVE)
    #define BRANCH_DIRECTIVE [branch]
#endif

float Sq(float x) { return x*x; }

uint DitherPatternInt(uint2 pixelCoords)
{
	uint ditherArray[16] =
	{
		4, 12,  0,  8,
		10,  2, 14,  6,
		15,  7, 11,  3,
		1,  9,  5, 13
	};
	uint2 t = pixelCoords.xy & 0x3;
	return ditherArray[t.x+t.y*4];
}

float DitherPatternValue(uint2 pixelCoords)
{
	return float(DitherPatternInt(pixelCoords)) / 16.f;
}

uint IntegerHash(uint seed)
{
		// From http://www.concentric.net/~Ttwang/tech/inthash.htm
		// This produces an output integer from an input integer,
		// where the bits of the output are generally unrelated to
		// the input, but there is a near 1:1 mapping from input to
		// output.
		// So if the input numbers have some ordering, this output
		// will be scrambled in the output.
		// In our case, the input seed numbers have a clear arrangement
		// in screen space XY. This could result in noticeable patterns.
		// When we scramble them through this hashing function, we should
		// remove patterns.
		//
		// We can find potentially cheaper implementations of the same
		// concept here:
		//		https://gist.github.com/badboy/6267743
		// Note that the constants below may not have a big effect on the
		// algorithm; but it's possible that there is some ideal set of
		// numbers that are least likely to produce patterns.
#if 1
	seed = (seed+0x7ed55d16u) + (seed<<12);
	seed = (seed^0xc761c23cu) ^ (seed>>19);
	seed = (seed+0x165667b1u) + (seed<<5);
	seed = (seed+0xd3a2646cu) ^ (seed<<9);
	seed = (seed+0xfd7046c5u) + (seed<<3);
	seed = (seed^0xb55a4f09u) ^ (seed>>16);
	return seed;
#else
	uint c2=0x27d4eb2d; // a prime or an odd constant
	seed = (seed ^ 61) ^ (seed >> 16);
	seed = seed + (seed << 3);
	seed = seed ^ (seed >> 4);
	seed = seed * c2;
	seed = seed ^ (seed >> 15);
	return seed;
#endif
}

int CountTrue(bool3 input)
{
	return dot(float(true).xxx, input);
}

#endif
