// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "MeshDatabase.h"
#include "../Format.h"
#include "../Types.h"
#include "../VertexUtil.h"
#include "../../Assets/AssetsCore.h"
#include "../../Math/Vector.h"
#include "../../Math/XLEMath.h"
#include "../../OSServices/Log.h"
#include "../../Utility/StringUtils.h"
#include "../../Utility/MemoryUtils.h"
#include "../../Utility/IteratorUtils.h"
#include "../../Utility/BitUtils.h"
#include "../../Core/Exceptions.h"
#include <iterator>
#include <queue>
#include <cfloat>

namespace RenderCore { namespace Assets { namespace GeoProc
{
	inline void GetVertDataF32(
        float* dst, 
        const float* src, unsigned srcComponentCount,
        ProcessingFlags::BitField processingFlags)
    {
		RenderCore::Internal::GetVertDataF32(dst, src, srcComponentCount);

        if (processingFlags & ProcessingFlags::TexCoordFlip) {
            dst[1] = 1.0f - dst[1];
        } else if (processingFlags & ProcessingFlags::BitangentFlip) {
            dst[0] = -dst[0];
            dst[1] = -dst[1];
            dst[2] = -dst[2];
        } else if (processingFlags & ProcessingFlags::TangentHandinessFlip) {
            dst[3] = -dst[3];
        }
    }

	inline void GetVertDataF16(
        float* dst, 
        const uint16* src, unsigned srcComponentCount,
        ProcessingFlags::BitField processingFlags)
    {
		RenderCore::Internal::GetVertDataF16(dst, src, srcComponentCount);
		if (processingFlags & ProcessingFlags::Renormalize) {
			float scale = 1.0f;
			if (XlRSqrt_Checked(&scale, dst[0] * dst[0] + dst[1] * dst[1] + dst[2] * dst[2]))
				dst[0] *= scale; dst[1] *= scale; dst[2] *= scale;
		}

        if (processingFlags & ProcessingFlags::TexCoordFlip) {
            dst[1] = 1.0f - dst[1];
        } else if (processingFlags & ProcessingFlags::BitangentFlip) {
            dst[0] = -dst[0];
            dst[1] = -dst[1];
            dst[2] = -dst[2];
        } else if (processingFlags & ProcessingFlags::TangentHandinessFlip) {
            dst[3] = -dst[3];
        }
    }

	inline void GetVertDataUNorm16(
		float* dst,
		const uint16* src, unsigned srcComponentCount,
		ProcessingFlags::BitField processingFlags)
	{
		RenderCore::Internal::GetVertDataUNorm16(dst, src, srcComponentCount);
		if (processingFlags & ProcessingFlags::Renormalize) {
			float scale = 1.0f;
			if (XlRSqrt_Checked(&scale, dst[0] * dst[0] + dst[1] * dst[1] + dst[2] * dst[2]))
				dst[0] *= scale; dst[1] *= scale; dst[2] *= scale;
		}

		if (processingFlags & ProcessingFlags::TexCoordFlip) {
			dst[1] = 1.0f - dst[1];
		}
		else if (processingFlags & ProcessingFlags::BitangentFlip) {
			dst[0] = -dst[0];
			dst[1] = -dst[1];
			dst[2] = -dst[2];
		}
		else if (processingFlags & ProcessingFlags::TangentHandinessFlip) {
			dst[3] = -dst[3];
		}
	}

	inline void GetVertDataSNorm16(
		float* dst,
		const int16* src, unsigned srcComponentCount,
		ProcessingFlags::BitField processingFlags)
	{
		RenderCore::Internal::GetVertDataSNorm16(dst, src, srcComponentCount);
		if (processingFlags & ProcessingFlags::Renormalize) {
			float scale = 1.0f;
			if (XlRSqrt_Checked(&scale, dst[0] * dst[0] + dst[1] * dst[1] + dst[2] * dst[2]))
				dst[0] *= scale; dst[1] *= scale; dst[2] *= scale;
		}

		if (processingFlags & ProcessingFlags::TexCoordFlip) {
			dst[1] = 1.0f - dst[1];
		}
		else if (processingFlags & ProcessingFlags::BitangentFlip) {
			dst[0] = -dst[0];
			dst[1] = -dst[1];
			dst[2] = -dst[2];
		}
		else if (processingFlags & ProcessingFlags::TangentHandinessFlip) {
			dst[3] = -dst[3];
		}
	}

	inline void GetVertData(
        float* dst, 
        const void* src, BrokenDownFormat fmt,
        ProcessingFlags::BitField processingFlags)
    {
        switch (fmt._type) {
        case VertexUtilComponentType::Float32:
            GetVertDataF32(dst, (const float*)src, fmt._componentCount, processingFlags);
            break;
        case VertexUtilComponentType::Float16:
            GetVertDataF16(dst, (const uint16*)src, fmt._componentCount, processingFlags);
            break;
		case VertexUtilComponentType::UNorm16:
			GetVertDataUNorm16(dst, (const uint16*)src, fmt._componentCount, processingFlags);
			break;
		case VertexUtilComponentType::SNorm16:
			GetVertDataSNorm16(dst, (const int16*)src, fmt._componentCount, processingFlags);
			break;
        default:
            UNREACHABLE();
            break;
        }
    }

///////////////////////////////////////////////////////////////////////////////////////////////////

    template<> Float3 GetVertex(const IVertexSourceData& sourceData, size_t index)
    {
        auto stride = sourceData.GetStride();
        const auto* sourceStart = PtrAdd(sourceData.GetData().begin(), index * stride);

        float input[4];
        GetVertData(input, (const float*)sourceStart, BreakdownFormat(sourceData.GetFormat()), sourceData.GetProcessingFlags());
        return Float3(input[0], input[1], input[2]);
    }

    template<> Float2 GetVertex(const IVertexSourceData& sourceData, size_t index)
    {
        auto stride = sourceData.GetStride();
        const auto* sourceStart = PtrAdd(sourceData.GetData().begin(), index * stride);

        float input[4];
        GetVertData(input, (const float*)sourceStart, BreakdownFormat(sourceData.GetFormat()), sourceData.GetProcessingFlags());
        return Float2(input[0], input[1]);
    }

    template<> Float4 GetVertex(const IVertexSourceData& sourceData, size_t index)
    {
        auto stride = sourceData.GetStride();
        const auto* sourceStart = PtrAdd(sourceData.GetData().begin(), index * stride);

        float input[4];
        GetVertData(input, (const float*)sourceStart, BreakdownFormat(sourceData.GetFormat()), sourceData.GetProcessingFlags());
        return Float4(input[0], input[1], input[2], input[3]);
    }

    template<> float GetVertex(const IVertexSourceData& sourceData, size_t index)
    {
        auto stride = sourceData.GetStride();
        const auto* sourceStart = PtrAdd(sourceData.GetData().begin(), index * stride);

        float input[4];
        GetVertData(input, (const float*)sourceStart, BreakdownFormat(sourceData.GetFormat()), sourceData.GetProcessingFlags());
        return input[0];
    }

