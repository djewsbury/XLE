// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../Math/Vector.h"

namespace RenderOverlays
{
    enum class TextAlignment 
	{
        TopLeft, Top, TopRight,
        Left, Center, Right,
        BottomLeft, Bottom, BottomRight
    };

	class ColorB
    {
    public:
        uint8_t           a, r, g, b;

        ColorB() {}
        ColorB(uint8_t r_, uint8_t g_, uint8_t b_, uint8_t a_ = 0xff) : r(r_), g(g_), b(b_), a(a_) {}
        ColorB(uint32_t rawColor)    { a = rawColor >> 24; r = (rawColor >> 16) & 0xff; g = (rawColor >> 8) & 0xff; b = (rawColor >> 0) & 0xff; }
        
        unsigned        AsUInt32() const           { return (uint32_t(a) << 24) | (uint32_t(r) << 16) | (uint32_t(g) << 8) | uint32_t(b); }

        static ColorB   FromNormalized(float r_, float g_, float b_, float a_ = 1.f)
        {
            return ColorB(  uint8_t(Clamp(r_, 0.f, 1.f) * 255.f + 0.5f), uint8_t(Clamp(g_, 0.f, 1.f) * 255.f + 0.5f), 
                            uint8_t(Clamp(b_, 0.f, 1.f) * 255.f + 0.5f), uint8_t(Clamp(a_, 0.f, 1.f) * 255.f + 0.5f));
        }

        static const ColorB White;
        static const ColorB Black;
        static const ColorB Red;
        static const ColorB Green;
        static const ColorB Blue;
        static const ColorB Zero;
    };

	class Quad 
	{
	public:
		Float2 min, max;

		Quad() : min(0.f, 0.f), max(0.f, 0.f) {}
		static Quad Empty();
		static Quad MinMax(float minX, float minY, float maxX, float maxY);
		static Quad MinMax(const Float2& min, const Float2& max);
		static Quad CenterExtent(const Float2& center, const Float2& extent);

		bool operator == (const Quad& v) const;
		bool operator != (const Quad& v) const;

		Float2 Center() const;
		Float2 Extent() const;
		float Length() const { return max[0] - min[0]; }
		float Height() const { return max[1] - min[1]; }
	};

}
