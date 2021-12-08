// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#define _SCL_SECURE_NO_WARNINGS

#include "RawAnimationCurve.h"
#include "../Format.h"
#include "../../Math/Matrix.h"
#include "../../Math/Quaternion.h"
#include "../../Math/Interpolation.h"
#include "../../Utility/IteratorUtils.h"
#include "../../Core/Exceptions.h"
#include <cmath>

namespace RenderCore { namespace Assets
{
    static float LerpParameter(float A, float B, float input) { return (input - A) / (B-A); }

	template<typename OutType>
		class CurveElementDecompressor
		{
		public:
			const OutType& operator()(unsigned idx, unsigned timeMarkerValue, unsigned componentOffset=0) const { return *(const OutType*)PtrAdd(_data.begin(), idx*_stride+componentOffset); }
			unsigned KeyCount() const { return _data.size() / _stride; }
			CurveElementDecompressor(IteratorRange<const void*> data, unsigned stride, Format fmt);
		private:
			Format _fmt;
			IteratorRange<const void*> _data;
			unsigned _stride;
		};

	template<>
		CurveElementDecompressor<float>::CurveElementDecompressor(IteratorRange<const void*> data, unsigned stride, Format fmt)
		: _data(data), _stride(stride)
	{
		assert(fmt == Format::R32_FLOAT
			|| fmt == Format::R32G32_FLOAT
			|| fmt == Format::R32G32B32_FLOAT
			|| fmt == Format::R32G32B32A32_FLOAT);
	}

	template<>
		CurveElementDecompressor<Float3>::CurveElementDecompressor(IteratorRange<const void*> data, unsigned stride, Format fmt)
		: _data(data), _stride(stride)
	{
		assert(fmt == Format::R32G32B32_FLOAT
			|| fmt == Format::R32G32B32A32_FLOAT);
	}

	template<>
		CurveElementDecompressor<Float4x4>::CurveElementDecompressor(IteratorRange<const void*> data, unsigned stride, Format fmt)
		: _data(data), _stride(stride)
	{
		assert(fmt == Format::Matrix4x4);
	}
		
	template<>
		class CurveElementDecompressor<Float4>
		{
		public:
			Float4 operator()(unsigned idx, unsigned timeMarkerValue, unsigned componentOffset=0) const
			{ 
				auto keyData = PtrAdd(_data.begin(), idx*_stride+componentOffset);
				if (_fmt == Format::R10G10B10A10_SNORM) {
					// Decompress 5 byte quaternion format
					// This is 4 10-bit signed values, in x,y,z,w form
					struct Q {
						int x : 10;
						int y : 10;
						int z : 10;
						int w : 10;
					};
					const auto& q = *(const Q*)keyData;
					// note --	min value should be -0x200, but max positive value is 0x1ff
					//			so, this calculation will never actually return +1.0f
					return Float4(q.w/float(200), q.x/float(0x200), q.y/float(0x200), q.z/float(0x200));
				} else {
					return *(const Float4*)keyData;
				}
			}

			unsigned KeyCount() const { return _data.size() / _stride; }

			CurveElementDecompressor(IteratorRange<const void*> data, unsigned stride, Format fmt)
			: _fmt(fmt), _data(data), _stride(stride)
			{
				assert(fmt == Format::R10G10B10A10_SNORM || fmt == Format::R32G32B32A32_FLOAT);
			}
		private:
			Format _fmt;
			IteratorRange<const void*> _data;
			unsigned _stride;
		};

