// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "Format.h"
#include "../../Foreign/half-1.9.2/include/half.hpp"
#include "../Math/Vector.h"
#include "../Utility/IteratorUtils.h"
#include <utility>
#include <vector>
#include <assert.h>

namespace RenderCore
{
	class VertexElementIterator
	{
	public:
		class ConstValue
		{
		public:
			template<typename Type> const Type& As(); 
			IteratorRange<void*> _data;
			RenderCore::Format _format = RenderCore::Format(0);
		};

		class Value : public ConstValue
		{
		public:
			template<typename Type> void operator=(const Type& input);
			Value() {}
			Value(IteratorRange<void*> data, RenderCore::Format format) : ConstValue{data, format} {}
		};

		bool operator==(const VertexElementIterator&);
		bool operator!=(const VertexElementIterator&);
		void operator++();
		friend size_t operator-(const VertexElementIterator& lhs, const VertexElementIterator& rhs);
		friend bool operator<(const VertexElementIterator& lhs, const VertexElementIterator& rhs);
		friend VertexElementIterator operator+(const VertexElementIterator& lhs, ptrdiff_t advance);

		ConstValue operator*() const;
		Value operator*();
		ConstValue operator->() const;
		Value operator->();
		ConstValue operator[](size_t idx) const;
		Value operator[](size_t idx);

		RenderCore::Format Format() const { return _format; }

		IteratorRange<void*> _data;
		size_t _stride = 0;
		RenderCore::Format _format = RenderCore::Format(0);

		VertexElementIterator() {}
		VertexElementIterator(std::nullptr_t) {}
		VertexElementIterator(IteratorRange<void*> data, size_t stride, RenderCore::Format fmt) : _data(data), _stride(stride), _format(fmt) {}

		using difference_type = size_t;
		using value_type = Value;
		using pointer = Value*;
		using reference = Value&;
		using iterator_category = std::random_access_iterator_tag;
	};

	inline bool VertexElementIterator::operator==(const VertexElementIterator& other)
	{
		return _data.begin() == other._data.begin();
	}

	inline bool VertexElementIterator::operator!=(const VertexElementIterator& other)
	{
		return _data.begin() != other._data.begin();
	}

	inline void VertexElementIterator::operator++()
	{
		_data.first = PtrAdd(_data.first, _stride);
	}

	inline VertexElementIterator operator+(const VertexElementIterator& lhs, ptrdiff_t advance)
	{
		VertexElementIterator result = lhs;
		result._data.first = PtrAdd(result._data.first, result._stride * advance);
		return result;
	}

	inline auto VertexElementIterator::operator*() const -> ConstValue
	{ 
		return ConstValue { 
			IteratorRange<void*>(_data.begin(), PtrAdd(_data.begin(), std::min(_data.size(), _stride))), 
			_format}; 
	}

	inline auto VertexElementIterator::operator*() -> Value
	{ 
		return Value { 
			IteratorRange<void*>(_data.begin(), PtrAdd(_data.begin(), std::min(_data.size(), _stride))), 
			_format}; 
	}

	inline auto VertexElementIterator::operator->() const -> ConstValue
	{ 
		return ConstValue { 
			IteratorRange<void*>(_data.begin(), PtrAdd(_data.begin(), std::min(_data.size(), _stride))), 
			_format}; 
	}

	inline auto VertexElementIterator::operator->() -> Value
	{ 
		return Value { 
			IteratorRange<void*>(_data.begin(), PtrAdd(_data.begin(), std::min(_data.size(), _stride))), 
			_format}; 
	}

	inline auto VertexElementIterator::operator[](size_t idx) const -> ConstValue
	{
		return ConstValue { 
			IteratorRange<void*>(
				PtrAdd(_data.begin(), std::min(_data.size(), idx*_stride)),
				PtrAdd(_data.begin(), std::min(_data.size(), (idx+1)*_stride))), 
			_format}; 
	}

	inline auto VertexElementIterator::operator[](size_t idx) -> Value
	{
		return Value { 
			IteratorRange<void*>(
				PtrAdd(_data.begin(), std::min(_data.size(), idx*_stride)),
				PtrAdd(_data.begin(), std::min(_data.size(), (idx+1)*_stride))), 
			_format}; 
	}