    void CopyVertexData(
        const void* dst, Format dstFmt, size_t dstStride, size_t dstDataSize,
        const void* src, Format srcFmt, size_t srcStride, size_t srcDataSize,
        unsigned count, 
        IteratorRange<const unsigned*> mapping,
        ProcessingFlags::BitField processingFlags)
    {
        auto dstFormat = BreakdownFormat(dstFmt);
        auto srcFormat = BreakdownFormat(srcFmt);
		auto dstFormatSize = BitsPerPixel(dstFmt) / 8;
		auto srcFormatSize = BitsPerPixel(srcFmt) / 8;
		(void)srcFormatSize;
        assert(dstStride != 0);     // never use zero strides -- you'll just end up with duplicated data
        assert(srcStride != 0);
        assert(count != 0);

            //      This could be be made more efficient with a smarter loop..
        if (srcFormat._type == VertexUtilComponentType::Float32) {

            if (dstFormat._type == VertexUtilComponentType::Float32) {  ////////////////////////////////////////////////

                for (unsigned v = 0; v<count; ++v, dst = PtrAdd(dst, dstStride)) {
                    auto srcIndex = (v < mapping.size()) ? mapping[v] : v;
                    assert(srcIndex * srcStride + sizeof(float) <= srcDataSize);
                    auto* srcV = PtrAdd(src, srcIndex * srcStride);

                    float input[4] = {0.f, 0.f, 0.f, 1.0f};
                    GetVertDataF32(input, (const float*)srcV, srcFormat._componentCount, processingFlags);

                    for (unsigned c=0; c<dstFormat._componentCount; ++c) {
                        assert(&((float*)dst)[c+1] <= PtrAdd(dst, dstDataSize));
                        ((float*)dst)[c] = input[c];
                    }
                }

            } else if (dstFormat._type == VertexUtilComponentType::Float16) {  ////////////////////////////////////////////////

                for (unsigned v = 0; v<count; ++v, dst = PtrAdd(dst, dstStride)) {
                    auto srcIndex = (v < mapping.size()) ? mapping[v] : v;
                    assert(srcIndex * srcStride + sizeof(float) <= srcDataSize);
                    auto* srcV = PtrAdd(src, srcIndex * srcStride);

                    float input[4] = {0.f, 0.f, 0.f, 1.0f};
                    GetVertDataF32(input, (const float*)srcV, srcFormat._componentCount, processingFlags);

                    for (unsigned c=0; c<dstFormat._componentCount; ++c) {
                        assert(&((unsigned short*)dst)[c+1] <= PtrAdd(dst, dstDataSize));
                        ((unsigned short*)dst)[c] = AsFloat16(input[c]);
                    }
                }

            } else if (dstFormat._type == VertexUtilComponentType::UNorm8) {  ////////////////////////////////////////////////

                for (unsigned v = 0; v<count; ++v, dst = PtrAdd(dst, dstStride)) {
                    auto srcIndex = (v < mapping.size()) ? mapping[v] : v;
                    assert(srcIndex * srcStride + sizeof(float) <= srcDataSize);
                    auto* srcV = PtrAdd(src, srcIndex * srcStride);

                    float input[4] = {0.f, 0.f, 0.f, 1.0f};
                    GetVertDataF32(input, (const float*)srcV, srcFormat._componentCount, processingFlags);

                    for (unsigned c=0; c<dstFormat._componentCount; ++c) {
                        assert(&((unsigned char*)dst)[c+1] <= PtrAdd(dst, dstDataSize));
                        ((unsigned char*)dst)[c] = (unsigned char)Clamp(((float*)input)[c]*255.f, 0.f, 255.f);
                    }
                }

            } else {
                Throw(std::runtime_error("Error while copying vertex data. Unexpected format for destination parameter."));
            }

		} else if (srcFormat._type == VertexUtilComponentType::Float16) {

			if (dstFormat._type == VertexUtilComponentType::Float32) {  ////////////////////////////////////////////////

                for (unsigned v = 0; v<count; ++v, dst = PtrAdd(dst, dstStride)) {
                    auto srcIndex = (v < mapping.size()) ? mapping[v] : v;
                    assert(srcIndex * srcStride + sizeof(uint16_t) <= srcDataSize);
                    auto* srcV = PtrAdd(src, srcIndex * srcStride);

                    float input[4] = {0.f, 0.f, 0.f, 1.0f};
                    GetVertDataF16(input, (const uint16_t*)srcV, srcFormat._componentCount, processingFlags);

                    for (unsigned c=0; c<dstFormat._componentCount; ++c) {
                        assert(&((float*)dst)[c+1] <= PtrAdd(dst, dstDataSize));
                        ((float*)dst)[c] = input[c];
                    }
                }

            } else if (dstFormat._type == VertexUtilComponentType::Float16) {  ////////////////////////////////////////////////

                for (unsigned v = 0; v<count; ++v, dst = PtrAdd(dst, dstStride)) {
                    auto srcIndex = (v < mapping.size()) ? mapping[v] : v;
                    assert(srcIndex * srcStride + sizeof(uint16_t) <= srcDataSize);
                    auto* srcV = PtrAdd(src, srcIndex * srcStride);

                    float input[4] = {0.f, 0.f, 0.f, 1.0f};
                    GetVertDataF16(input, (const uint16_t*)srcV, srcFormat._componentCount, processingFlags);

                    for (unsigned c=0; c<dstFormat._componentCount; ++c) {
                        assert(&((unsigned short*)dst)[c+1] <= PtrAdd(dst, dstDataSize));
                        ((unsigned short*)dst)[c] = AsFloat16(input[c]);
                    }
                }

            } else if (dstFormat._type == VertexUtilComponentType::UNorm8) {  ////////////////////////////////////////////////

                for (unsigned v = 0; v<count; ++v, dst = PtrAdd(dst, dstStride)) {
                    auto srcIndex = (v < mapping.size()) ? mapping[v] : v;
                    assert(srcIndex * srcStride + sizeof(uint16_t) <= srcDataSize);
                    auto* srcV = PtrAdd(src, srcIndex * srcStride);

                    float input[4] = {0.f, 0.f, 0.f, 1.0f};
                    GetVertDataF16(input, (const uint16_t*)srcV, srcFormat._componentCount, processingFlags);

                    for (unsigned c=0; c<dstFormat._componentCount; ++c) {
                        assert(&((unsigned char*)dst)[c+1] <= PtrAdd(dst, dstDataSize));
                        ((unsigned char*)dst)[c] = (unsigned char)Clamp(((float*)input)[c]*255.f, 0.f, 255.f);
                    }
                }

            } else {
                Throw(std::runtime_error("Error while copying vertex data. Unexpected format for destination parameter."));
            }

        } else if (srcFormat._type == dstFormat._type &&  srcFormat._componentCount == dstFormat._componentCount) {

                // simple copy of uint8 data
            for (unsigned v = 0; v<count; ++v, dst = PtrAdd(dst, dstStride)) {
                auto srcIndex = (v < mapping.size()) ? mapping[v] : v;
                assert(srcIndex * srcStride + srcFormatSize <= srcDataSize);

                auto* srcV = (uint8*)PtrAdd(src, srcIndex * srcStride);
                for (unsigned c=0; c<dstFormatSize; ++c) {
                    assert(&((uint8*)dst)[c+1] <= PtrAdd(dst, dstDataSize));
                    ((uint8*)dst)[c] = srcV[c];
                }
            }

		} else if (srcFormat._type == VertexUtilComponentType::UNorm16) {

			if (dstFormat._type == VertexUtilComponentType::Float32) {  ////////////////////////////////////////////////

				for (unsigned v = 0; v<count; ++v, dst = PtrAdd(dst, dstStride)) {
                    auto srcIndex = (v < mapping.size()) ? mapping[v] : v;
                    assert(srcIndex * srcStride + sizeof(uint16_t) <= srcDataSize);
                    auto* srcV = PtrAdd(src, srcIndex * srcStride);

                    float input[4] = {0.f, 0.f, 0.f, 1.0f};
                    GetVertDataUNorm16(input, (const uint16_t*)srcV, srcFormat._componentCount, processingFlags);

                    for (unsigned c=0; c<dstFormat._componentCount; ++c) {
                        assert(&((float*)dst)[c+1] <= PtrAdd(dst, dstDataSize));
                        ((float*)dst)[c] = input[c];
                    }
                }

			} else {
                Throw(std::runtime_error("Error while copying vertex data. Unexpected format for destination parameter."));
            }

		} else if (srcFormat._type == VertexUtilComponentType::SNorm16) {

			if (dstFormat._type == VertexUtilComponentType::Float32) {  ////////////////////////////////////////////////

				for (unsigned v = 0; v<count; ++v, dst = PtrAdd(dst, dstStride)) {
                    auto srcIndex = (v < mapping.size()) ? mapping[v] : v;
                    assert(srcIndex * srcStride + sizeof(uint16_t) <= srcDataSize);
                    auto* srcV = PtrAdd(src, srcIndex * srcStride);

                    float input[4] = {0.f, 0.f, 0.f, 1.0f};
                    GetVertDataUNorm16(input, (const uint16_t*)srcV, srcFormat._componentCount, processingFlags);

                    for (unsigned c=0; c<dstFormat._componentCount; ++c) {
                        assert(&((float*)dst)[c+1] <= PtrAdd(dst, dstDataSize));
                        ((float*)dst)[c] = input[c];
                    }
                }

			} else {
                Throw(std::runtime_error("Error while copying vertex data. Unexpected format for destination parameter."));
            }

        } else {
            Throw(std::runtime_error("Error while copying vertex data. Format not supported."));
        }
    }