	Quaternion Decompress_36bit(const void* data)
	{
		// Decompress quaternions stored in a 36bit 12/12/12 form.
		// The final element is implied by the fact that we want to end up with a 
		// normalized quaternion.

		class CompressedQuaternion
		{
		public:
			uint8_t a, b, c, d, e;
		};
		auto&v = *(const CompressedQuaternion*)data;

		uint32_t A = uint32_t(v.a) | (uint32_t(v.b)&0xf)<<8;
		uint32_t B = uint32_t(v.b)>>4 | uint32_t(v.c)<<4;
		uint32_t C = uint32_t(v.d) | (uint32_t(v.e)&0xf)<<8;
		float a = (int32_t(A) - 2048) / 2048.0f;	// not 100% if we're using two-complement or just wrapping around zero
		float b = (int32_t(B) - 2048) / 2048.0f;
		float c = (int32_t(C) - 2048) / 2048.0f;

		// 2047 seems to come up a lot in the data, suggesting it might be zero
		// The constant here, 2895.f, is based on comparing some of the fixed values in animation
		// files to the default parameters on skeletons. It's not clear why we're not using the full
		// range here; and the constant might not be perfectly accurate.
		a = (int32_t(A) - 2047) / 2895.f;
		b = (int32_t(B) - 2047) / 2895.f;
		c = (int32_t(C) - 2047) / 2895.f;

		float t = a*a + b*b + c*c;
		assert(t<=1.0f);
		t = std::min(t, 1.0f);
		float reconstructed = std::sqrt(1.0f - t);

		// We have one bit to represent the sign of the reconstructed element.
		// But could we not just negate the other elements so that the reconstructed
		// element is always positive? Or would that cause problems in interpolation somehow.
		if (v.e&0x40) reconstructed = -reconstructed;
		assert(!(v.e&0x80));	// unused bit?

		switch ((v.e>>4)&0x3) {
		case 0:
			return { c, reconstructed, a, b };
		case 1:
			return { c, a, reconstructed, b };
		case 2:
			return { c, a, b, reconstructed };
		case 3:
			return { reconstructed, a, b, c };
		}

		assert(0);	// compiler doesn't seem to be realize it's impossible to get here
		return {0.f, 0.f, 0.f, 0.f};
	}

	template<>
		class CurveElementDecompressor<Quaternion>
		{
		public:
			Quaternion operator()(unsigned idx, unsigned timeMarkerValue, unsigned componentOffset=0) const
			{
				auto keyData = PtrAdd(_data.begin(), idx*_stride+componentOffset);
				if (_fmt == Format::R10G10B10A10_SNORM) {
					// Decompress 5 byte quaternion format
					// This is 4 10-bit signed values, in x,y,z,w form
					struct Q {
						int x : 10;
						int y : 10;
						int z : 10;
						int w : 10;
					};
					const auto& q = *(const Q*)keyData;
					// note --	min value should be -0x200, but max positive value is 0x1ff
					//			so, this calculation will never actually return +1.0f
					// When this format is used for unnormalized quaternions (ie, we're expecting a normalize operation at some
					// point, possibly after an interpolation) then it won't matter too much -- because the magnitude is only
					// meaningful in relation to the magnitudes of other quaternions in the same form.
					return Quaternion(q.w/float(0x200), q.x/float(0x200), q.y/float(0x200), q.z/float(200));
				} else if (_fmt == Format::R12G12B12A4_SNORM) {
					return Decompress_36bit(keyData);
				} else {
					return *(const Quaternion*)keyData;		// (note -- expecting w, x, y, z order here)
				}
			}

			unsigned KeyCount() const { return _data.size() / _stride; }

			CurveElementDecompressor(IteratorRange<const void*> data, unsigned stride, Format fmt) 
			: _fmt(fmt), _data(data), _stride(stride)
			{
				assert(fmt == Format::R10G10B10A10_SNORM || fmt == Format::R32G32B32A32_FLOAT || fmt == Format::R12G12B12A4_SNORM);
			}
		private:
			Format _fmt;
			IteratorRange<const void*> _data;
			unsigned _stride;
		};

	template<typename OutType>
		class CurveElementDequantDecompressor
		{
		public:
			OutType operator()(unsigned idx, unsigned timeMarkerValue, unsigned componentOffset=0) const;
			unsigned KeyCount() const;

			CurveElementDequantDecompressor(IteratorRange<const void*> data, unsigned stride, Format fmt, unsigned blockCount)
			: _fmt(fmt), _data(data), _stride(stride), _blockCount(blockCount) { assert(_blockCount); }
		private:
			Format _fmt;
			IteratorRange<const void*> _data;
			unsigned _stride;
			unsigned _blockCount;

			std::pair<const CurveDequantizationBlock*, const void*> FindKey(unsigned idx, unsigned timeMarkerValue) const;
		};

	template <typename Type>
		unsigned CurveElementDequantDecompressor<Type>::KeyCount() const
	{
		return (_data.size()-_blockCount*sizeof(CurveDequantizationBlock))/_stride;
	}