	inline size_t operator-(const VertexElementIterator& lhs, const VertexElementIterator& rhs)
	{
		assert(lhs._stride == rhs._stride && lhs._format == rhs._format);
		auto byteDifference = (const uint8_t*)lhs._data.begin() - (const uint8_t*)rhs._data.begin();
		auto leftOver = byteDifference % lhs._stride;
		auto res = byteDifference / lhs._stride;
		if (leftOver > 0) {
			// The end pointer may not hit exactly the vertex stride mark; but when it doesn't, there
			// should be enough for one extra element. For example, if there is padding between vertices
			// that padding may not exist after the last vertex.
			assert(leftOver >= BitsPerPixel(lhs._format)/8); 
			++res;
		}
		return res;
	}

	inline bool operator<(const VertexElementIterator& lhs, const VertexElementIterator& rhs)
	{
		return lhs._data.begin() < rhs._data.begin();
	}

	template<typename Type> 
		const Type& VertexElementIterator::ConstValue::As()
	{
		assert(_data.size() >= sizeof(Type));
		return *(const Type*)_data.begin();
	}

	template<typename Type> 
		void VertexElementIterator::Value::operator=(const Type& input)
	{
		assert(_data.size() >= sizeof(Type));
		*(Type*)_data.begin() = input;
	}

///////////////////////////////////////////////////////////////////////////////////////////////////

	inline IteratorRange<VertexElementIterator> MakeVertexIteratorRange(IteratorRange<void*> data, size_t stride, RenderCore::Format fmt)
	{
		return {
			VertexElementIterator{data, stride, fmt},
			VertexElementIterator{{data.end(), data.end()}, stride, fmt}};
	}

	inline IteratorRange<VertexElementIterator> MakeVertexIteratorRangeConst(IteratorRange<const void*> data, size_t stride, RenderCore::Format fmt)
	{
		auto castedData = MakeIteratorRange(const_cast<void*>(data.begin()), const_cast<void*>(data.end()));
		return {
			VertexElementIterator{castedData, stride, fmt},
			VertexElementIterator{{castedData.end(), castedData.end()}, stride, fmt}};
	}

///////////////////////////////////////////////////////////////////////////////////////////////////

	enum class VertexUtilComponentType { Float32, Float16, UNorm8, UNorm16, SNorm8, SNorm16, UInt8, UInt16, UInt32, SInt8, SInt16, SInt32 };
    struct BrokenDownFormat { VertexUtilComponentType _type; unsigned _componentCount; };
	BrokenDownFormat BreakdownFormat(Format fmt);
    
	inline unsigned short AsFloat16(float input)
    {
        //
        //      Using "half" library
        //          http://sourceforge.net/projects/half/
        //
        //      It doesn't have vectorized conversions,
        //      and it looks like it doesn't support denormalized
        //      or overflowed numbers. But it has lots of rounding
        //      modes!
        //

        auto result = half_float::detail::float2half<std::round_to_nearest>(input);
        // assert(!std::isinf(half_float::detail::half2float(result)));
        return result;
    }

    inline float AsFloat32(unsigned short input)
    {
        return half_float::detail::half2float(input);
    }

	inline BrokenDownFormat BreakdownFormat(Format fmt)
    {
        if (fmt == Format::Unknown) return {VertexUtilComponentType::Float32, 0};

        auto componentType = VertexUtilComponentType::Float32;
        unsigned componentCount = GetComponentCount(GetComponents(fmt));

        auto type = GetComponentType(fmt);
        unsigned prec = GetComponentPrecision(fmt);

        switch (type) {
        case FormatComponentType::Float:
            assert(prec == 16 || prec == 32);
            componentType = (prec > 16) ? VertexUtilComponentType::Float32 : VertexUtilComponentType::Float16; 
            break;

        case FormatComponentType::UnsignedFloat16:
        case FormatComponentType::SignedFloat16:
            componentType = VertexUtilComponentType::Float16;
            break;

		case FormatComponentType::SNorm:
			componentType = (prec == 16) ? VertexUtilComponentType::SNorm16 : VertexUtilComponentType::SNorm8;
			break;

		case FormatComponentType::UNorm: 
        case FormatComponentType::UNorm_SRGB:
            assert(prec==8 || prec==16);
            componentType = (prec == 16) ? VertexUtilComponentType::UNorm16 : VertexUtilComponentType::UNorm8;
            break;

		case FormatComponentType::UInt:
			assert(prec==8 || prec==16 || prec==32);
			if (prec == 8) componentType = VertexUtilComponentType::UInt8;
			else if (prec == 16) componentType = VertexUtilComponentType::UInt16;
			else componentType = VertexUtilComponentType::UInt32;
			break;

		case FormatComponentType::SInt:
			assert(prec==8 || prec==16 || prec==32);
			if (prec == 8) componentType = VertexUtilComponentType::SInt8;
			else if (prec == 16) componentType = VertexUtilComponentType::SInt16;
			else componentType = VertexUtilComponentType::SInt32;
			break;

        default:
            assert(0);
        }

        return {componentType, componentCount};
    }

/////////////////////////////////////////////////////////////////////////////////////////////////////////////

