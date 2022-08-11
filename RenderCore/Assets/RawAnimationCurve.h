// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../Format.h"
#include "../../Utility/PtrUtils.h"
#include "../../Utility/Streams/SerializationUtils.h"
#include "../../Core/Types.h"
#include <memory>

namespace RenderCore { namespace Assets
{
	struct CurveKeyDataDesc
	{
		struct Flags { enum BitValue { Quantized = 1<<0, HasInTangent = 1<<1, HasOutTangent = 1<<2 }; using BitField = unsigned; };
		Flags::BitField	_flags = 0;
		unsigned		_elementStride = 0;
		Format			_elementFormat = Format(0);
        float           _frameDuration = 0.f;
        unsigned        _blockCount = 1;
	};

	struct CurveDequantizationBlock
	{
		unsigned _elementFlags;
		float _mins[4], _maxs[4];
	};

	enum class CurveInterpolationType : unsigned { Linear, Bezier, Hermite, CatmullRom, NURBS };

    class RawAnimationCurve 
    {
    public:
        template<typename Serializer>
            void        SerializeMethod(Serializer& outputSerializer) const;

        float       StartTime() const;
        float       EndTime() const;

        template<typename OutType>
            OutType        Calculate(float inputTime) const never_throws;

		RawAnimationCurve(  SerializableVector<uint16_t>&& timeMarkers, 
                            SerializableVector<uint8_t>&& keyData,
							const CurveKeyDataDesc&	keyDataDesc,
                            CurveInterpolationType	interpolationType);
        RawAnimationCurve(RawAnimationCurve&& moveFrom) = default;
        RawAnimationCurve& operator=(RawAnimationCurve&& moveFrom) = default;
		RawAnimationCurve(const RawAnimationCurve& copyFrom) = default;
		RawAnimationCurve& operator=(const RawAnimationCurve& copyFrom) = default;
		~RawAnimationCurve();

    protected:
        SerializableVector<uint16_t>    _timeMarkers;
        SerializableVector<uint8_t>	    _keyData;
        CurveKeyDataDesc			    _keyDataDesc;
		CurveInterpolationType		    _interpolationType;
    };

    template<typename Serializer>
        void        RawAnimationCurve::SerializeMethod(Serializer& outputSerializer) const
    {
        SerializationOperator(outputSerializer, _timeMarkers);
        SerializationOperator(outputSerializer, _keyData);
        SerializationOperator(outputSerializer, _keyDataDesc._flags);
		SerializationOperator(outputSerializer, _keyDataDesc._elementStride);
        SerializationOperator(outputSerializer, unsigned(_keyDataDesc._elementFormat));
        SerializationOperator(outputSerializer, _keyDataDesc._frameDuration);
        SerializationOperator(outputSerializer, _keyDataDesc._blockCount);        
		SerializationOperator(outputSerializer, unsigned(_interpolationType));
    }

}}