	template <typename Type>
		std::pair<const CurveDequantizationBlock*, const void*> CurveElementDequantDecompressor<Type>::FindKey(unsigned idx, unsigned timeMarkerValue) const 
	{
		// We should find a dequantization block at the start and after every X keys
		// This will contain the reconstructed min & max, and other parameters that
		// help with dequantization.
		const unsigned framesPerDequantBlock = 256;
		unsigned dequantBlockIdx = timeMarkerValue/framesPerDequantBlock;
		assert(dequantBlockIdx < _blockCount);
		auto* dequantBlock = (const CurveDequantizationBlock*)PtrAdd(_data.begin(), dequantBlockIdx*sizeof(CurveDequantizationBlock));
		assert(PtrAdd(dequantBlock, sizeof(CurveDequantizationBlock)) <= _data.end());
		assert(dequantBlock->_mins[3] == 0.f && dequantBlock->_maxs[3] == 0.f);
		return {dequantBlock, PtrAdd(_data.begin(), _blockCount*sizeof(CurveDequantizationBlock)+idx*_stride)};
	}

	template <>
		float CurveElementDequantDecompressor<float>::operator()(unsigned idx, unsigned timeMarkerValue, unsigned componentOffset) const 
	{
		assert(_fmt == Format::R16_UNORM);
		assert(componentOffset == 0);
		const CurveDequantizationBlock* dequantBlock; const void* data;
		std::tie(dequantBlock, data) = FindKey(idx, timeMarkerValue);
		float result = dequantBlock->_mins[0];
		auto* v = (const uint16*)data;
		if (dequantBlock->_elementFlags & (1<<0)) result = LinearInterpolate(dequantBlock->_mins[0], dequantBlock->_maxs[0], float(*v++)/float(0xffff));
		return result;
	}

	template <>
		Float3 CurveElementDequantDecompressor<Float3>::operator()(unsigned idx, unsigned timeMarkerValue, unsigned componentOffset) const 
	{
		assert(_fmt == Format::R16_UNORM);
		assert(componentOffset == 0);
		const CurveDequantizationBlock* dequantBlock; const void* data;
		std::tie(dequantBlock, data) = FindKey(idx, timeMarkerValue);
		Float3 result { dequantBlock->_mins[0], dequantBlock->_mins[1], dequantBlock->_mins[2] };
		auto* v = (const uint16*)data;
		// Dequantize each element separately
		if (dequantBlock->_elementFlags & (1<<0)) result[0] = LinearInterpolate(dequantBlock->_mins[0], dequantBlock->_maxs[0], float(*v++)/float(0xffff));
		if (dequantBlock->_elementFlags & (1<<1)) result[1] = LinearInterpolate(dequantBlock->_mins[1], dequantBlock->_maxs[1], float(*v++)/float(0xffff));
		if (dequantBlock->_elementFlags & (1<<2)) result[2] = LinearInterpolate(dequantBlock->_mins[2], dequantBlock->_maxs[2], float(*v++)/float(0xffff));
		assert(std::isfinite(result[0]) && std::isfinite(result[1]) && std::isfinite(result[2]));
		assert(!std::isnan(result[0]) && !std::isnan(result[1]) && !std::isnan(result[2]));
		assert(result[0] == result[0] && result[1] == result[1] && result[2] == result[2]);
		return result;
	}

	template <>
		Float4 CurveElementDequantDecompressor<Float4>::operator()(unsigned idx, unsigned timeMarkerValue, unsigned componentOffset) const 
	{
		assert(_fmt == Format::R16_UNORM);
		assert(componentOffset == 0);
		const CurveDequantizationBlock* dequantBlock; const void* data;
		std::tie(dequantBlock, data) = FindKey(idx, timeMarkerValue);
		Float4 result = dequantBlock->_mins;
		auto* v = (const uint16*)data;
		// Dequantize each element separately
		if (dequantBlock->_elementFlags & (1<<0)) result[0] = LinearInterpolate(dequantBlock->_mins[0], dequantBlock->_maxs[0], float(*v++)/float(0xffff));
		if (dequantBlock->_elementFlags & (1<<1)) result[1] = LinearInterpolate(dequantBlock->_mins[1], dequantBlock->_maxs[1], float(*v++)/float(0xffff));
		if (dequantBlock->_elementFlags & (1<<2)) result[2] = LinearInterpolate(dequantBlock->_mins[2], dequantBlock->_maxs[2], float(*v++)/float(0xffff));
		if (dequantBlock->_elementFlags & (1<<3)) result[3] = LinearInterpolate(dequantBlock->_mins[3], dequantBlock->_maxs[3], float(*v++)/float(0xffff));
		assert(std::isfinite(result[0]) && std::isfinite(result[1]) && std::isfinite(result[2]) && std::isfinite(result[3]));
		assert(!std::isnan(result[0]) && !std::isnan(result[1]) && !std::isnan(result[2]) && !std::isnan(result[3]));
		assert(result[0] == result[0] && result[1] == result[1] && result[2] == result[2] && result[3] == result[3]);
		return result;
	}

