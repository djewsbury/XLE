// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../Format.h"
#include "../../Utility/PtrUtils.h"
#include "../../Utility/Streams/SerializationUtils.h"
#include <memory>

namespace RenderCore { namespace Assets
{
    enum class CurveInterpolationType : unsigned { None, Linear, Bezier, Hermite, CatmullRom, NURBS };
    enum class TimeMarkerType : unsigned { None, Default, NURBSKnots };

	struct CurveDesc
	{
		struct Flags { enum BitValue { HasDequantBlock = 1<<0, HasInTangent = 1<<1, HasOutTangent = 1<<2 }; using BitField = unsigned; };
		Flags::BitField	_flags = 0;
		unsigned		_elementStride = 0;
		Format			_elementFormat = Format(0);
        TimeMarkerType  _timeMarkerType = TimeMarkerType::None;
	};

    class RawAnimationCurve 
    {
    public:
        template<typename Serializer>
            void        SerializeMethod(Serializer& outputSerializer) const;

        uint16_t    TimeAtFirstKeyframe() const;
        uint16_t    TimeAtLastKeyframe() const;
        const CurveDesc& Desc() const { return _desc; }

        template<typename OutType>
            OutType        Calculate(float inputTime, CurveInterpolationType interpolationType) const never_throws;

		RawAnimationCurve(  SerializableVector<uint16_t>&& timeMarkers, 
                            SerializableVector<uint8_t>&& keyData,
							const CurveDesc&	keyDataDesc);
        RawAnimationCurve(RawAnimationCurve&& moveFrom) = default;
        RawAnimationCurve& operator=(RawAnimationCurve&& moveFrom) = default;
		RawAnimationCurve(const RawAnimationCurve& copyFrom) = default;
		RawAnimationCurve& operator=(const RawAnimationCurve& copyFrom) = default;
		~RawAnimationCurve();

    protected:
        SerializableVector<uint16_t>    _timeMarkers;
        SerializableVector<uint8_t>	    _keyData;
        CurveDesc			            _desc;
    };

    struct CurveDequantizationBlock
	{
		unsigned _elementFlags;
		float _mins[4], _maxs[4];
	};

    template<typename Serializer>
        void        RawAnimationCurve::SerializeMethod(Serializer& outputSerializer) const
    {
        SerializationOperator(outputSerializer, _timeMarkers);
        SerializationOperator(outputSerializer, _keyData);
        SerializationOperator(outputSerializer, _desc._flags);
		SerializationOperator(outputSerializer, _desc._elementStride);
        SerializationOperator(outputSerializer, unsigned(_desc._elementFormat));
		SerializationOperator(outputSerializer, unsigned(_desc._timeMarkerType));
    }

}}





