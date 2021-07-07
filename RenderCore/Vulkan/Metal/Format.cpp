// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "Format.h"
#include "IncludeVulkan.h"
#include "../../Format.h"
#include "../../../Core/Prefix.h"
#include <assert.h>

namespace RenderCore { namespace Metal_Vulkan
{
    static Format s_vkToGenericTable[256];
    static VkFormat_ s_genericToVkTable[(int)Format::Max];
    static bool s_lookupTablesInitialized = false;

    static void BindFormat2(Format lhs, VkFormat_ rhs)
    {
        assert(unsigned(lhs) < dimof(s_genericToVkTable));
        assert(unsigned(rhs) < dimof(s_vkToGenericTable));
        s_genericToVkTable[unsigned(lhs)] = rhs;
        s_vkToGenericTable[unsigned(rhs)] = lhs;
    }

    static void BindFormat1(Format lhs, VkFormat_ rhs)
    {
        assert(unsigned(rhs) < dimof(s_vkToGenericTable));
        s_vkToGenericTable[unsigned(rhs)] = lhs;
    }

    static void BindFormatTypeless(Format lhs, VkFormat_ rhs)
    {
        assert(unsigned(lhs) < dimof(s_genericToVkTable));
        s_genericToVkTable[unsigned(lhs)] = rhs;
    }