	void Copy(IteratorRange<VertexElementIterator> destination, IteratorRange<VertexElementIterator> source, unsigned vertexCount)
	{
		assert(destination.size() >= vertexCount);
		assert(source.size() >= vertexCount);
		CopyVertexData(
			destination.begin()._data.begin(),
			destination.begin()._format,
			destination.begin()._stride,
			destination.begin()._data.size(),
			source.begin()._data.begin(),
			source.begin()._format,
			source.begin()._stride,
			source.begin()._data.size(),
			vertexCount);
	}

///////////////////////////////////////////////////////////////////////////////////////////////////

    unsigned    MeshDatabase::HasElement(const char name[]) const
    {
        unsigned result = 0;
        for (auto i = _streams.cbegin(); i != _streams.cend(); ++i) {
            if (!XlCompareStringI(i->GetSemanticName().c_str(), name)) {
                assert((result & (1 << i->GetSemanticIndex())) == 0);
                result |= (1 << i->GetSemanticIndex());
            }
        }
        return result;
    }

    unsigned    MeshDatabase::FindElement(const char name[], unsigned semanticIndex) const
    {
        for (auto i = _streams.cbegin(); i != _streams.cend(); ++i)
            if (i->GetSemanticIndex() == semanticIndex && !XlCompareStringI(i->GetSemanticName().c_str(), name))
                return unsigned(std::distance(_streams.cbegin(), i));
        return ~0u;
    }

    void        MeshDatabase::RemoveStream(unsigned elementIndex)
    {
        if (elementIndex < _streams.size())
            _streams.erase(_streams.begin() + elementIndex);
    }

    template<typename Type> 
        Type MeshDatabase::GetUnifiedElement(size_t vertexIndex, unsigned elementIndex) const
    {
        auto& stream = _streams[elementIndex];
		if (vertexIndex < stream.GetVertexMap().size())
			vertexIndex = stream.GetVertexMap()[vertexIndex];
        auto& sourceData = *stream.GetSourceData();
        return GetVertex<Type>(sourceData, vertexIndex);
    }

    template Float3 MeshDatabase::GetUnifiedElement(size_t vertexIndex, unsigned elementIndex) const;
    template Float2 MeshDatabase::GetUnifiedElement(size_t vertexIndex, unsigned elementIndex) const;

    std::unique_ptr<uint32[]> MeshDatabase::BuildUnifiedVertexIndexToPositionIndex() const
    {
            //      Collada has this idea of "vertex index"; which is used to map
            //      on the vertex weight information. But that seems to be lost in OpenCollada.
            //      All we can do is use the position index as a subtitute.

        auto unifiedVertexIndexToPositionIndex = std::make_unique<uint32[]>(_unifiedVertexCount);
        
        if (!_streams[0].GetVertexMap().empty()) {
            for (size_t v=0; v<_unifiedVertexCount; ++v) {
                // assuming the first element is the position
                auto attributeIndex = _streams[0].GetVertexMap()[v];
                // assert(!_streams[0]._sourceData.IsValid() || attributeIndex < _mesh->getPositions().getValuesCount());
                unifiedVertexIndexToPositionIndex[v] = (uint32)attributeIndex;
            }
        } else {
            for (size_t v=0; v<_unifiedVertexCount; ++v) {
                unifiedVertexIndexToPositionIndex[v] = (uint32)v;
            }
        }

        return unifiedVertexIndexToPositionIndex;
    }

    void MeshDatabase::WriteStream(
        const Stream& stream,
        const void* dst, Format dstFormat, size_t dstStride, size_t dstSize) const
    {
        const auto& sourceData = *stream.GetSourceData();
        auto stride = sourceData.GetStride();
        CopyVertexData(
            dst, dstFormat, dstStride, dstSize,
            sourceData.GetData().begin(), sourceData.GetFormat(), stride, sourceData.GetData().size(),
            (unsigned)_unifiedVertexCount, 
            stream.GetVertexMap(), sourceData.GetProcessingFlags());
    }

    std::vector<uint8_t>  MeshDatabase::BuildNativeVertexBuffer(const NativeVBLayout& outputLayout) const
    {
            //
            //      Write the data into the vertex buffer
            //
        auto size = outputLayout._vertexStride * _unifiedVertexCount;
        std::vector<uint8_t> finalVertexBuffer(size);
        XlSetMemory(finalVertexBuffer.data(), 0, size);

        for (unsigned elementIndex = 0; elementIndex <_streams.size(); ++elementIndex) {
            const auto& nativeElement     = outputLayout._elements[elementIndex];
            const auto& stream            = _streams[elementIndex];
            WriteStream(
                stream, PtrAdd(finalVertexBuffer.data(), nativeElement._alignedByteOffset),
                nativeElement._nativeFormat, outputLayout._vertexStride,
                size - nativeElement._alignedByteOffset);
        }

        return finalVertexBuffer;
    }

