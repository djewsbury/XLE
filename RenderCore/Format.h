// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "Metal/Detail/DXGIFormatValues.h"         // maintain format number compatibility with DXGI whenever possible
#include "../Utility/StringUtils.h"

namespace Utility { namespace ImpliedTyping { class TypeDesc; } }

namespace RenderCore 
{
    enum class Format : int
    {
        Unknown = 0,

        #undef _EXP
        #define _EXP(X, Y, Z, U)    X##_##Y = DXGIVALUE_##X##_##Y,
            #include "Metal/Detail/DXGICompatibleFormats.h"
        #undef _EXP

        R24G8_TYPELESS = DXGIVALUE_R24G8_TYPELESS,
        D24_UNORM_S8_UINT = DXGIVALUE_D24_UNORM_S8_UINT,
        R24_UNORM_X8_TYPELESS = DXGIVALUE_R24_UNORM_X8_TYPELESS,
        X24_TYPELESS_G8_UINT = DXGIVALUE_X24_TYPELESS_G8_UINT,
        S8_UINT = 148,
        D32_SFLOAT_S8_UINT = 149,

        Matrix4x4 = 150,
        Matrix3x4 = 151,

		R10G10B10A10_SNORM = 152,		// (5 byte, 4 component 10 bit signed normalized value; often used for quaternions)
		R12G12B12A4_SNORM = 153,		// (5 byte, 3 component signed values with implied forth; another format for quaternions)

        Max
    };

    enum class FormatCompressionType
    {
        None, BlockCompression
    };

    enum class FormatComponents
    {
        Unknown,
        Alpha, 
        Luminance, LuminanceAlpha,
        RGB, RGBAlpha,
        RG, Depth, DepthStencil, Stencil, RGBE
    };

    enum class FormatComponentType
    {
        Typeless,
        Float, UInt, SInt,
        UNorm, SNorm, UNorm_SRGB,
        Exponential,
        UnsignedFloat16, SignedFloat16
    };

    auto        GetCompressionType(Format format) -> FormatCompressionType;
    auto        GetComponents(Format format) -> FormatComponents;
    auto        GetComponentType(Format format) -> FormatComponentType;
    unsigned    BitsPerPixel(Format format);
    unsigned    GetComponentPrecision(Format format);
    unsigned    GetComponentCount(FormatComponents components);

    struct CompressionParameters
    {
        unsigned _blockWidth, _blockHeight;
        unsigned _blockBytes;
        unsigned _decompressionPrecision;
    };
    CompressionParameters    GetCompressionParameters(Format format);

	Format FindFormat(
        FormatCompressionType compression, 
        FormatComponents components,
        FormatComponentType componentType,
        unsigned precision);

    Format		AsSRGBFormat(Format inputFormat);
    Format		AsLinearFormat(Format inputFormat);
    Format		AsTypelessFormat(Format inputFormat);
    bool        HasLinearAndSRGBFormats(Format inputFormat);

    Format      AsDepthStencilFormat(Format inputFormat);
    Format      AsDepthAspectSRVFormat(Format inputFormat);
    Format      AsStencilAspectSRVFormat(Format inputFormat);

    enum class ShaderNormalizationMode
    {
        Integer, Normalized, Float
    };

	Format AsFormat(
        const Utility::ImpliedTyping::TypeDesc& type,
        ShaderNormalizationMode norm = ShaderNormalizationMode::Integer);

	Utility::ImpliedTyping::TypeDesc AsImpliedType(Format fmt);

    const char* AsString(Format);
	std::optional<Format> AsFormat(StringSection<> name);
}

