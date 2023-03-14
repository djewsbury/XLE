// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../Assets/AssetsCore.h"
#include "../Math/Vector.h"

namespace RenderCore { class MiniInputElementDesc; }

namespace RenderOverlays
{
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

	class ColorB
    {
    public:
        uint8_t           a, r, g, b;

        ColorB() {}
        ColorB(uint8_t r_, uint8_t g_, uint8_t b_, uint8_t a_ = 0xff) : a(a_), r(r_), g(g_), b(b_) {}
        ColorB(uint32_t rawColor)    { a = rawColor >> 24; r = (rawColor >> 16) & 0xff; g = (rawColor >> 8) & 0xff; b = (rawColor >> 0) & 0xff; }
        
        unsigned        AsUInt32() const           { return (uint32_t(a) << 24) | (uint32_t(r) << 16) | (uint32_t(g) << 8) | uint32_t(b); }

        static ColorB   FromNormalized(float r_, float g_, float b_, float a_ = 1.f)
        {
            return ColorB(  uint8_t(Clamp(r_, 0.f, 1.f) * 255.f + 0.5f), uint8_t(Clamp(g_, 0.f, 1.f) * 255.f + 0.5f), 
                            uint8_t(Clamp(b_, 0.f, 1.f) * 255.f + 0.5f), uint8_t(Clamp(a_, 0.f, 1.f) * 255.f + 0.5f));
        }

        static ColorB   FromNormalized(const Float4& v)
        {
            return ColorB(  uint8_t(Clamp(v[0], 0.f, 1.f) * 255.f + 0.5f), uint8_t(Clamp(v[1], 0.f, 1.f) * 255.f + 0.5f), 
                            uint8_t(Clamp(v[2], 0.f, 1.f) * 255.f + 0.5f), uint8_t(Clamp(v[3], 0.f, 1.f) * 255.f + 0.5f));
        }

        Float4 AsNormalized() const
        {
            return { r/255.f, g/255.f, b/255.f, a/255.f };
        }

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
        Rect() {}

        Coord       Width() const     { return _bottomRight[0] - _topLeft[0]; }
        Coord       Height() const    { return _bottomRight[1] - _topLeft[1]; }

        static Rect Invalid() { return Rect { {std::numeric_limits<Coord>::max(), std::numeric_limits<Coord>::max()}, {std::numeric_limits<Coord>::min(), std::numeric_limits<Coord>::min()}}; }
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