    unsigned    MeshDatabase::AddStream(
        std::shared_ptr<IVertexSourceData> dataSource,
        std::vector<unsigned>&& vertexMap,
        const char semantic[], unsigned semanticIndex)
    {
        return InsertStream(~0u, dataSource, std::move(vertexMap), semantic, semanticIndex);
    }

    unsigned    MeshDatabase::InsertStream(
        unsigned insertionPosition,
        std::shared_ptr<IVertexSourceData> dataSource,
        std::vector<unsigned>&& vertexMap,
        const char semantic[], unsigned semanticIndex)
    {
        auto count = vertexMap.size() ? vertexMap.size() : dataSource->GetCount();
        assert(count > 0);
        if (!_unifiedVertexCount) { _unifiedVertexCount = count; }
        else _unifiedVertexCount = std::min(_unifiedVertexCount, count);

        if (insertionPosition == ~0u) {
            _streams.push_back(
                Stream { std::move(dataSource), std::move(vertexMap), semantic, semanticIndex });
            return unsigned(_streams.size()-1);
        } else {
            _streams.insert(
                _streams.begin()+insertionPosition,
                Stream { std::move(dataSource), std::move(vertexMap), semantic, semanticIndex });
            return insertionPosition;
        }
    }

    MeshDatabase::MeshDatabase()
    {
        _unifiedVertexCount = 0;
    }

    MeshDatabase::MeshDatabase(MeshDatabase&& moveFrom) never_throws
    : _streams(std::move(moveFrom._streams))
    , _unifiedVertexCount(moveFrom._unifiedVertexCount)
    {}

    MeshDatabase& MeshDatabase::operator=(MeshDatabase&& moveFrom) never_throws
    {
        _streams = std::move(moveFrom._streams);
        _unifiedVertexCount = moveFrom._unifiedVertexCount;
        return *this;
    }

    MeshDatabase::~MeshDatabase() {}

///////////////////////////////////////////////////////////////////////////////////////////////////

    MeshDatabase::Stream::Stream() { _semanticIndex = 0; }
    MeshDatabase::Stream::Stream(
        std::shared_ptr<IVertexSourceData> sourceData, std::vector<unsigned> vertexMap, 
        const std::string& semanticName, unsigned semanticIndex)
    : _sourceData(std::move(sourceData)), _vertexMap(std::move(vertexMap))
    , _semanticName(semanticName), _semanticIndex(semanticIndex)
    {}

    MeshDatabase::Stream::Stream(Stream&& moveFrom) never_throws
    : _sourceData(std::move(moveFrom._sourceData))
    , _vertexMap(std::move(moveFrom._vertexMap))
    , _semanticName(std::move(moveFrom._semanticName))
    , _semanticIndex(moveFrom._semanticIndex)
    {
    }

    auto MeshDatabase::Stream::operator=(Stream&& moveFrom) never_throws -> Stream&
    {
        _sourceData = std::move(moveFrom._sourceData);
        _vertexMap = std::move(moveFrom._vertexMap);
        _semanticName = std::move(moveFrom._semanticName);
        _semanticIndex = moveFrom._semanticIndex;
        return *this;
    }

    MeshDatabase::Stream::~Stream() {}

///////////////////////////////////////////////////////////////////////////////////////////////////

    static Format CalculateFinalVBFormat(const IVertexSourceData& source, const NativeVBSettings& settings)
    {
            //
            //      Calculate a native format that matches this source data.
            //      Actually, there are a limited number of relevant native formats.
            //      So, it's easy to find one that works.
            //
            //      We don't support doubles in vertex buffers. So we can only choose from
            //
            //          R32G32B32A32_FLOAT
            //          R32G32B32_FLOAT
            //          R32G32_FLOAT
            //          R32_FLOAT
            //
            //          (assuming R9G9B9E5_SHAREDEXP, etc, not valid for vertex buffers)
            //          R10G10B10A2_UNORM   (ok for DX 11.1 -- but DX11??)
            //          R10G10B10A2_UINT    (ok for DX 11.1 -- but DX11??)
            //          R11G11B10_FLOAT     (ok for DX 11.1 -- but DX11??)
            //
            //          R8G8B8A8_UNORM      (SRGB can't be used)
            //          R8G8_UNORM
            //          R8_UNORM
            //          B8G8R8A8_UNORM
            //          B8G8R8X8_UNORM
            //
            //          B5G6R5_UNORM        (on some hardware)
            //          B5G5R5A1_UNORM      (on some hardware)
            //          B4G4R4A4_UNORM      (on some hardware)
            //
            //          R16G16B16A16_FLOAT
            //          R16G16_FLOAT
            //          R16_FLOAT
            //
            //          (or UINT, SINT, UNORM, SNORM versions of the same thing)
            //

        auto brkdn = BreakdownFormat(source.GetFormat());
        if (!brkdn._componentCount)
            return source.GetFormat();

        if (source.GetFormatHint() & FormatHint::IsColor) {
            if (brkdn._componentCount == 1)         return Format::R8_UNORM;
            else if (brkdn._componentCount == 2)    return Format::R8G8_UNORM;
            else                                    return Format::R8G8B8A8_UNORM;
        }

        // If we start with 32 bit floats here, we can decide to convert them to 16 bit
        if (settings._use16BitFloats && brkdn._type == VertexUtilComponentType::Float32) {
            if (brkdn._componentCount == 1)         return Format::R16_FLOAT;
            else if (brkdn._componentCount == 2)    return Format::R16G16_FLOAT;
            else                                    return Format::R16G16B16A16_FLOAT;
        }

        // If no conversion is necessary, try to retain the previous format
        return source.GetFormat();
    }

    NativeVBLayout BuildDefaultLayout(MeshDatabase& mesh, const NativeVBSettings& settings)
    {
        unsigned accumulatingOffset = 0;
        unsigned largestRequiredAlignment = 1;

        NativeVBLayout result;
        result._elements.resize(mesh.GetStreams().size());

        unsigned c=0;
        for (const auto&stream : mesh.GetStreams()) {
            auto& nativeElement = result._elements[c++];
            nativeElement._semanticName         = stream.GetSemanticName();
            nativeElement._semanticIndex        = stream.GetSemanticIndex();

                // Note --  There's a problem here with texture coordinates. Sometimes texture coordinates
                //          have 3 components in the Collada file. But only 2 components are actually used
                //          by mapping. The last component might just be redundant. The only way to know 
                //          for sure that the final component is redundant is to look at where the geometry
                //          is used, and how this vertex element is bound to materials. But in this function
                //          call we only have access to the "Geometry" object, without any context information.
                //          We don't yet know how it will be bound to materials.
            nativeElement._nativeFormat         = CalculateFinalVBFormat(*stream.GetSourceData(), settings);
            nativeElement._inputSlot            = 0;

            auto alignment = VertexAttributeRequiredAlignment(nativeElement._nativeFormat);
            if ((accumulatingOffset%alignment) != 0) {
                accumulatingOffset += alignment-(accumulatingOffset%alignment);
                Log(Warning) << "Adding spacer in vertex buffer due to attribute alignment rules" << std::endl;
            }
            largestRequiredAlignment = std::max(largestRequiredAlignment, alignment);
            nativeElement._alignedByteOffset    = accumulatingOffset;
            nativeElement._inputSlotClass       = InputDataRate::PerVertex;
            nativeElement._instanceDataStepRate = 0;

            accumulatingOffset += BitsPerPixel(nativeElement._nativeFormat)/8;
        }

        if ((accumulatingOffset%largestRequiredAlignment) != 0) {
            accumulatingOffset += largestRequiredAlignment-(accumulatingOffset%largestRequiredAlignment);
            Log(Warning) << "Adding spacer in vertex buffer due to attribute alignment rules" << std::endl;
        }
        result._vertexStride = accumulatingOffset;

        return result;
    }

///////////////////////////////////////////////////////////////////////////////////////////////////