	template <>
		Quaternion CurveElementDequantDecompressor<Quaternion>::operator()(unsigned idx, unsigned timeMarkerValue, unsigned componentOffset) const
	{
		assert(0);
		return Quaternion();
	}

	template <>
		Float4x4 CurveElementDequantDecompressor<Float4x4>::operator()(unsigned idx, unsigned timeMarkerValue, unsigned componentOffset) const
	{
		assert(0);
		return Float4x4();
	}

	template<typename OutType, typename Decomp>
        OutType        EvaluateCurve(	float evalTime, 
										IteratorRange<const uint16_t*> timeMarkers,
										const CurveKeyDataDesc& keyDataDesc,
										CurveInterpolationType interpolationType,
										const Decomp& decomp) never_throws 
	{
		// reminder -- lower_bound returns a pointer to the first key that is not smaller than inputTime (eg, equal or larger)
		uint16_t evalFrame = evalTime / keyDataDesc._frameDuration;
		auto* key = std::lower_bound(timeMarkers.begin(), timeMarkers.end(), evalFrame);

			// note -- clamping at start and end positions of the curve
		if (key == timeMarkers.end())
			return decomp(0, timeMarkers[0]);

		if (*key != evalFrame) {
			assert(key != timeMarkers.begin());
			--key;	// (back one, to the first key that is smaller)
		}
		auto keyIndex = key-timeMarkers.begin();
		auto alpha = LerpParameter(key[0] * keyDataDesc._frameDuration, key[1] * keyDataDesc._frameDuration, evalTime);
		auto keyCount = timeMarkers.size();
		assert(decomp.KeyCount() == keyCount);

        if (interpolationType == CurveInterpolationType::Linear) {

			// (need at least one key greater than the interpolation point, to perform interpolation correctly)
			if ((key+1) >= timeMarkers.end())
				return decomp(keyCount-1, timeMarkers[keyCount-1]);

            assert(key[1] >= key[0]);		// (validating sorting assumption)
            
            auto P0 = decomp(keyIndex, key[0]);
            auto P1 = decomp(keyIndex+1, key[1]);
            return SphericalInterpolate(P0, P1, alpha);

        } else if (interpolationType == CurveInterpolationType::Bezier) {

            assert(keyDataDesc._flags & CurveKeyDataDesc::Flags::HasInTangent);
			assert(keyDataDesc._flags & CurveKeyDataDesc::Flags::HasOutTangent);

			// (need at least one key greater than the interpolation point, to perform interpolation correctly)
			if ((key+1) >= timeMarkers.end())
				return decomp(keyCount-1, timeMarkers[keyCount-1]);

			assert(key[1] >= key[0]);		// (validating sorting assumption)
            const auto inTangentOffset = BitsPerPixel(keyDataDesc._elementFormat)/8;
            const auto outTangentOffset = inTangentOffset + BitsPerPixel(keyDataDesc._elementFormat)/8;

            auto P0 = decomp(keyIndex, key[0]);
            auto P1 = decomp(keyIndex+1, key[1]);

			// This is a convention of the Collada format
			// (see Collada spec 1.4.1, page 4-4)
			//		the first control point is stored under the semantic "OUT_TANGENT" for P0
			//		and second control point is stored under the semantic "IN_TANGENT" for P1
            auto C0 = decomp(keyIndex, key[0], outTangentOffset);
			auto C1 = decomp(keyIndex+1, key[1], inTangentOffset);

            return SphericalBezierInterpolate(P0, C0, C1, P1, alpha);

		} else if (interpolationType == CurveInterpolationType::CatmullRom) {

			// (need at least one key greater than the interpolation point, to perform interpolation correctly)
			if ((key+2) >= timeMarkers.end())
				return decomp(keyCount-1, timeMarkers[keyCount-1]);

			auto P0 = decomp(keyIndex, key[0]);
            auto P1 = decomp(keyIndex+1, key[1]);
			// (note the clamp here that can result in P0 == P0n1 at the start of the curve)
			auto kn1 = std::max(0, signed(keyIndex)-1);
			auto kp1 = std::min(unsigned(keyIndex+2), unsigned(keyCount-1));
			auto P0n1T = timeMarkers[kn1];
			auto P1p1T = timeMarkers[kp1];
			auto P0n1 = decomp(kn1, P0n1T);
			auto P1p1 = decomp(kp1, P1p1T);

			return SphericalCatmullRomInterpolate(
				P0n1, P0, P1, P1p1, 
				(P0n1T - key[0]) / float(key[1] - key[0]), (P1p1T - key[0]) / float(key[1] - key[0]),
				alpha);

        } else if (interpolationType == CurveInterpolationType::Hermite) {
			// hermite version not implemented
			//  -- but it's similar to both the Bezier and Catmull Rom implementations, nad
			//		could be easily hooked up
			assert(0);      
		}

        return decomp(0, timeMarkers[0]);
    }