	inline void GetVertDataF32(
        float* dst, 
        const float* src, unsigned srcComponentCount)
    {
            // In Collada, the default for values not set is 0.f (or 1. for components 3 or greater)
        dst[0] = (srcComponentCount > 0) ? src[0] : 0.f;
        dst[1] = (srcComponentCount > 1) ? src[1] : 0.f;
        dst[2] = (srcComponentCount > 2) ? src[2] : 0.f;
        dst[3] = (srcComponentCount > 3) ? src[3] : 1.f;
    }

    inline void GetVertDataF16(
        float* dst, 
        const uint16_t* src, unsigned srcComponentCount)
    {
            // In Collada, the default for values not set is 0.f (or 1. for components 3 or greater)
        dst[0] = (srcComponentCount > 0) ? AsFloat32(src[0]) : 0.f;
        dst[1] = (srcComponentCount > 1) ? AsFloat32(src[1]) : 0.f;
        dst[2] = (srcComponentCount > 2) ? AsFloat32(src[2]) : 0.f;
        dst[3] = (srcComponentCount > 3) ? AsFloat32(src[3]) : 1.f;
    }

	// Note the slight oddity with snorm numbers whereby there are 2 representations for -1 (the smallest integer input and the second
	// smallest). This is so 0 falls on directly on an integer. 
	// See (for example) https://docs.microsoft.com/en-us/windows/win32/direct3d10/d3d10-graphics-programming-guide-resources-data-conversion
	inline float UNorm16AsFloat32(uint16_t value)	{ return value / float(0xffff); }
	inline float SNorm16AsFloat32(int16_t value)	{ return std::max(value, int16_t(-0x7fff)) / float(0x7fff); }
	inline float UNorm8AsFloat32(uint8_t value)		{ return value / float(0xff); }
	inline float SNorm8AsFloat32(int8_t value)		{ return std::max(value, int8_t(-0x7f)) / float(0x7f); }

	inline void GetVertDataUNorm16(
		float* dst,
		const uint16_t* src, unsigned srcComponentCount)
	{
		// In Collada, the default for values not set is 0.f (or 1. for components 3 or greater)
		dst[0] = (srcComponentCount > 0) ? UNorm16AsFloat32(src[0]) : 0.f;
		dst[1] = (srcComponentCount > 1) ? UNorm16AsFloat32(src[1]) : 0.f;
		dst[2] = (srcComponentCount > 2) ? UNorm16AsFloat32(src[2]) : 0.f;
		dst[3] = (srcComponentCount > 3) ? UNorm16AsFloat32(src[3]) : 1.f;
	}

	inline void GetVertDataSNorm16(
		float* dst,
		const int16_t* src, unsigned srcComponentCount)
	{
		// In Collada, the default for values not set is 0.f (or 1. for components 3 or greater)
		dst[0] = (srcComponentCount > 0) ? SNorm16AsFloat32(src[0]) : 0.f;
		dst[1] = (srcComponentCount > 1) ? SNorm16AsFloat32(src[1]) : 0.f;
		dst[2] = (srcComponentCount > 2) ? SNorm16AsFloat32(src[2]) : 0.f;
		dst[3] = (srcComponentCount > 3) ? SNorm16AsFloat32(src[3]) : 1.f;
	}