    class RawVertexSourceDataAdapter : public IVertexSourceData
    {
    public:
        IteratorRange<const void*> GetData() const      { return MakeIteratorRange(_rawData); }
        size_t GetStride() const                        { return _stride; }
        size_t GetCount() const                         { return _count; }

        RenderCore::Format			GetFormat() const     { return _fmt; }
        ProcessingFlags::BitField   GetProcessingFlags() const      { return _processingFlags; }
        FormatHint::BitField        GetFormatHint() const           { return _formatHint; }

        RawVertexSourceDataAdapter()    { _fmt = Format::Unknown; _count = _stride = 0; _processingFlags = 0; _formatHint = 0; }
        RawVertexSourceDataAdapter(
            const void* start, const void* end, 
            size_t count, size_t stride,
            Format fmt, ProcessingFlags::BitField processingFlags, FormatHint::BitField formatHint)
        : _fmt(fmt), _rawData((const uint8*)start, (const uint8*)end), _count(count), _stride(stride), _processingFlags(processingFlags), _formatHint(formatHint) {}

        RawVertexSourceDataAdapter(
            std::vector<uint8>&& rawData, 
            size_t count, size_t stride,
            Format fmt, ProcessingFlags::BitField processingFlags, FormatHint::BitField formatHint)
        : _rawData(std::move(rawData)), _fmt(fmt), _count(count), _stride(stride), _processingFlags(processingFlags), _formatHint(formatHint) {}

    protected:
        std::vector<uint8>  _rawData;
        Format				_fmt;
        size_t              _count, _stride;
        ProcessingFlags::BitField _processingFlags;
        FormatHint::BitField _formatHint;
    };

    IVertexSourceData::~IVertexSourceData() {}

    std::shared_ptr<IVertexSourceData>
        CreateRawDataSource(
            const void* dataBegin, const void* dataEnd, 
            size_t count, size_t stride,
            Format srcFormat)
    {
        return std::make_shared<RawVertexSourceDataAdapter>(dataBegin, dataEnd, count, stride, srcFormat, 0, 0);
    }

    std::shared_ptr<IVertexSourceData>
        CreateRawDataSource(
            const void* dataBegin, const void* dataEnd, 
            Format srcFormat)
    {
        auto stride = RenderCore::BitsPerPixel(srcFormat) / 8;
        auto count = (size_t(dataEnd) - size_t(dataBegin)) / stride;
        return CreateRawDataSource(dataBegin, dataEnd, count, stride, srcFormat);
    }

    std::shared_ptr<IVertexSourceData>
        CreateRawDataSource(
            std::vector<uint8>&& data, 
            size_t count, size_t stride,
            Format srcFormat)
    {
        return std::make_shared<RawVertexSourceDataAdapter>(std::move(data), count, stride, srcFormat, 0, 0);
    }

///////////////////////////////////////////////////////////////////////////////////////////////////

    struct QuantizedBlockId { Int4 _blockCoords; uint64_t _uberBlockId; };

    static std::vector<std::pair<QuantizedBlockId, unsigned>> BuildQuantizedCoords(
        const IVertexSourceData& sourceStream,
        Float4 quantization, Float4 offset,
        bool ignoreWComponent = false)
    {
        std::vector<std::pair<QuantizedBlockId, unsigned>> result;
        result.resize(sourceStream.GetCount());

        auto stride = sourceStream.GetStride();
        auto fmtBrkdn = BreakdownFormat(sourceStream.GetFormat());

        std::vector<Float4> extractedPositions;
        extractedPositions.resize(sourceStream.GetCount());
        CopyVertexData(
            extractedPositions.data(), Format::R32G32B32A32_FLOAT, sizeof(Float4), extractedPositions.size()*sizeof(Float4),
            sourceStream.GetData().begin(), sourceStream.GetFormat(), sourceStream.GetStride(), sourceStream.GetData().size(),
            sourceStream.GetCount());

        Double4 doubleOffset = offset, doubleQuant = quantization;

        for (unsigned c=0; c<extractedPositions.size(); ++c) {

            // note that if we're using very small values for quantization,
            // or if the source data is very large numbers, we could run into
            // integer precision problems here.

            Double4 pos = extractedPositions[c];
            int64_t A = int64_t((pos[0] + doubleOffset[0]) / doubleQuant[0]);
            int64_t B = int64_t((pos[1] + doubleOffset[1]) / doubleQuant[1]);
            int64_t C = int64_t((pos[2] + doubleOffset[2]) / doubleQuant[2]);
            int64_t D = int64_t((pos[3] + doubleOffset[3]) / doubleQuant[3]);

            assert((A>>32ll) >= std::numeric_limits<int16_t>::min() && (A>>32ll) <= std::numeric_limits<int16_t>::max());
            assert((B>>32ll) >= std::numeric_limits<int16_t>::min() && (B>>32ll) <= std::numeric_limits<int16_t>::max());
            assert((C>>32ll) >= std::numeric_limits<int16_t>::min() && (C>>32ll) <= std::numeric_limits<int16_t>::max());
            assert((D>>32ll) >= std::numeric_limits<int16_t>::min() && (D>>32ll) <= std::numeric_limits<int16_t>::max());
            int16_t uberA = A>>32ll;
            int16_t uberB = B>>32ll;
            int16_t uberC = C>>32ll;
            int16_t uberD = D>>32ll;
            uint64_t uberIdx = (uint64_t(uberD) << 48ull) | (uint64_t(uberC) << 32ull) | (uint64_t(uberB) << 16ull) | uint64_t(uberA);
            Int4 q{int(A), int(B), int(C), int(D)};
            result[c] = std::make_pair(QuantizedBlockId{q, uberIdx}, c);
        }

        if (ignoreWComponent)
            for (auto& r:result) {
                r.first._blockCoords[3] = 0;
                r.first._uberBlockId &= (1ull<<48ull)-1ull;     // clear top 16 bits
            }

        return result;
    }

    static bool operator==(const QuantizedBlockId& lhs, const QuantizedBlockId& rhs)
    {
        return lhs._blockCoords == rhs._blockCoords && lhs._uberBlockId == rhs._uberBlockId;
    }

