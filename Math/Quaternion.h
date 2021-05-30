// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "XLEMath.h"

#if MATHLIBRARY_ACTIVE == MATHLIBRARY_CML
    #pragma warning(push)
    #pragma warning(disable:4512)       // assignment operator could not be generated
    #include <cml/quaternion.h>
    #pragma warning(pop)
#endif

namespace XLEMath
{
    #if MATHLIBRARY_ACTIVE == MATHLIBRARY_CML

        typedef cml::quaternion< float, cml::fixed<>, cml::scalar_first, cml::positive_cross >      Quaternion;

    #endif

    Quaternion SphericalInterpolate(const Quaternion& lhs, const Quaternion& rhs, float alpha);

    inline auto Magnitude(const Quaternion& q) -> decltype(cml::length(q))
        { return cml::length(q); }

    inline auto MagnitudeSquared(const Quaternion& q) -> decltype(cml::length_squared(q))
        { return cml::length_squared(q); }

    template<typename BasicType>
        inline bool Equivalent(
            cml::quaternion< BasicType, cml::fixed<>, cml::scalar_first, cml::positive_cross > lhs,
            cml::quaternion< BasicType, cml::fixed<>, cml::scalar_first, cml::positive_cross > rhs, BasicType tolerance)
        {
            return Equivalent(lhs[0], rhs[0], tolerance)
                && Equivalent(lhs[1], rhs[1], tolerance)
                && Equivalent(lhs[2], rhs[2], tolerance)
                && Equivalent(lhs[3], rhs[3], tolerance)
                ;
        }

            ////   I M P L E M E N T A T I O N S   /////

    inline Quaternion SphericalInterpolate(const Quaternion& lhs, const Quaternion& rhs, float alpha)
    {
		float tolerance = 1e-4f;

		Quaternion qr = rhs;
		float c = cml::dot(lhs,qr);
		if (c < 0.f) {
			qr = -qr;
			c = -c;
		}

		float omega = cml::acos_safe(c);
		float s = std::sin(omega);
		
		return (s < tolerance) ?
			cml::normalize((1.f - alpha) * lhs + alpha * qr) :
			(float(std::sin((1.f - alpha) * omega)) * lhs + float(std::sin(alpha * omega)) * qr) / s;
	}
}