	inline void GetVertDataUNorm8(
		float* dst,
		const uint8_t* src, unsigned srcComponentCount)
	{
		// In Collada, the default for values not set is 0.f (or 1. for components 3 or greater)
		dst[0] = (srcComponentCount > 0) ? UNorm8AsFloat32(src[0]) : 0.f;
		dst[1] = (srcComponentCount > 1) ? UNorm8AsFloat32(src[1]) : 0.f;
		dst[2] = (srcComponentCount > 2) ? UNorm8AsFloat32(src[2]) : 0.f;
		dst[3] = (srcComponentCount > 3) ? UNorm8AsFloat32(src[3]) : 1.f;
	}

	inline void GetVertDataSNorm8(
		float* dst,
		const int8_t* src, unsigned srcComponentCount)
	{
		// In Collada, the default for values not set is 0.f (or 1. for components 3 or greater)
		dst[0] = (srcComponentCount > 0) ? SNorm8AsFloat32(src[0]) : 0.f;
		dst[1] = (srcComponentCount > 1) ? SNorm8AsFloat32(src[1]) : 0.f;
		dst[2] = (srcComponentCount > 2) ? SNorm8AsFloat32(src[2]) : 0.f;
		dst[3] = (srcComponentCount > 3) ? SNorm8AsFloat32(src[3]) : 1.f;
	}

	inline void GetVertDataUInt8(
		unsigned* dst,
		const uint8_t* src, unsigned srcComponentCount)
	{
		dst[0] = (srcComponentCount > 0) ? src[0] : 0;
		dst[1] = (srcComponentCount > 1) ? src[1] : 0;
		dst[2] = (srcComponentCount > 2) ? src[2] : 0;
		dst[3] = (srcComponentCount > 3) ? src[3] : 0;
	}

	inline void GetVertDataUInt16(
		unsigned* dst,
		const uint16_t* src, unsigned srcComponentCount)
	{
		dst[0] = (srcComponentCount > 0) ? src[0] : 0;
		dst[1] = (srcComponentCount > 1) ? src[1] : 0;
		dst[2] = (srcComponentCount > 2) ? src[2] : 0;
		dst[3] = (srcComponentCount > 3) ? src[3] : 0;
	}

	inline void GetVertDataUInt32(
		unsigned* dst,
		const uint32_t* src, unsigned srcComponentCount)
	{
		dst[0] = (srcComponentCount > 0) ? src[0] : 0;
		dst[1] = (srcComponentCount > 1) ? src[1] : 0;
		dst[2] = (srcComponentCount > 2) ? src[2] : 0;
		dst[3] = (srcComponentCount > 3) ? src[3] : 0;
	}

	// static unsigned short AsFloat16_Fast(float input)
    // {
    //         //
    //         //      See stack overflow article:
    //         //          http://stackoverflow.com/questions/3026441/float32-to-float16
    //         //
    //         //      He suggests either using a table lookup or vectorising
    //         //      this code for further optimisation.
    //         //
    //     unsigned int fltInt32 = FloatBits(input);
    // 
    //     unsigned short fltInt16 = (fltInt32 >> 31) << 5;
    // 
    //     unsigned short tmp = (fltInt32 >> 23) & 0xff;
    //     tmp = (tmp - 0x70) & ((unsigned int)((int)(0x70 - tmp) >> 4) >> 27);
    // 
    //     fltInt16 = (fltInt16 | tmp) << 10;
    //     fltInt16 |= (fltInt32 >> 13) & 0x3ff;
    // 
    //     return fltInt16;
    // }