    static bool SortQuantizedSet(
        const std::pair<QuantizedBlockId, unsigned>& lhs,
        const std::pair<QuantizedBlockId, unsigned>& rhs)
    {
        if (lhs.first._uberBlockId < rhs.first._uberBlockId) return true;
        if (lhs.first._uberBlockId > rhs.first._uberBlockId) return false;
        if (lhs.first._blockCoords[0] < rhs.first._blockCoords[0]) return true;
        if (lhs.first._blockCoords[0] > rhs.first._blockCoords[0]) return false;
        if (lhs.first._blockCoords[1] < rhs.first._blockCoords[1]) return true;
        if (lhs.first._blockCoords[1] > rhs.first._blockCoords[1]) return false;
        if (lhs.first._blockCoords[2] < rhs.first._blockCoords[2]) return true;
        if (lhs.first._blockCoords[2] > rhs.first._blockCoords[2]) return false;
		if (lhs.first._blockCoords[3] < rhs.first._blockCoords[3]) return true;
        if (lhs.first._blockCoords[3] > rhs.first._blockCoords[3]) return false;
            // when the quantized coordinates are equal, sort by
            // vertex index.
        return lhs.second < rhs.second; 
    }
    
    static bool CompareVertexPair(
        const std::pair<unsigned, unsigned>& lhs,
        const std::pair<unsigned, unsigned>& rhs)
    {
        if (lhs.first < rhs.first) return true;
        if (lhs.first > rhs.first) return false;
        if (lhs.second < rhs.second) return true;
        return false;
    }

    static void FindVertexPairs(
        std::vector<std::pair<unsigned, unsigned>>& closeVertices,
        std::vector<std::pair<QuantizedBlockId, unsigned>> & quantizedSet,
        const IVertexSourceData& sourceStream, float threshold)
    {
        auto stride = sourceStream.GetStride();
        auto fmtBrkdn = BreakdownFormat(sourceStream.GetFormat());

        std::vector<bool> alreadyProcessedIdentical;
        const float tsq = threshold*threshold;
        for (auto c=quantizedSet.cbegin(); c!=quantizedSet.cend(); ) {
            auto c2 = c+1;
            while (c2!=quantizedSet.cend() && c2->first == c->first) ++c2;

            // Every vertex in the range [c..c2) has equal quantized coordinates
            // We can now use a brute-force test to find if they are truly "close"
            alreadyProcessedIdentical.clear();
			alreadyProcessedIdentical.resize(c2-c, false);
            float vert0[4], vert1[4];
            for (auto ct0=c; ct0<c2; ++ct0) {
				if (alreadyProcessedIdentical[ct0-c]) continue;

                GetVertData(
                    vert0, (const float*)PtrAdd(sourceStream.GetData().begin(), ct0->second * stride), 
                    fmtBrkdn, sourceStream.GetProcessingFlags());
                for (auto ct1=ct0+1; ct1<c2; ++ct1) {
                    GetVertData(
                        vert1, (const float*)PtrAdd(sourceStream.GetData().begin(), ct1->second * stride), 
                        fmtBrkdn, sourceStream.GetProcessingFlags());

                    auto off = Float4(vert1[0]-vert0[0], vert1[1]-vert0[1], vert1[2]-vert0[2], vert1[3]-vert0[3]);
                    float dstSq = MagnitudeSquared(off);

                    if (dstSq < tsq) {
                        assert(ct0->second < ct1->second); // first index should always be smaller
                        auto p = std::make_pair(ct0->second, ct1->second);
                        auto i = std::lower_bound(closeVertices.begin(), closeVertices.end(), p, CompareVertexPair);
                        if (i == closeVertices.end() || *i != p)
                            closeVertices.insert(i, p);

						// As an optimization for a bad case --
						//		if ct0 and ct1 are completely identical, we can skip 
						//		processing of ct1 completely (because the result will just be the same as ct0) 
						if (dstSq == 0.f) {
							alreadyProcessedIdentical[ct1-c] = true;
						}
                    }

                }
            }

            c = c2;
        }
    }

    static unsigned FindClosestToAverage(
        const IVertexSourceData& sourceStream,
        const unsigned* chainStart, const unsigned* chainEnd)
    {
        if (chainEnd <= chainStart) { assert(0); return ~0u; }

        auto stride = sourceStream.GetStride();
        auto fmtBrkdn = BreakdownFormat(sourceStream.GetFormat());

        float ave[4] = {0.f, 0.f, 0.f, 0.f};
        for (auto c=chainStart; c!=chainEnd; ++c) {
            float b[4];
            GetVertData(
                b, (const float*)PtrAdd(sourceStream.GetData().begin(), (*c) * stride), 
                fmtBrkdn, sourceStream.GetProcessingFlags());
            for (unsigned q=0; q<dimof(ave); ++q)
                ave[q] += b[q];
        }

        auto count = chainEnd - chainStart;
        for (unsigned q=0; q<dimof(ave); ++q)
            ave[q] /= float(count);

        float closestDifference = std::numeric_limits<float>::max();
        auto bestIndex = ~0u;
        for (auto c=chainStart; c!=chainEnd; ++c) {
            float b[4];
            GetVertData(
                b, (const float*)PtrAdd(sourceStream.GetData().begin(), (*c) * stride), 
                fmtBrkdn, sourceStream.GetProcessingFlags());
            float dstSq = 0.f;
            for (unsigned q=0; q<dimof(ave); ++q) {
                float a = b[q] - ave[q];
                dstSq += a * a;
            }
            if (dstSq < closestDifference) {
                closestDifference = dstSq;
                bestIndex = *c;
            }
        }
        return bestIndex;
    }

    std::shared_ptr<IVertexSourceData>
        RemoveDuplicates(
            std::vector<unsigned>& outputMapping,
            const IVertexSourceData& sourceStream,
            float threshold)
    {
        auto duplicateChains = FindDuplicateChains(
            outputMapping, sourceStream, threshold);

            // We want to convert our pairs into chains of interacting vertices
            // Each chain will get merged into a single vertex.
            // While doing this, we will create a new IVertexSourceData
            // We want to try to keep the ordering in this new source data to be
            // similar to the old ordering.
        const auto vertexSize = BitsPerPixel(sourceStream.GetFormat()) / 8;
        std::vector<uint8> finalVB;
        finalVB.reserve(vertexSize * sourceStream.GetCount());
        size_t finalVBCount = 0;
        auto srcStreamStride = sourceStream.GetStride();

        const unsigned highBit = 1u<<31u;
		auto i = duplicateChains.begin();
        while (i != duplicateChains.end()) {
			auto start = i;
			++i;
			while (i != duplicateChains.end() && ((*i) & highBit) == 0) ++i;

			*start &= ~highBit;
            if ((i - start) > 1) {
                    // all vertices in this chain will be replaced with the vertex that is the closest to the
                    // average of them all
                auto m = FindClosestToAverage(sourceStream, AsPointer(start), AsPointer(i));
                const auto* sourceVertex = PtrAdd(sourceStream.GetData().begin(), m * srcStreamStride);
                finalVB.insert(finalVB.end(), (const uint8*)sourceVertex, (const uint8*)PtrAdd(sourceVertex, vertexSize));
            } else {
                    // This vertex is not part of a chain.
                    // Just append to the finalVB
                const auto* sourceVertex = PtrAdd(sourceStream.GetData().begin(), (*start) * srcStreamStride);
                finalVB.insert(finalVB.end(), (const uint8*)sourceVertex, (const uint8*)PtrAdd(sourceVertex, vertexSize));
            }
        }

            // finally, return the source data adapter
        return std::make_shared<RawVertexSourceDataAdapter>(
            std::move(finalVB), finalVBCount, vertexSize,
            sourceStream.GetFormat(), sourceStream.GetProcessingFlags(), sourceStream.GetFormatHint());
    }

