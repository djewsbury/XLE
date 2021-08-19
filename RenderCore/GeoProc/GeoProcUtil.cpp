// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "GeoProcUtil.h"
#include "NascentRawGeometry.h"
#include "../Format.h"
#include "../../Utility/StringUtils.h"
#include "../../Foreign/half-1.9.2/include/half.hpp"

#include <cstdlib>

namespace RenderCore { namespace Assets { namespace GeoProc
{
    unsigned int FloatBits(float input)
    {
            // (or just use a reinterpret cast)
        union Converter { float f; unsigned int i; };
        Converter c; c.f = input; 
        return c.i;
    }

    unsigned int FloatBits(double input)
    {
            // (or just use a reinterpret cast)
        union Converter { float f; unsigned int i; };
        Converter c; c.f = float(input); 
        return c.i;
    }

    void AddToBoundingBox(  std::pair<Float3, Float3>& boundingBox,
                            const Float3& localPosition, const Float4x4& localToWorld)
    {
        Float3 transformedPosition = Truncate(localToWorld * Expand(localPosition, 1.f));

        boundingBox.first[0]    = std::min(transformedPosition[0], boundingBox.first[0]);
        boundingBox.first[1]    = std::min(transformedPosition[1], boundingBox.first[1]);
        boundingBox.first[2]    = std::min(transformedPosition[2], boundingBox.first[2]);
        boundingBox.second[0]   = std::max(transformedPosition[0], boundingBox.second[0]);
        boundingBox.second[1]   = std::max(transformedPosition[1], boundingBox.second[1]);
        boundingBox.second[2]   = std::max(transformedPosition[2], boundingBox.second[2]);
    }

    static float UNormAsFloat32(unsigned char unormValue)
    {
        return float(unormValue) / 255.f;
    }

	static float UNormAsFloat32(uint16_t unormValue)
    {
        return float(unormValue) / 65535.f;
    }

	static float SNormAsFloat32(int16_t unormValue)
    {
        return float(unormValue) / 32767.f;
    }

    float Float16AsFloat32(unsigned short input)
    {
        return half_float::detail::half2float(input);
    }

#pragma clang diagnostic ignored "-Wswitch"     // (enumeration values not handled)

