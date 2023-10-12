// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../Math/Vector.h"
#include "../Utility/IteratorUtils.h"

namespace RenderCore { class MiniInputElementDesc; }

namespace RenderOverlays
{
    inline uint8_t ClampToUInt8(int32_t v) {  v = std::max(v, 0); v = std::min(v, 255); return uint8_t(v); }

	struct ColorB
    {
        uint8_t b, g, r, a;

        ColorB();
        ColorB(uint8_t r_, uint8_t g_, uint8_t b_, uint8_t a_ = 0xff);
        ColorB(uint32_t rawColor);
        unsigned AsUInt32() const;
        static ColorB FromNormalized(float r_, float g_, float b_, float a_ = 1.f);
        static ColorB FromNormalized(const Float4& v);
        Float4 AsNormalized() const;

        static const ColorB White;
        static const ColorB Black;
        static const ColorB Red;
        static const ColorB Green;
        static const ColorB Blue;
        static const ColorB Zero;
    };

    using Coord = int;
    using Coord2 = Int2;

    inline Coord2 AsCoord2(const Float2& input) { return Coord2(Coord(input[0]), Coord(input[1])); }
    inline Float2 AsFloat2(const Coord2& input) { return Float2(float(input[0]), float(input[1])); }

    struct Rect ///////////////////////////////////////////////////////////////////////
    {
        Coord2      _topLeft, _bottomRight;
        Rect(const Coord2& topLeft, const Coord2& bottomRight) : _topLeft(topLeft), _bottomRight(bottomRight) {}
        Rect(Coord left, Coord top, Coord right, Coord bottom) : _topLeft(left, top), _bottomRight(right, bottom) {}
        Rect() {}

        Coord       Width() const     { return _bottomRight[0] - _topLeft[0]; }
        Coord       Height() const    { return _bottomRight[1] - _topLeft[1]; }

        static Rect Invalid() { return Rect { {std::numeric_limits<Coord>::max(), std::numeric_limits<Coord>::max()}, {std::numeric_limits<Coord>::min(), std::numeric_limits<Coord>::min()}}; }

        inline Rect& operator-=(const Coord2& rhs) { _topLeft -= rhs; _bottomRight -= rhs; return *this; }
        inline Rect& operator+=(const Coord2& rhs) { _topLeft += rhs; _bottomRight += rhs; return *this; }
    };

    inline bool Intersects(const Rect& lhs, const Rect& rhs)
    {
        return 
            !(  lhs._bottomRight[0] <= rhs._topLeft[0]
            ||  lhs._bottomRight[1] <= rhs._topLeft[1]
            ||  lhs._topLeft[0] >= rhs._bottomRight[0]
            ||  lhs._topLeft[1] >= rhs._bottomRight[1]);
    }

    inline bool Contains(
        const Rect& bigger, 
        const Rect& smaller)
    {
        return
            (   smaller._topLeft[0]  >= bigger._topLeft[0]
            &&  smaller._topLeft[1]  >= bigger._topLeft[1]
            &&  smaller._bottomRight[0] <= bigger._bottomRight[0]
            &&  smaller._bottomRight[1] <= bigger._bottomRight[1]);
    }

    inline bool Contains(
        const Rect& rect,
        const Coord2& pt)
    {
        return rect._topLeft[0] <= pt[0] && rect._topLeft[1] <= pt[1]
            && rect._bottomRight[0] >= pt[0] && rect._bottomRight[1] >= pt[1];
    }

    inline bool IsGood(const Rect& rect)
    {
        return  rect._topLeft[0] < rect._bottomRight[0]
            &&  rect._topLeft[1] < rect._bottomRight[1];
    }

    inline Rect operator-(const Rect& lhs, const Coord2& rhs) { return { lhs._topLeft - rhs, lhs._bottomRight - rhs }; }
    inline Rect operator+(const Rect& lhs, const Coord2& rhs) { return { lhs._topLeft + rhs, lhs._bottomRight + rhs }; }
    inline bool operator==(const Rect& lhs, const Rect& rhs) { return lhs._topLeft == rhs._topLeft && lhs._bottomRight == rhs._bottomRight; }
    inline bool operator!=(const Rect& lhs, const Rect& rhs) { return lhs._topLeft != rhs._topLeft || lhs._bottomRight != rhs._bottomRight; }

    ///////////////////////////////////////////////////////////////////////////////////

    enum class TextAlignment 
	{
        TopLeft, Top, TopRight,
        Left, Center, Right,
        BottomLeft, Bottom, BottomRight
    };

    namespace DrawTextFlags
    {
        enum Flags { Shadow = 1u<<0u, Outline = 1u<<1u, Snap = 1u<<2u, Clip = 1u<<3u };
        using BitField = unsigned;
    };

    ///////////////////////////////////////////////////////////////////////////////////
    //          V E R T E X   T Y P E S

	struct Vertex_PCT
    {
        Float3 _position;
        unsigned _colour;
        Float2 _texCoord;
        static IteratorRange<const RenderCore::MiniInputElementDesc*> s_inputElements2D;
        static IteratorRange<const RenderCore::MiniInputElementDesc*> s_inputElements3D;
    };

    struct Vertex_PC
    {
        Float3 _position;
        unsigned _colour;
        static IteratorRange<const RenderCore::MiniInputElementDesc*> s_inputElements2D;
        static IteratorRange<const RenderCore::MiniInputElementDesc*> s_inputElements3D;
    };

	///////////////////////////////////////////////////////////////////////////////////
    //          M I S C

    Float3      AsPixelCoords(Coord2 input);
    Float3      AsPixelCoords(Coord2 input, float depth);
    Float3      AsPixelCoords(Float2 input);
    Float3      AsPixelCoords(Float3 input);
    std::tuple<Float3, Float3> AsPixelCoords(const Rect& rect);
    unsigned    HardwareColor(ColorB input);

    inline ColorB::ColorB() {}
    inline ColorB::ColorB(uint8_t r_, uint8_t g_, uint8_t b_, uint8_t a_) : a(a_), r(r_), g(g_), b(b_) {}
    inline ColorB::ColorB(uint32_t rawColor) { a = rawColor >> 24u; r = (rawColor >> 16u) & 0xffu; g = (rawColor >> 8u) & 0xffu; b = rawColor & 0xffu; }
    inline unsigned ColorB::AsUInt32() const { return (uint32_t(a) << 24u) | (uint32_t(r) << 16u) | (uint32_t(g) << 8u) | uint32_t(b); }
    inline ColorB ColorB::FromNormalized(float r_, float g_, float b_, float a_)
    {
        return ColorB(  ClampToUInt8(int32_t(r_ * 255.f + 0.5f)), ClampToUInt8(int32_t(g_ * 255.f + 0.5f)), 
                        ClampToUInt8(int32_t(b_ * 255.f + 0.5f)), ClampToUInt8(int32_t(a_ * 255.f + 0.5f)));
    }
    inline ColorB ColorB::FromNormalized(const Float4& v) { return FromNormalized(v[0], v[1], v[2], v[3]); }
    inline Float4 ColorB::AsNormalized() const { return { r/255.f, g/255.f, b/255.f, a/255.f }; }

    inline float LinearToSRGB_Formal(float input)
    {
        return std::max(std::min(input*12.92f, 0.0031308f),1.055f*std::pow(input,0.41666f)-0.055f);
    }

    inline float SRGBToLinear_Formal(float input)
    {
        if (input <= 0.04045f) {
            return input / 12.92f;
        } else
            return std::pow((input+0.055f)/1.055f, 2.4f);
    }
}