    std::vector<unsigned> FindDuplicateChains(
        std::vector<unsigned>& oldOrderingToNewOrdering,
        const IVertexSourceData& sourceStream,
        float threshold)
    {
        // Using the same method as RemoveDuplicates, find chains of vertices that equivalent within the
        // given threshold
        // The result will have vertex indices, with the first index for each chain marked with bit 31 set
        std::vector<unsigned> result;
        result.reserve(sourceStream.GetCount());

            // We need to find vertices that are close together...
            // The easiest way to do this is to quantize space into grids of size 2 * threshold.
            // 2 vertices that have the same quantized position may be "close".
            // We do this twice -- once with a offset of half the grid size.
            // We will keep a record of all vertices that are found to be "close". Afterwards,
            // we should combine these pairs into chains of vertices. These chains get combined
            // into a single vertex, which is the one that is closest to the averaged vertex.
        auto quant = Float4(2.f*threshold, 2.f*threshold, 2.f*threshold, 2.f*threshold);
        auto quantizedSet0 = BuildQuantizedCoords(sourceStream, quant, Zero<Float4>());
        auto quantizedSet1 = BuildQuantizedCoords(sourceStream, quant, Float4(threshold, threshold, threshold, threshold));

            // sort our quantized vertices to make it easier to find duplicates
            // note that duplicates will be sorted with the lowest vertex index first,
            // which is important when building the pairs.
        std::sort(quantizedSet0.begin(), quantizedSet0.end(), SortQuantizedSet);
        std::sort(quantizedSet1.begin(), quantizedSet1.end(), SortQuantizedSet);
        
            // Find the pairs of close vertices
            // Note that in these pairs, the first index will always be smaller 
            // than the second index.
        std::vector<std::pair<unsigned, unsigned>> closeVertices;
        FindVertexPairs(closeVertices, quantizedSet0, sourceStream, threshold);
        FindVertexPairs(closeVertices, quantizedSet1, sourceStream, threshold);

        std::vector<std::pair<unsigned, unsigned>> reversedCloseVertices;
        reversedCloseVertices.reserve(closeVertices.size());
        for (auto c:closeVertices) reversedCloseVertices.emplace_back(c.second, c.first);
        std::sort(reversedCloseVertices.begin(), reversedCloseVertices.end(), CompareFirst2{});

        oldOrderingToNewOrdering.clear();
        oldOrderingToNewOrdering.resize(sourceStream.GetCount(), ~0u);
        size_t finalVBCount = 0;

        std::vector<unsigned> chainBuffer;
        std::vector<unsigned> pendingChainEnds;
        chainBuffer.reserve(32);
        const unsigned highBit = 1u<<31u;

        for (unsigned c=0; c<sourceStream.GetCount(); c++) {
            if (oldOrderingToNewOrdering[c] != ~0u) continue;

            chainBuffer.clear();    // clear without deallocate
            pendingChainEnds.clear();
			
			pendingChainEnds.push_back(c);
            while (!pendingChainEnds.empty()) {
				auto chainEnd = pendingChainEnds.back();
				pendingChainEnds.pop_back();

				if (std::find(chainBuffer.begin(), chainBuffer.end(), chainEnd) != chainBuffer.end())
					continue;
				assert(oldOrderingToNewOrdering[chainEnd] == ~0u);
				chainBuffer.push_back(chainEnd);

                // lookup links (both going from small index to large index, and large index to small index)
                auto linkRange = EqualRange(closeVertices, chainEnd);
				for (auto i2 = linkRange.first; i2 != linkRange.second; ++i2)
					pendingChainEnds.push_back(i2->second);

                linkRange = EqualRange(reversedCloseVertices, chainEnd);
				for (auto i2 = linkRange.first; i2 != linkRange.second; ++i2)
					pendingChainEnds.push_back(i2->second);
            }

            assert(!chainBuffer.empty());
            result.push_back(chainBuffer.front() | highBit);
            result.insert(result.end(), chainBuffer.begin()+1, chainBuffer.end());

            // figure out the reordering now; we do this because we need to track which vertices have been processed, anyway
            for (auto q=chainBuffer.cbegin(); q!=chainBuffer.cend(); ++q)
                oldOrderingToNewOrdering[*q] = (unsigned)finalVBCount;
            ++finalVBCount;
        }

        assert(result.size() == sourceStream.GetCount());
        return result;
    }

    std::shared_ptr<IVertexSourceData>
        RemoveBitwiseIdenticals(
            std::vector<unsigned>& outputMapping,
            const IVertexSourceData& sourceStream)
    {
        outputMapping.clear();
        outputMapping.resize(sourceStream.GetCount(), ~0u);

        const auto vertexSize = BitsPerPixel(sourceStream.GetFormat()) / 8;
        std::vector<uint8_t> finalVB;
        finalVB.reserve(vertexSize * sourceStream.GetCount());
        unsigned finalVBCount = 0;

        auto srcStreamStart = sourceStream.GetData().begin();
        auto srcStreamCount = sourceStream.GetCount();
        auto srcStreamStride = sourceStream.GetStride();
        if (!srcStreamCount) return nullptr;

        auto quant = Float4(1e-5f, 1e-5f, 1e-5f, 1e-5f);
        auto quantizedSet0 = BuildQuantizedCoords(sourceStream, quant, Zero<Float4>());
        std::sort(quantizedSet0.begin(), quantizedSet0.end(), SortQuantizedSet);

        auto q = quantizedSet0.begin();
        while (q != quantizedSet0.end()) {
            auto q2 = q+1;
            while (q2 != quantizedSet0.end() && q2->first == q->first) ++q2;

            for (auto c=q; c!=q2; ++c) {
                if (outputMapping[c->second] != ~0u) continue;

                auto vFirst = PtrAdd(srcStreamStart, c->second*srcStreamStride);
                for (auto c2=c+1; c2!=q2; c2++)
                    if (std::memcmp(vFirst, PtrAdd(srcStreamStart, c2->second*srcStreamStride), vertexSize) == 0)
                        outputMapping[c2->second] = finalVBCount;

                finalVB.insert(finalVB.end(), (const uint8_t*)vFirst, (const uint8_t*)PtrAdd(vFirst, vertexSize));
                outputMapping[c->second] = finalVBCount;
                ++finalVBCount;
            }
            
            q = q2;
        }

        finalVB.shrink_to_fit();

            // finally, return the source data adapter
        return std::make_shared<RawVertexSourceDataAdapter>(
            std::move(finalVB), finalVBCount, vertexSize,
            sourceStream.GetFormat(), sourceStream.GetProcessingFlags(), sourceStream.GetFormatHint());
    }