    Float4 AsFloat4(const void* rawData, unsigned nativeFormat)
    {   
            //
            //      todo -- this needs to move to the metal layer, so it can use
            //              platform specific formats
            //
        switch ((Format)nativeFormat) {
        case Format::R32G32B32A32_FLOAT:    return *(const Float4*)rawData;
        case Format::R32G32B32_FLOAT:       return Float4(((const float*)rawData)[0], ((const float*)rawData)[1], ((const float*)rawData)[2], 0.f);
        case Format::R32G32_FLOAT:          return Float4(((const float*)rawData)[0], ((const float*)rawData)[1], 0.f, 1.f);
        case Format::R32_FLOAT:             return Float4(((const float*)rawData)[0], 0.f, 0.f, 1.f);

        case Format::R10G10B10A2_UNORM:
        case Format::R10G10B10A2_UINT:
        case Format::R11G11B10_FLOAT:       
        case Format::B5G6R5_UNORM:
        case Format::B5G5R5A1_UNORM:        assert(0); return Float4(0,0,0,1);  // requires some custom adjustments (these are uncommon uses, anyway)

        case Format::R16G16B16A16_FLOAT:    return Float4(Float16AsFloat32(((const unsigned short*)rawData)[0]), Float16AsFloat32(((const unsigned short*)rawData)[1]), Float16AsFloat32(((const unsigned short*)rawData)[2]), Float16AsFloat32(((const unsigned short*)rawData)[3]));
        case Format::R16G16_FLOAT:          return Float4(Float16AsFloat32(((const unsigned short*)rawData)[0]), Float16AsFloat32(((const unsigned short*)rawData)[1]), 0.f, 1.f);
        case Format::R16_FLOAT:             return Float4(Float16AsFloat32(((const unsigned short*)rawData)[0]), 0.f, 0.f, 1.f);

        case Format::B8G8R8A8_UNORM:
        case Format::R8G8B8A8_UNORM:        return Float4(UNormAsFloat32(((const unsigned char*)rawData)[0]), UNormAsFloat32(((const unsigned char*)rawData)[1]), UNormAsFloat32(((const unsigned char*)rawData)[2]), UNormAsFloat32(((const unsigned char*)rawData)[3]));
        case Format::R8G8_UNORM:            return Float4(UNormAsFloat32(((const unsigned char*)rawData)[0]), UNormAsFloat32(((const unsigned char*)rawData)[1]), 0.f, 1.f);
        case Format::R8_UNORM:              return Float4(UNormAsFloat32(((const unsigned char*)rawData)[0]), 0.f, 0.f, 1.f);
        
        case Format::B8G8R8X8_UNORM:        return Float4(UNormAsFloat32(((const unsigned char*)rawData)[0]), UNormAsFloat32(((const unsigned char*)rawData)[1]), UNormAsFloat32(((const unsigned char*)rawData)[2]), 1.f);

		case Format::R16G16B16A16_UNORM:	return Float4(UNormAsFloat32(((const uint16_t*)rawData)[0]), UNormAsFloat32(((const uint16_t*)rawData)[1]), UNormAsFloat32(((const uint16_t*)rawData)[2]), UNormAsFloat32(((const uint16_t*)rawData)[3]));
		case Format::R16G16B16A16_SNORM:	return Float4(SNormAsFloat32(((const int16_t*)rawData)[0]), SNormAsFloat32(((const int16_t*)rawData)[1]), SNormAsFloat32(((const int16_t*)rawData)[2]), SNormAsFloat32(((const int16_t*)rawData)[3]));
            
        default:
                assert(0);
        }

        assert(0);
        return Float4(0,0,0,1);
    }

    void AddToBoundingBox(  std::pair<Float3, Float3>& boundingBox,
                            const void* vertexData, size_t vertexStride, size_t vertexCount,
                            const Assets::VertexElement& elementDesc, 
                            const Float4x4& localToWorld)
    {
            //
            //      We need to have an explicit aligned byte offset for this call. If we
            //      pass in ~unsigned(0x0), it means "pack tightly after the previous element",
            //      But since we don't know the previous elements, we can't be sure
            //
        assert(elementDesc._alignedByteOffset != ~unsigned(0x0));
        for (size_t c=0; c<vertexCount; ++c) {
            const void* v    = PtrAdd(vertexData, vertexStride*c + elementDesc._alignedByteOffset);
            Float3 position  = Truncate(AsFloat4(v, (unsigned)elementDesc._nativeFormat));
            assert(!std::isinf(position[0]) && !std::isinf(position[1]) && !std::isinf(position[2]));
            AddToBoundingBox(boundingBox, position, localToWorld);
        }
    }

    std::pair<Float3, Float3>       InvalidBoundingBox()
    {
        const Float3 mins(      std::numeric_limits<Float3::value_type>::max(),
                                std::numeric_limits<Float3::value_type>::max(),
                                std::numeric_limits<Float3::value_type>::max());
        const Float3 maxs(      -std::numeric_limits<Float3::value_type>::max(),
                                -std::numeric_limits<Float3::value_type>::max(),
                                -std::numeric_limits<Float3::value_type>::max());
        return std::make_pair(mins, maxs);
    }

    Assets::VertexElement FindPositionElement(const Assets::VertexElement elements[], size_t elementCount)
    {
        for (unsigned c=0; c<elementCount; ++c)
            if (elements[c]._semanticIndex == 0 && !XlCompareStringI(elements[c]._semanticName, "POSITION"))
                return elements[c];
        return Assets::VertexElement();
    }

}}}

