// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "NascentSkeletonHelper.h"
#include "../Assets/AnimationScaffoldInternal.h"
#include "../../Math/Transformations.h"
#include <vector>
#include <string>

namespace Assets { class BlockSerializer; }
namespace RenderCore { namespace Assets { class RawAnimationCurve; }}
namespace RenderCore { enum class Format : int; }
namespace Utility { class OutputStream; }

namespace RenderCore { namespace Assets { namespace GeoProc
{
        //
        //      "NascentAnimationSet" is a set of animations
        //      and some information to bind these animations to
        //      a skeleton
        //

    class NascentAnimationSet
    {
    public:
		using AnimationDriver = RenderCore::Assets::AnimationSet::AnimationDriver;
		using ConstantDriver = RenderCore::Assets::AnimationSet::ConstantDriver;
		using AnimationBlock = RenderCore::Assets::AnimationSet::AnimationBlock;
        using Animation = RenderCore::Assets::AnimationSet::Animation;

        struct StringOrHash
        {
            std::optional<std::string> _stringForm;
            uint64_t _hashForm = ~0ull;
            StringOrHash() = default;
            StringOrHash(const std::string&);
            StringOrHash(uint64_t);
            friend bool operator==(const StringOrHash& lhs, const StringOrHash& rhs);
        };

        struct BlockSpan { unsigned _beginFrame, _endFrame; };
        class NascentBlock;
        std::vector<NascentBlock> AddAnimation(const std::string& name, IteratorRange<const BlockSpan*>, float framesPerSecond);

        class NascentBlock
        {
        public:
            unsigned AddCurve(RenderCore::Assets::RawAnimationCurve&& curve);

            void    AddAnimationDriver(
                StringOrHash            parameterName, 
                AnimSamplerComponent    parameterComponent,
                AnimSamplerType         samplerType,
                unsigned                curveIndex,
                CurveInterpolationType 	interpolationType);

            void    AddConstantDriver(  
                StringOrHash            parameterName, 
                AnimSamplerComponent    parameterComponent,
                AnimSamplerType         samplerType,
                const void*             constantValue,
                size_t                  constantValueSize,
                Format                  format);
        private:
            NascentAnimationSet* _animSet = nullptr;
            unsigned _blockIdx = 0;
            NascentBlock(NascentAnimationSet& animSet, unsigned blockIdx) : _animSet(&animSet), _blockIdx(blockIdx) {}
            NascentBlock() = default;
            friend class NascentAnimationSet;
        };

        unsigned  AddParameter(StringOrHash parameterName, AnimSamplerComponent parameterComponent, AnimSamplerType samplerType);

        bool    HasAnimationDriver(StringOrHash parameterName) const;
		void    MergeInAsManyAnimations(const NascentAnimationSet& copyFrom, const std::string& namePrefix = {});
		void	MakeIndividualAnimation(const std::string& name, float framesPerSecond);
        unsigned AddCurve(RenderCore::Assets::RawAnimationCurve&& curve);

		IteratorRange<const AnimationDriver*> GetAnimationDrivers() const { return MakeIteratorRange(_animationDrivers); }
		IteratorRange<const ConstantDriver*> GetConstantDrivers() const { return MakeIteratorRange(_constantDrivers); }
        IteratorRange<const RenderCore::Assets::RawAnimationCurve*> GetCurves() const { return MakeIteratorRange(_curves); }
		unsigned GetParameterIndex(const std::string& parameterName, AnimSamplerComponent parameterComponent) const;

		friend std::ostream& SerializationOperator(std::ostream&, const NascentAnimationSet&);
        friend void SerializationOperator(::Assets::BlockSerializer&, const NascentAnimationSet&);
    private:
        std::vector<AnimationDriver>    _animationDrivers;
        std::vector<ConstantDriver>     _constantDrivers;
        std::vector<AnimationBlock>     _animationBlocks;
        std::vector<std::pair<std::string, Animation>>              _animations;
        struct Param { StringOrHash _name; AnimSamplerComponent _component; AnimSamplerType _samplerType; };
        std::vector<Param>              _parameterInterfaceDefinition;
        std::vector<uint8_t>            _constantData;
		std::vector<RenderCore::Assets::RawAnimationCurve> _curves;

        void AppendAnimationDriverToBlock(unsigned blockIdx, unsigned driverIdx);
        void AppendConstantDriverToBlock(unsigned blockIdx, unsigned driverIdx);
    };

        //
        //      "NascentSkeleton" represents the skeleton information for an 
        //      object. Usually this is mostly just the transformation machine.
        //      But we also need some binding information for binding the output
        //      matrices of the transformation machine to joints.
        //

    class NascentSkeleton
    {
    public:
		struct Transform
		{
			std::optional<Float4x4> _fullTransform;
			std::optional<Float3> _translation;
			std::optional<Quaternion> _rotationAsQuaternion;
            std::optional<ArbitraryRotation> _rotationAsAxisAngle;
			std::optional<Float3> _arbitraryScale;
			std::optional<float> _uniformScale;

            Transform() = default;
            Transform(const Float4x4&);
            Transform(const Float3& translation, const Quaternion& rotation, float scale);
		};
		void	WriteStaticTransform(const Transform& transform);
		void    WriteParameterizedTransform(StringSection<> parameterName, const Transform& transform);

		void	WriteOutputMarker(StringSection<> skeletonName, StringSection<> jointName);

		void	WritePushLocalToWorld();
		void	WritePopLocalToWorld(unsigned popCount=1);

        friend void SerializationOperator(::Assets::BlockSerializer&, const NascentSkeleton&);

        Internal::NascentSkeletonHelper&            GetSkeletonMachine()			{ return _skeletonMachine; }
        const Internal::NascentSkeletonHelper&      GetSkeletonMachine() const		{ return _skeletonMachine; }
    private:
        Internal::NascentSkeletonHelper		_skeletonMachine;
        std::vector<std::pair<uint64_t, std::string>> _dehashTable;

        std::vector<uint32_t> TransformToCmds(const Transform& transform, unsigned& cmdCount);
    };

}}}


