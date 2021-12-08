// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "TransformationCommands.h"
#include "../Format.h"
#include "../../Utility/IteratorUtils.h"
#include "../../Utility/StringUtils.h"
#include "../../Utility/Streams/SerializationUtils.h"

namespace RenderCore { namespace Assets
{
    class RawAnimationCurve;
    class AnimationSetBinding;
    class SkeletonMachine;
	namespace GeoProc { class NascentAnimationSet; }

	/// <summary>Represents the state of animation effects on an object<summary>
    /// AnimationState is a placeholder for containing the states related to
    /// animating vertices in a model.
    class AnimationState
    {
    public:
            // only a single animation supported currently //
        float       _time = 0.f;
        uint64_t	_animation = 0;
    };

    #pragma pack(push)
    #pragma pack(1)

    class AnimationSet
    {
    public:
            /////   A N I M A T I O N   D R I V E R   /////
        class AnimationDriver
        {
        public:
            unsigned            _curveIndex = ~0u;
            unsigned            _parameterIndex = ~0u;
            AnimSamplerType     _samplerType = (AnimSamplerType)~0u;
			unsigned            _samplerOffset = ~0u;

			static const bool SerializeRaw = true;
        };

			/////   C O N S T A N T   D R I V E R   /////
        class ConstantDriver
        {
        public:
            unsigned            _dataOffset = ~0u;
            unsigned            _parameterIndex = ~0u;
			Format				_format = (Format)0;
            AnimSamplerType     _samplerType = (AnimSamplerType)~0u;
			unsigned            _samplerOffset = ~0u;

            static const bool SerializeRaw = true;
        };

		using OutputInterface = IteratorRange<const uint64_t*>;

        class Animation
        {
        public:
            unsigned    _beginDriver, _endDriver;
            unsigned    _beginConstantDriver, _endConstantDriver;
            float       _beginTime, _endTime;

			static const bool SerializeRaw = true;
        };
		using AnimationAndName = std::pair<uint64_t, Animation>;

        TransformationParameterSet  BuildTransformationParameterSet(
            const AnimationState&					animState,
            const SkeletonMachine&					transformationMachine,
            const AnimationSetBinding&				binding,
            IteratorRange<const RawAnimationCurve*>	curves) const;

        Animation               FindAnimation(uint64_t animation) const;
        unsigned                FindParameter(uint64_t parameterName) const;
		StringSection<>			LookupStringName(uint64_t animation) const;

		IteratorRange<const AnimationDriver*> GetAnimationDrivers() const { return MakeIteratorRange(_animationDrivers); }
		IteratorRange<const ConstantDriver*> GetConstantDrivers() const { return MakeIteratorRange(_constantDrivers); }
		IteratorRange<const AnimationAndName*> GetAnimations() const { return MakeIteratorRange(_animations); }
        IteratorRange<const void*> GetConstantData() const { return MakeIteratorRange(_constantData); }

        OutputInterface	GetOutputInterface() const { return MakeIteratorRange(_outputInterface); }

        AnimationSet();
        ~AnimationSet();

		AnimationSet(const AnimationSet&) = delete;
		AnimationSet& operator=(const AnimationSet&) = delete;

		void            SerializeMethod(::Assets::NascentBlockSerializer& serializer) const;
    protected:
        SerializableVector<AnimationDriver>		_animationDrivers;
        SerializableVector<ConstantDriver>		_constantDrivers;
        SerializableVector<uint8_t>				_constantData;
        SerializableVector<AnimationAndName>	_animations;
        SerializableVector<uint64_t>			_outputInterface;

		SerializableVector<unsigned>			_stringNameBlockOffsets;
		SerializableVector<char>				_stringNameBlock;

		friend class GeoProc::NascentAnimationSet;
    };

    class AnimationImmutableData
    {
    public:
        AnimationSet							_animationSet;
        SerializableVector<RawAnimationCurve>	_curves;

        AnimationImmutableData();
        ~AnimationImmutableData();
    };

    #pragma pack(pop)

}}

