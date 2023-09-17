// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#if !defined(EQUATIONS_H)
#define EQUATIONS_H

/// <summary>Cheap & rough cosine approximation</summary>
/// This looks like the first few terms of a taylor series.
/// It's very cheap, but it may not be necessarily faster than
/// just using the intrinsic "cos" function.
float BlinnWyvillCosineApproximation(float x)
{
    float x2 =  x *  x;
    float x4 = x2 * x2;
    float x6 = x4 * x2;

    float fa =  4.f / 9.f;
    float fb = 17.f / 9.f;
    float fc = 22.f / 9.f;

    float y = fa*x6 - fb*x4 + fc*x2;
    return y;
}

/// <summary>Equation with "S" type shape</summary>
/// Sometimes it's useful to have a general equation with
/// with behaviour for debugging & testing. However, it's
/// a little expensive. It may be better to burn it into
/// a lookup table or consider a cheaper alternative.
float DoubleCubicSeatWithLinearBlend(float x, float a, float b)
{
    float epsilon = 0.00001f;
    float min_param_a = 0.f + epsilon;
    float max_param_a = 1.f - epsilon;
    float min_param_b = 0.f;
    float max_param_b = 1.f;
    a = min(max_param_a, max(min_param_a, a));
    b = min(max_param_b, max(min_param_b, b));
    b = 1.f - b; //reverse for intelligibility.

    float y = 0.f;
    if (x<=a) {
        y = b*x + (1-b)*a*(1-pow(1-x/a, 3.f));
    } else {
        y = b*x + (1-b)*(a + (1-a)*pow((x-a)/(1-a), 3.f));
    }
    return y;
}

#endif