	template<typename OutType>
        OutType        RawAnimationCurve::Calculate(float inputTime) const never_throws
    {
		if (_keyDataDesc._flags & CurveKeyDataDesc::Flags::Quantized) {
			return EvaluateCurve<OutType>(	
				inputTime, 
				MakeIteratorRange(_timeMarkers.begin(), _timeMarkers.end()),
				_keyDataDesc, _interpolationType,
				CurveElementDequantDecompressor<OutType>(MakeIteratorRange(_keyData.begin(), _keyData.end()), _keyDataDesc._elementStride, _keyDataDesc._elementFormat, _keyDataDesc._blockCount));
		} else {
			return EvaluateCurve<OutType>(	
				inputTime, 
				MakeIteratorRange(_timeMarkers.begin(), _timeMarkers.end()),
				_keyDataDesc, _interpolationType,
				CurveElementDecompressor<OutType>(MakeIteratorRange(_keyData.begin(), _keyData.end()), _keyDataDesc._elementStride, _keyDataDesc._elementFormat));
		}
	}

    float       RawAnimationCurve::StartTime() const
    {
        if (_timeMarkers.empty()) { return std::numeric_limits<float>::max(); }
        return _timeMarkers[0];
    }

    float       RawAnimationCurve::EndTime() const
    {
        if (_timeMarkers.empty()) return -std::numeric_limits<float>::max();
        return _timeMarkers[_timeMarkers.size()-1];
    }

    template float      RawAnimationCurve::Calculate(float inputTime) const never_throws;
    template Float3     RawAnimationCurve::Calculate(float inputTime) const never_throws;
    template Float4     RawAnimationCurve::Calculate(float inputTime) const never_throws;
    template Float4x4   RawAnimationCurve::Calculate(float inputTime) const never_throws;
	template Quaternion RawAnimationCurve::Calculate(float inputTime) const never_throws;

    RawAnimationCurve::RawAnimationCurve(   SerializableVector<uint16_t>&& timeMarkers, 
											SerializableVector<uint8>&& keyData,
											const CurveKeyDataDesc&	keyDataDesc,
											CurveInterpolationType interpolationType)
    :       _timeMarkers(std::move(timeMarkers))
    ,       _keyData(std::move(keyData))
    ,       _keyDataDesc(keyDataDesc)
    ,       _interpolationType(interpolationType)
    {}

	RawAnimationCurve::~RawAnimationCurve() {}

	RawAnimationCurve::RawAnimationCurve(const RawAnimationCurve& copyFrom)
	: _timeMarkers(copyFrom._timeMarkers)
	, _keyData(copyFrom._keyData)
	, _keyDataDesc(copyFrom._keyDataDesc)
	, _interpolationType(copyFrom._interpolationType) 
	{}

	RawAnimationCurve& RawAnimationCurve::operator=(const RawAnimationCurve& copyFrom) 
	{
		_timeMarkers = copyFrom._timeMarkers;
		_keyData = copyFrom._keyData;
		_keyDataDesc = copyFrom._keyDataDesc;
		_interpolationType = copyFrom._interpolationType;
		return *this;
	}

}}