	inline std::vector<Float3> AsFloat3s(IteratorRange<VertexElementIterator> input)
	{
		std::vector<Float3> result(input.size());
		auto output = result.begin();

		auto fmtBreakdown = BreakdownFormat(input.begin()._format);
		switch (fmtBreakdown._type) {
		case VertexUtilComponentType::Float32:
			for (auto p=input.begin(); p<input.end(); ++p, ++output) {
				Float4 value{0.f, 0.f, 0.f, 1.0};
				GetVertDataF32(value.data(), &(*p).As<float>(), std::min(fmtBreakdown._componentCount, 3u));
				*output = Truncate(value);
			}
			break;
		case VertexUtilComponentType::Float16:
			for (auto p=input.begin(); p<input.end(); ++p, ++output) {
				Float4 value{0.f, 0.f, 0.f, 1.0};
				GetVertDataF16(value.data(), &(*p).As<uint16_t>(), std::min(fmtBreakdown._componentCount, 3u));
				*output = Truncate(value);
			}
			break;
		case VertexUtilComponentType::UNorm16:
			for (auto p=input.begin(); p<input.end(); ++p, ++output) {
				Float4 value{0.f, 0.f, 0.f, 1.0};
				GetVertDataUNorm16(value.data(), &(*p).As<uint16_t>(), std::min(fmtBreakdown._componentCount, 3u));
				*output = Truncate(value);
			}
			break;
		case VertexUtilComponentType::SNorm16:
			for (auto p=input.begin(); p<input.end(); ++p, ++output) {
				Float4 value{0.f, 0.f, 0.f, 1.0};
				GetVertDataSNorm16(value.data(), &(*p).As<int16_t>(), std::min(fmtBreakdown._componentCount, 3u));
				*output = Truncate(value);
			}
			break;
		default:
			assert(0);
			break;
		}

		return result;
	}

	inline std::vector<Float4> AsFloat4s(IteratorRange<VertexElementIterator> input)
	{
		std::vector<Float4> result(input.size());
		auto output = result.begin();

		auto fmtBreakdown = BreakdownFormat(input.begin()._format);
		switch (fmtBreakdown._type) {
		case VertexUtilComponentType::Float32:
			for (auto p=input.begin(); p<input.end(); ++p, ++output)
				GetVertDataF32(output->data(), &(*p).As<float>(), fmtBreakdown._componentCount);
			break;
		case VertexUtilComponentType::Float16:
			for (auto p=input.begin(); p<input.end(); ++p, ++output)
				GetVertDataF16(output->data(), &(*p).As<uint16_t>(), fmtBreakdown._componentCount);
			break;
		case VertexUtilComponentType::UNorm16:
			for (auto p=input.begin(); p<input.end(); ++p, ++output)
				GetVertDataUNorm16(output->data(), &(*p).As<uint16_t>(), fmtBreakdown._componentCount);
			break;
		case VertexUtilComponentType::SNorm16:
			for (auto p=input.begin(); p<input.end(); ++p, ++output)
				GetVertDataSNorm16(output->data(), &(*p).As<int16_t>(), fmtBreakdown._componentCount);
			break;
		case VertexUtilComponentType::UNorm8:
			for (auto p=input.begin(); p<input.end(); ++p, ++output)
				GetVertDataUNorm8(output->data(), &(*p).As<uint8_t>(), fmtBreakdown._componentCount);
			break;
		case VertexUtilComponentType::SNorm8:
			for (auto p=input.begin(); p<input.end(); ++p, ++output)
				GetVertDataSNorm8(output->data(), &(*p).As<int8_t>(), fmtBreakdown._componentCount);
			break;
		default:
			assert(0);
			break;
		}

		return result;
	}

	inline std::vector<UInt4> AsUInt4s(IteratorRange<VertexElementIterator> input)
	{
		std::vector<UInt4> result(input.size());
		auto output = result.begin();

		auto fmtBreakdown = BreakdownFormat(input.begin()._format);
		switch (fmtBreakdown._type) {
		case VertexUtilComponentType::UInt8:
			for (auto p=input.begin(); p<input.end(); ++p, ++output)
				GetVertDataUInt8(output->data(), &(*p).As<uint8_t>(), fmtBreakdown._componentCount);
			break;
		case VertexUtilComponentType::UInt16:
			for (auto p=input.begin(); p<input.end(); ++p, ++output)
				GetVertDataUInt16(output->data(), &(*p).As<uint16_t>(), fmtBreakdown._componentCount);
			break;
		case VertexUtilComponentType::UInt32:
			for (auto p=input.begin(); p<input.end(); ++p, ++output)
				GetVertDataUInt32(output->data(), &(*p).As<uint32_t>(), fmtBreakdown._componentCount);
			break;
		default:
			assert(0);
			break;
		}

		return result;
	}
}

namespace std
{
	inline size_t distance(const RenderCore::VertexElementIterator& begin, const RenderCore::VertexElementIterator& end) { return end - begin; }
}