    std::vector<unsigned> MapToBitwiseIdenticals(
        const IVertexSourceData& sourceStream,
        IteratorRange<const unsigned*> originalMapping,
        bool ignoreWComponent)
    {
        std::vector<unsigned> oldOrderingToNewOrdering(sourceStream.GetCount(), ~0u);

        auto srcStreamStart = sourceStream.GetData().begin();
        auto srcStreamCount = sourceStream.GetCount();
        if (!srcStreamCount) return {};

        auto quant = Float4(1e-5f, 1e-5f, 1e-5f, 1e-5f);
        auto quantizedSet0 = BuildQuantizedCoords(sourceStream, quant, Zero<Float4>(), ignoreWComponent);
        std::sort(quantizedSet0.begin(), quantizedSet0.end(), SortQuantizedSet);

        const auto fmtBrkdn = BreakdownFormat(sourceStream.GetFormat());
        const auto stride = sourceStream.GetStride();
        auto vertexSize = BitsPerPixel(sourceStream.GetFormat()) / 8;

        if (ignoreWComponent) {
            auto typelessFormat = AsTypelessFormat(sourceStream.GetFormat());
            if (typelessFormat == Format::R32G32B32A32_TYPELESS) vertexSize = sizeof(float)*3;
            else if (typelessFormat == Format::R16G16B16A16_TYPELESS) vertexSize = sizeof(uint16_t)*3;
            else if (typelessFormat == Format::R8G8B8A8_TYPELESS) vertexSize = sizeof(uint8_t)*3;
            else assert(GetComponents(typelessFormat) != FormatComponents::RGBAlpha);
        }

        auto q = quantizedSet0.begin();
        while (q != quantizedSet0.end()) {
            auto q2 = q+1;
            while (q2 != quantizedSet0.end() && q2->first == q->first) ++q2;

            for (auto c=q; c!=q2; ++c) {
                if (oldOrderingToNewOrdering[c->second] != ~0u) continue;
                oldOrderingToNewOrdering[c->second] = c->second;

                auto vFirst = PtrAdd(srcStreamStart, c->second*stride);
                for (auto c2=c+1; c2!=q2; c2++) {
                    if (std::memcmp(vFirst, PtrAdd(srcStreamStart, c2->second*stride), vertexSize) == 0)
                        oldOrderingToNewOrdering[c2->second] = c->second;
                }
            }
            
            q = q2;
        }

        if (originalMapping.empty())
            return oldOrderingToNewOrdering;

        // have to transform "originalMapping" via this new mapping
        std::vector<unsigned> outputMapping;
        outputMapping.reserve(originalMapping.size());
        std::transform(
            originalMapping.begin(), originalMapping.end(),
            std::back_inserter(outputMapping),
            [&oldOrderingToNewOrdering](const unsigned i) { return oldOrderingToNewOrdering[i]; });
        return outputMapping;
    }

///////////////////////////////////////////////////////////////////////////////////////////////////

	MeshDatabase RemoveDuplicates(
		std::vector<unsigned>& outputMapping,
		const MeshDatabase& input) 
	{
		// Note -- assuming that the vertex streams in "input" have already had RemoveDuplicates() 
		// called to ensure that duplicate vertex values have been combined into one.
		// Given that this is the case, we only need to check for cases where the vertex mapping
		// values are identical in the vertex stream mapping

		outputMapping.clear();
		class RemappedStream
		{
		public:
			std::vector<unsigned> _unifiedToStreamElement;
		};
		std::vector<RemappedStream> workingMapping;
		auto inputStreams = input.GetStreams();
		workingMapping.resize(inputStreams.size());

		unsigned finalUnifiedVertexCount = 0u;
		for (unsigned v = 0; v < input.GetUnifiedVertexCount(); ++v) {
			// look for an existing vertex that is identical
			unsigned existingVertex = ~0u;
			for (unsigned c = 0; c < finalUnifiedVertexCount; ++c) {
				bool isIdentical = true;
				for (unsigned s = 0; s < inputStreams.size(); ++s) {
					auto mappedIndex = !inputStreams[s].GetVertexMap().empty() ? inputStreams[s].GetVertexMap()[v] : v;
					if (workingMapping[s]._unifiedToStreamElement[c] != mappedIndex) {
						isIdentical = false;
						break;
					}
				}
				if (isIdentical) {
					existingVertex = c;
					break;
				}
			}

			if (existingVertex != ~0u) {
				outputMapping.push_back(existingVertex);
			} else {
				// if we got this far, there's no existing identical vertex
				for (unsigned s = 0; s < inputStreams.size(); ++s)
					workingMapping[s]._unifiedToStreamElement.push_back(!inputStreams[s].GetVertexMap().empty() ? inputStreams[s].GetVertexMap()[v] : v);
				outputMapping.push_back(finalUnifiedVertexCount);
				++finalUnifiedVertexCount;
			}
		}

		MeshDatabase result;
		for (unsigned s = 0; s < inputStreams.size(); ++s) {
			result.AddStream(
				inputStreams[s].GetSourceData(),
				std::move(workingMapping[s]._unifiedToStreamElement),
				inputStreams[s].GetSemanticName().c_str(),
				inputStreams[s].GetSemanticIndex());
		}

		assert(result.GetUnifiedVertexCount() == finalUnifiedVertexCount);
		return result;
	}

///////////////////////////////////////////////////////////////////////////////////////////////////

    size_t CreateTriangleWindingFromPolygon(unsigned buffer[], size_t bufferCount, size_t polygonVertexCount)
    {
            //
            //      Assuming simple convex polygon
            //      (nothing fancy required to convert to triangle list)
            //
        size_t outputIterator = 0;
        for (unsigned triangleCount = 0; triangleCount < polygonVertexCount - 2; ++triangleCount) {
                ////////        ////////
            unsigned v0, v1, v2;
            v0 = (triangleCount+1) / 2;
            if (triangleCount&0x1) {
                v1 = unsigned(polygonVertexCount - 2 - triangleCount/2);
            } else {
                v1 = unsigned(v0 + 1);
            }
            v2 = unsigned(polygonVertexCount - 1 - triangleCount/2);
                ////////        ////////
            assert((outputIterator+3) <= bufferCount);
            buffer[outputIterator++] = v0;
            buffer[outputIterator++] = v1;
            buffer[outputIterator++] = v2;
                ////////        ////////
        }
        return outputIterator/3;
    }

    IteratorRange<VertexElementIterator> MakeVertexIteratorRange(const IVertexSourceData& srcData)
    {
        return RenderCore::MakeVertexIteratorRangeConst(
            srcData.GetData(), srcData.GetStride(), srcData.GetFormat());
    }

	std::vector<unsigned> CompressIndexBuffer(IteratorRange<unsigned*> indexBufferInAndOut)
	{
		std::vector<unsigned> mapping { indexBufferInAndOut.begin(), indexBufferInAndOut.end() };
		std::sort(mapping.begin(), mapping.end());
		auto i = std::unique(mapping.begin(), mapping.end());
		mapping.erase(i, mapping.end());
		if (mapping.empty())
			return {};

		std::vector<unsigned> reverseMapping(*(mapping.end()-1)+1, ~0u);
		for (unsigned c=0; c<mapping.size(); ++c)
			reverseMapping[mapping[c]] = c;

		for (auto& idx:indexBufferInAndOut) {
			assert(reverseMapping[idx] != ~0u);
			idx = reverseMapping[idx];
		}

		return mapping;
	}

}}}