    void InitFormatConversionTables()
    {
        if (s_lookupTablesInitialized) return;

        BindFormat2(Format::Unknown, VK_FORMAT_UNDEFINED);
        // BindFormat2(Format::R4G4_UNORM_PACK8, VK_FORMAT_R4G4_UNORM_PACK8);
        // BindFormat2(Format::R4G4B4A4_UNORM_PACK16, VK_FORMAT_R4G4B4A4_UNORM_PACK16);
        // BindFormat2(Format::B4G4R4A4_UNORM_PACK16, VK_FORMAT_B4G4R4A4_UNORM_PACK16);
        // BindFormat2(Format::R5G6B5_UNORM_PACK16, VK_FORMAT_R5G6B5_UNORM_PACK16);
        // BindFormat2(Format::B5G6R5_UNORM_PACK16, VK_FORMAT_B5G6R5_UNORM_PACK16);
        // BindFormat2(Format::R5G5B5A1_UNORM_PACK16, VK_FORMAT_R5G5B5A1_UNORM_PACK16);
        // BindFormat2(Format::B5G5R5A1_UNORM_PACK16, VK_FORMAT_B5G5R5A1_UNORM_PACK16);
        // BindFormat2(Format::A1R5G5B5_UNORM_PACK16, VK_FORMAT_A1R5G5B5_UNORM_PACK16);
        BindFormat2(Format::R8_UNORM, VK_FORMAT_R8_UNORM);
        BindFormat2(Format::R8_SNORM, VK_FORMAT_R8_SNORM);
        // BindFormat2(Format::R8_USCALED, VK_FORMAT_R8_USCALED);
        // BindFormat2(Format::R8_SSCALED, VK_FORMAT_R8_SSCALED);
        BindFormat2(Format::R8_UINT, VK_FORMAT_R8_UINT);
        BindFormat2(Format::R8_SINT, VK_FORMAT_R8_SINT);
        // BindFormat2(Format::R8_UNORM_SRGB, VK_FORMAT_R8_SRGB);
        BindFormat2(Format::R8G8_UNORM, VK_FORMAT_R8G8_UNORM);
        BindFormat2(Format::R8G8_SNORM, VK_FORMAT_R8G8_SNORM);
        // BindFormat2(Format::R8G8_USCALED, VK_FORMAT_R8G8_USCALED);
        // BindFormat2(Format::R8G8_SSCALED, VK_FORMAT_R8G8_SSCALED);
        BindFormat2(Format::R8G8_UINT, VK_FORMAT_R8G8_UINT);
        BindFormat2(Format::R8G8_SINT, VK_FORMAT_R8G8_SINT);
        // BindFormat2(Format::R8G8_UNORM_SRGB, VK_FORMAT_R8G8_SRGB);
        // BindFormat2(Format::R8G8B8_UNORM, VK_FORMAT_R8G8B8_UNORM);
        // BindFormat2(Format::R8G8B8_SNORM, VK_FORMAT_R8G8B8_SNORM);
        // BindFormat2(Format::R8G8B8_USCALED, VK_FORMAT_R8G8B8_USCALED);
        // BindFormat2(Format::R8G8B8_SSCALED, VK_FORMAT_R8G8B8_SSCALED);
        // BindFormat2(Format::R8G8B8_UINT, VK_FORMAT_R8G8B8_UINT);
        // BindFormat2(Format::R8G8B8_SINT, VK_FORMAT_R8G8B8_SINT);
        // BindFormat2(Format::R8G8B8_UNORM_SRGB, VK_FORMAT_R8G8B8_SRGB);
        // BindFormat2(Format::B8G8R8_UNORM, VK_FORMAT_B8G8R8_UNORM);
        // BindFormat2(Format::B8G8R8_SNORM, VK_FORMAT_B8G8R8_SNORM);
        // BindFormat2(Format::B8G8R8_USCALED, VK_FORMAT_B8G8R8_USCALED);
        // BindFormat2(Format::B8G8R8_SSCALED, VK_FORMAT_B8G8R8_SSCALED);
        // BindFormat2(Format::B8G8R8_UINT, VK_FORMAT_B8G8R8_UINT);
        // BindFormat2(Format::B8G8R8_SINT, VK_FORMAT_B8G8R8_SINT);
        // BindFormat2(Format::B8G8R8_UNORM_SRGB, VK_FORMAT_B8G8R8_SRGB);
        BindFormat2(Format::R8G8B8A8_UNORM, VK_FORMAT_R8G8B8A8_UNORM);
        BindFormat2(Format::R8G8B8A8_SNORM, VK_FORMAT_R8G8B8A8_SNORM);
        // BindFormat2(Format::R8G8B8A8_USCALED, VK_FORMAT_R8G8B8A8_USCALED);
        // BindFormat2(Format::R8G8B8A8_SSCALED, VK_FORMAT_R8G8B8A8_SSCALED);
        BindFormat2(Format::R8G8B8A8_UINT, VK_FORMAT_R8G8B8A8_UINT);
        BindFormat2(Format::R8G8B8A8_SINT, VK_FORMAT_R8G8B8A8_SINT);
        BindFormat2(Format::R8G8B8A8_UNORM_SRGB, VK_FORMAT_R8G8B8A8_SRGB);
        BindFormat2(Format::B8G8R8A8_UNORM, VK_FORMAT_B8G8R8A8_UNORM);
        // BindFormat2(Format::B8G8R8A8_SNORM, VK_FORMAT_B8G8R8A8_SNORM);
        // BindFormat2(Format::B8G8R8A8_USCALED, VK_FORMAT_B8G8R8A8_USCALED);
        // BindFormat2(Format::B8G8R8A8_SSCALED, VK_FORMAT_B8G8R8A8_SSCALED);
        // BindFormat2(Format::B8G8R8A8_UINT, VK_FORMAT_B8G8R8A8_UINT);
        // BindFormat2(Format::B8G8R8A8_SINT, VK_FORMAT_B8G8R8A8_SINT);
        BindFormat2(Format::B8G8R8A8_UNORM_SRGB, VK_FORMAT_B8G8R8A8_SRGB);
        // BindFormat2(Format::A8B8G8R8_UNORM_PACK32, VK_FORMAT_A8B8G8R8_UNORM_PACK32);
        // BindFormat2(Format::A8B8G8R8_SNORM_PACK32, VK_FORMAT_A8B8G8R8_SNORM_PACK32);
        // BindFormat2(Format::A8B8G8R8_USCALED_PACK32, VK_FORMAT_A8B8G8R8_USCALED_PACK32);
        // BindFormat2(Format::A8B8G8R8_SSCALED_PACK32, VK_FORMAT_A8B8G8R8_SSCALED_PACK32);
        // BindFormat2(Format::A8B8G8R8_UINT_PACK32, VK_FORMAT_A8B8G8R8_UINT_PACK32);
        // BindFormat2(Format::A8B8G8R8_SINT_PACK32, VK_FORMAT_A8B8G8R8_SINT_PACK32);
        // BindFormat2(Format::A8B8G8R8_SRGB_PACK32, VK_FORMAT_A8B8G8R8_SRGB_PACK32);
        // BindFormat2(Format::A2R10G10B10_UNORM_PACK32, VK_FORMAT_A2R10G10B10_UNORM_PACK32);
        // BindFormat2(Format::A2R10G10B10_SNORM_PACK32, VK_FORMAT_A2R10G10B10_SNORM_PACK32);
        // BindFormat2(Format::A2R10G10B10_USCALED_PACK32, VK_FORMAT_A2R10G10B10_USCALED_PACK32);
        // BindFormat2(Format::A2R10G10B10_SSCALED_PACK32, VK_FORMAT_A2R10G10B10_SSCALED_PACK32);
        // BindFormat2(Format::A2R10G10B10_UINT_PACK32, VK_FORMAT_A2R10G10B10_UINT_PACK32);
        // BindFormat2(Format::A2R10G10B10_SINT_PACK32, VK_FORMAT_A2R10G10B10_SINT_PACK32);
        // BindFormat2(Format::A2B10G10R10_UNORM_PACK32, VK_FORMAT_A2B10G10R10_UNORM_PACK32);
        // BindFormat2(Format::A2B10G10R10_SNORM_PACK32, VK_FORMAT_A2B10G10R10_SNORM_PACK32);
        // BindFormat2(Format::A2B10G10R10_USCALED_PACK32, VK_FORMAT_A2B10G10R10_USCALED_PACK32);
        // BindFormat2(Format::A2B10G10R10_SSCALED_PACK32, VK_FORMAT_A2B10G10R10_SSCALED_PACK32);
        // BindFormat2(Format::A2B10G10R10_UINT_PACK32, VK_FORMAT_A2B10G10R10_UINT_PACK32);
        // BindFormat2(Format::A2B10G10R10_SINT_PACK32, VK_FORMAT_A2B10G10R10_SINT_PACK32);
        BindFormat2(Format::R16_UNORM, VK_FORMAT_R16_UNORM);
        BindFormat2(Format::R16_SNORM, VK_FORMAT_R16_SNORM);
        // BindFormat2(Format::R16_USCALED, VK_FORMAT_R16_USCALED);
        // BindFormat2(Format::R16_SSCALED, VK_FORMAT_R16_SSCALED);
        BindFormat2(Format::R16_UINT, VK_FORMAT_R16_UINT);
        BindFormat2(Format::R16_SINT, VK_FORMAT_R16_SINT);
        BindFormat2(Format::R16_FLOAT, VK_FORMAT_R16_SFLOAT);
        BindFormat2(Format::R16G16_UNORM, VK_FORMAT_R16G16_UNORM);
        BindFormat2(Format::R16G16_SNORM, VK_FORMAT_R16G16_SNORM);
        // BindFormat2(Format::R16G16_USCALED, VK_FORMAT_R16G16_USCALED);
        // BindFormat2(Format::R16G16_SSCALED, VK_FORMAT_R16G16_SSCALED);
        BindFormat2(Format::R16G16_UINT, VK_FORMAT_R16G16_UINT);
        BindFormat2(Format::R16G16_SINT, VK_FORMAT_R16G16_SINT);
        BindFormat2(Format::R16G16_FLOAT, VK_FORMAT_R16G16_SFLOAT);
        // BindFormat2(Format::R16G16B16_UNORM, VK_FORMAT_R16G16B16_UNORM);
        // BindFormat2(Format::R16G16B16_SNORM, VK_FORMAT_R16G16B16_SNORM);
        // BindFormat2(Format::R16G16B16_USCALED, VK_FORMAT_R16G16B16_USCALED);
        // BindFormat2(Format::R16G16B16_SSCALED, VK_FORMAT_R16G16B16_SSCALED);
        // BindFormat2(Format::R16G16B16_UINT, VK_FORMAT_R16G16B16_UINT);
        // BindFormat2(Format::R16G16B16_SINT, VK_FORMAT_R16G16B16_SINT);
        // BindFormat2(Format::R16G16B16_SFLOAT, VK_FORMAT_R16G16B16_SFLOAT);
        BindFormat2(Format::R16G16B16A16_UNORM, VK_FORMAT_R16G16B16A16_UNORM);
        BindFormat2(Format::R16G16B16A16_SNORM, VK_FORMAT_R16G16B16A16_SNORM);
        // BindFormat2(Format::R16G16B16A16_USCALED, VK_FORMAT_R16G16B16A16_USCALED);
        // BindFormat2(Format::R16G16B16A16_SSCALED, VK_FORMAT_R16G16B16A16_SSCALED);
        BindFormat2(Format::R16G16B16A16_UINT, VK_FORMAT_R16G16B16A16_UINT);
        BindFormat2(Format::R16G16B16A16_SINT, VK_FORMAT_R16G16B16A16_SINT);
        BindFormat2(Format::R16G16B16A16_FLOAT, VK_FORMAT_R16G16B16A16_SFLOAT);
        BindFormat2(Format::R32_UINT, VK_FORMAT_R32_UINT);
        BindFormat2(Format::R32_SINT, VK_FORMAT_R32_SINT);
        BindFormat2(Format::R32_FLOAT, VK_FORMAT_R32_SFLOAT);
        BindFormat2(Format::R32G32_UINT, VK_FORMAT_R32G32_UINT);
        BindFormat2(Format::R32G32_SINT, VK_FORMAT_R32G32_SINT);
        BindFormat2(Format::R32G32_FLOAT, VK_FORMAT_R32G32_SFLOAT);
        BindFormat2(Format::R32G32B32_UINT, VK_FORMAT_R32G32B32_UINT);
        BindFormat2(Format::R32G32B32_SINT, VK_FORMAT_R32G32B32_SINT);
        BindFormat2(Format::R32G32B32_FLOAT, VK_FORMAT_R32G32B32_SFLOAT);
        BindFormat2(Format::R32G32B32A32_UINT, VK_FORMAT_R32G32B32A32_UINT);
        BindFormat2(Format::R32G32B32A32_SINT, VK_FORMAT_R32G32B32A32_SINT);
        BindFormat2(Format::R32G32B32A32_FLOAT, VK_FORMAT_R32G32B32A32_SFLOAT);
        // BindFormat2(Format::R64_UINT, VK_FORMAT_R64_UINT);
        // BindFormat2(Format::R64_SINT, VK_FORMAT_R64_SINT);
        // BindFormat2(Format::R64_FLOAT, VK_FORMAT_R64_SFLOAT);
        // BindFormat2(Format::R64G64_UINT, VK_FORMAT_R64G64_UINT);
        // BindFormat2(Format::R64G64_SINT, VK_FORMAT_R64G64_SINT);
        // BindFormat2(Format::R64G64_FLOAT, VK_FORMAT_R64G64_SFLOAT);
        // BindFormat2(Format::R64G64B64_UINT, VK_FORMAT_R64G64B64_UINT);
        // BindFormat2(Format::R64G64B64_SINT, VK_FORMAT_R64G64B64_SINT);
        // BindFormat2(Format::R64G64B64_FLOAT, VK_FORMAT_R64G64B64_SFLOAT);
        // BindFormat2(Format::R64G64B64A64_UINT, VK_FORMAT_R64G64B64A64_UINT);
        // BindFormat2(Format::R64G64B64A64_SINT, VK_FORMAT_R64G64B64A64_SINT);
        // BindFormat2(Format::R64G64B64A64_FLOAT, VK_FORMAT_R64G64B64A64_SFLOAT);
        BindFormat2(Format::R11G11B10_FLOAT, VK_FORMAT_B10G11R11_UFLOAT_PACK32);
        // BindFormat2(Format::E5B9G9R9_UFLOAT_PACK32, VK_FORMAT_E5B9G9R9_UFLOAT_PACK32);
        BindFormat2(Format::D16_UNORM, VK_FORMAT_D16_UNORM);
        // BindFormat2(Format::X8_D24_UNORM_PACK32, VK_FORMAT_X8_D24_UNORM_PACK32);             R24_UNORM_X8_TYPELESS?
        BindFormat2(Format::D32_FLOAT, VK_FORMAT_D32_SFLOAT);
        BindFormat2(Format::S8_UINT, VK_FORMAT_S8_UINT);
        BindFormat2(Format::D32_SFLOAT_S8_UINT, VK_FORMAT_D32_SFLOAT_S8_UINT);
        // BindFormat2(Format::D16_UNORM_S8_UINT, VK_FORMAT_D16_UNORM_S8_UINT);
        BindFormat2(Format::D24_UNORM_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT);
        // BindFormat2(Format::D32_FLOAT_S8_UINT, VK_FORMAT_D32_SFLOAT_S8_UINT);
        BindFormat1(Format::BC1_UNORM, VK_FORMAT_BC1_RGB_UNORM_BLOCK);
        BindFormat1(Format::BC1_UNORM_SRGB, VK_FORMAT_BC1_RGB_SRGB_BLOCK);
        BindFormat2(Format::BC1_UNORM, VK_FORMAT_BC1_RGBA_UNORM_BLOCK);
        BindFormat2(Format::BC1_UNORM_SRGB, VK_FORMAT_BC1_RGBA_SRGB_BLOCK);
        BindFormat2(Format::BC2_UNORM, VK_FORMAT_BC2_UNORM_BLOCK);
        BindFormat2(Format::BC2_UNORM_SRGB, VK_FORMAT_BC2_SRGB_BLOCK);
        BindFormat2(Format::BC3_UNORM, VK_FORMAT_BC3_UNORM_BLOCK);
        BindFormat2(Format::BC3_UNORM_SRGB, VK_FORMAT_BC3_SRGB_BLOCK);
        BindFormat2(Format::BC4_UNORM, VK_FORMAT_BC4_UNORM_BLOCK);
        BindFormat2(Format::BC4_SNORM, VK_FORMAT_BC4_SNORM_BLOCK);
        BindFormat2(Format::BC5_UNORM, VK_FORMAT_BC5_UNORM_BLOCK);
        BindFormat2(Format::BC5_SNORM, VK_FORMAT_BC5_SNORM_BLOCK);
        BindFormat2(Format::BC6H_UF16, VK_FORMAT_BC6H_UFLOAT_BLOCK);
        BindFormat2(Format::BC6H_SF16, VK_FORMAT_BC6H_SFLOAT_BLOCK);
        BindFormat2(Format::BC7_UNORM, VK_FORMAT_BC7_UNORM_BLOCK);
        BindFormat2(Format::BC7_UNORM_SRGB, VK_FORMAT_BC7_SRGB_BLOCK);
        // BindFormat2(Format::ETC2_R8G8B8_UNORM, VK_FORMAT_ETC2_R8G8B8_UNORM_BLOCK);
        // BindFormat2(Format::ETC2_R8G8B8_UNORM_SRGB, VK_FORMAT_ETC2_R8G8B8_SRGB_BLOCK);
        // BindFormat2(Format::ETC2_R8G8B8A1_UNORM, VK_FORMAT_ETC2_R8G8B8A1_UNORM_BLOCK);
        // BindFormat2(Format::ETC2_R8G8B8A1_UNORM_SRGB, VK_FORMAT_ETC2_R8G8B8A1_SRGB_BLOCK);
        // BindFormat2(Format::ETC2_R8G8B8A8_UNORM_BLOCK, VK_FORMAT_ETC2_R8G8B8A8_UNORM_BLOCK);
        // BindFormat2(Format::ETC2_R8G8B8A8_UNORM_SRGB, VK_FORMAT_ETC2_R8G8B8A8_SRGB_BLOCK);
        // BindFormat2(Format::EAC_R11_UNORM, VK_FORMAT_EAC_R11_UNORM_BLOCK);
        // BindFormat2(Format::EAC_R11_SNORM, VK_FORMAT_EAC_R11_SNORM_BLOCK);
        // BindFormat2(Format::EAC_R11G11_UNORM, VK_FORMAT_EAC_R11G11_UNORM_BLOCK);
        // BindFormat2(Format::EAC_R11G11_SNORM, VK_FORMAT_EAC_R11G11_SNORM_BLOCK);
        // BindFormat2(Format::ASTC_4x4_UNORM, VK_FORMAT_ASTC_4x4_UNORM_BLOCK);
        // BindFormat2(Format::ASTC_4x4_UNORM_SRGB, VK_FORMAT_ASTC_4x4_SRGB_BLOCK);
        // BindFormat2(Format::ASTC_5x4_UNORM, VK_FORMAT_ASTC_5x4_UNORM_BLOCK);
        // BindFormat2(Format::ASTC_5x4_UNORM_SRGB, VK_FORMAT_ASTC_5x4_SRGB_BLOCK);
        // BindFormat2(Format::ASTC_5x5_UNORM, VK_FORMAT_ASTC_5x5_UNORM_BLOCK);
        // BindFormat2(Format::ASTC_5x5_UNORM_SRGB, VK_FORMAT_ASTC_5x5_SRGB_BLOCK);
        // BindFormat2(Format::ASTC_6x5_UNORM, VK_FORMAT_ASTC_6x5_UNORM_BLOCK);
        // BindFormat2(Format::ASTC_6x5_UNORM_SRGB, VK_FORMAT_ASTC_6x5_SRGB_BLOCK);
        // BindFormat2(Format::ASTC_6x6_UNORM, VK_FORMAT_ASTC_6x6_UNORM_BLOCK);
        // BindFormat2(Format::ASTC_6x6_UNORM_SRGB, VK_FORMAT_ASTC_6x6_SRGB_BLOCK);
        // BindFormat2(Format::ASTC_8x5_UNORM, VK_FORMAT_ASTC_8x5_UNORM_BLOCK);
        // BindFormat2(Format::ASTC_8x5_UNORM_SRGB, VK_FORMAT_ASTC_8x5_SRGB_BLOCK);
        // BindFormat2(Format::ASTC_8x6_UNORM, VK_FORMAT_ASTC_8x6_UNORM_BLOCK);
        // BindFormat2(Format::ASTC_8x6_UNORM_SRGB, VK_FORMAT_ASTC_8x6_SRGB_BLOCK);
        // BindFormat2(Format::ASTC_8x8_UNORM, VK_FORMAT_ASTC_8x8_UNORM_BLOCK);
        // BindFormat2(Format::ASTC_8x8_UNORM_SRGB, VK_FORMAT_ASTC_8x8_SRGB_BLOCK);
        // BindFormat2(Format::ASTC_10x5_UNORM, VK_FORMAT_ASTC_10x5_UNORM_BLOCK);
        // BindFormat2(Format::ASTC_10x5_UNORM_SRGB, VK_FORMAT_ASTC_10x5_SRGB_BLOCK);
        // BindFormat2(Format::ASTC_10x6_UNORM, VK_FORMAT_ASTC_10x6_UNORM_BLOCK);
        // BindFormat2(Format::ASTC_10x6_UNORM_SRGB, VK_FORMAT_ASTC_10x6_SRGB_BLOCK);
        // BindFormat2(Format::ASTC_10x8_UNORM, VK_FORMAT_ASTC_10x8_UNORM_BLOCK);
        // BindFormat2(Format::ASTC_10x8_UNORM_SRGB, VK_FORMAT_ASTC_10x8_SRGB_BLOCK);
        // BindFormat2(Format::ASTC_10x10_UNORM, VK_FORMAT_ASTC_10x10_UNORM_BLOCK);
        // BindFormat2(Format::ASTC_10x10_UNORM_SRGB, VK_FORMAT_ASTC_10x10_SRGB_BLOCK);
        // BindFormat2(Format::ASTC_12x10_UNORM, VK_FORMAT_ASTC_12x10_UNORM_BLOCK);
        // BindFormat2(Format::ASTC_12x10_UNORM_SRGB, VK_FORMAT_ASTC_12x10_SRGB_BLOCK);
        // BindFormat2(Format::ASTC_12x12_UNORM, VK_FORMAT_ASTC_12x12_UNORM_BLOCK);
        // BindFormat2(Format::ASTC_12x12_UNORM_SRGB, VK_FORMAT_ASTC_12x12_SRGB_BLOCK);

        // Vulkan has no "typeless" constant -- so we should bind the typeless
        // formats onto some default type. Note the defaults here are picked arbitrarily,
        // based on commonly used formats -- but with SRGB formats avoided.
        BindFormatTypeless(Format::R32G32B32A32_TYPELESS, VK_FORMAT_R32G32B32A32_SFLOAT);
        BindFormatTypeless(Format::R32G32B32_TYPELESS, VK_FORMAT_R32G32B32_SFLOAT);
        BindFormatTypeless(Format::R16G16B16A16_TYPELESS, VK_FORMAT_R16G16B16A16_SFLOAT);
        BindFormatTypeless(Format::R32G32_TYPELESS, VK_FORMAT_R32G32_SFLOAT);
        BindFormatTypeless(Format::R8G8B8A8_TYPELESS, VK_FORMAT_R8G8B8A8_UNORM);
        BindFormatTypeless(Format::R16G16_TYPELESS, VK_FORMAT_R16G16_SFLOAT);
        BindFormatTypeless(Format::R32_TYPELESS, VK_FORMAT_D32_SFLOAT); // or VK_FORMAT_R32_SFLOAT?
        BindFormatTypeless(Format::R8G8_TYPELESS, VK_FORMAT_R8G8_UNORM);
        BindFormatTypeless(Format::R16_TYPELESS, VK_FORMAT_R16_SFLOAT);
        BindFormatTypeless(Format::R8_TYPELESS, VK_FORMAT_R8_UNORM);
        BindFormatTypeless(Format::BC1_TYPELESS, VK_FORMAT_BC1_RGBA_UNORM_BLOCK);
        BindFormatTypeless(Format::BC2_TYPELESS, VK_FORMAT_BC2_UNORM_BLOCK);
        BindFormatTypeless(Format::BC3_TYPELESS, VK_FORMAT_BC3_UNORM_BLOCK);
        BindFormatTypeless(Format::BC4_TYPELESS, VK_FORMAT_BC4_UNORM_BLOCK);
        BindFormatTypeless(Format::BC5_TYPELESS, VK_FORMAT_BC5_UNORM_BLOCK);
        BindFormatTypeless(Format::BC6H_TYPELESS, VK_FORMAT_BC6H_SFLOAT_BLOCK);
        BindFormatTypeless(Format::BC7_TYPELESS, VK_FORMAT_BC7_UNORM_BLOCK);
        BindFormatTypeless(Format::B8G8R8A8_TYPELESS, VK_FORMAT_B8G8R8A8_UNORM);
		BindFormatTypeless(Format::R24_UNORM_X8_TYPELESS, VK_FORMAT_D24_UNORM_S8_UINT);
        BindFormatTypeless(Format::X24_TYPELESS_G8_UINT, VK_FORMAT_D24_UNORM_S8_UINT);
		BindFormatTypeless(Format::R24G8_TYPELESS, VK_FORMAT_D24_UNORM_S8_UINT);

		// The A8 is an awkward throughback to DX9. Ideally it should be completely removed
		BindFormatTypeless(Format::A8_UNORM, VK_FORMAT_R8_UNORM);

        s_lookupTablesInitialized = true;
    }

    VkFormat_ AsVkFormat(Format input)
    {
        assert(s_lookupTablesInitialized);
        // Assuming NativeFormat::Enum and VkFormat are not the same thing, there's
        // just no way to do this conversion easily. We have to use a big lookup
        // table
        if (unsigned(input) > dimof(s_genericToVkTable))
            return VK_FORMAT_UNDEFINED;
        return s_genericToVkTable[unsigned(input)];
    }

	Format AsFormat(VkFormat_ input)
	{
		assert(s_lookupTablesInitialized);
		// Assuming NativeFormat::Enum and VkFormat are not the same thing, there's
		// just no way to do this conversion easily. We have to use a big lookup
		// table
		if (unsigned(input) > dimof(s_vkToGenericTable))
			return Format::Unknown;
		return s_vkToGenericTable[unsigned(input)];
	}

///////////////////////////////////////////////////////////////////////////////////////////////////

}}

