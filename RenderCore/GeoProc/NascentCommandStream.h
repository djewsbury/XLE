// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "NascentSkeletonMachine.h"
#include "../RenderCore/Assets/AnimationScaffoldInternal.h"
#include <vector>
#include <string>

namespace Assets { class NascentBlockSerializer; }
namespace RenderCore { namespace Assets { class RawAnimationCurve; }}
namespace RenderCore { enum class Format : int; }
namespace Utility { class OutputStream; }

namespace RenderCore { namespace Assets { namespace GeoProc
{
    class NascentSkeleton;
	class NascentSkeletonMachine;

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

        void    AddAnimationDriver(
            StringOrHash        parameterName, 
            unsigned            curveIndex, 
            AnimSamplerType     samplerType, 
            unsigned            samplerOffset);

        void    AddConstantDriver(  
            StringOrHash        parameterName, 
            const void*         constantValue,
			size_t				constantValueSize,
			Format				format,
            AnimSamplerType     samplerType, 
            unsigned            samplerOffset);

        bool    HasAnimationDriver(StringOrHash parameterName) const;
        void    MergeInAsAnIndividualAnimation(const NascentAnimationSet& copyFrom, const std::string& name);
		void    MergeInAsManyAnimations(const NascentAnimationSet& copyFrom, const std::string& namePrefix = {});
		void	MakeIndividualAnimation(const std::string& name);

		void	AddAnimation(
			const std::string& name, 
			unsigned driverBegin, unsigned driverEnd,
			unsigned constantBegin, unsigned constantEnd,
			float minTime, float maxTime);

		unsigned AddCurve(RenderCore::Assets::RawAnimationCurve&& curve);

		IteratorRange<const AnimationDriver*> GetAnimationDrivers() const { return MakeIteratorRange(_animationDrivers); }
		IteratorRange<const ConstantDriver*> GetConstantDrivers() const { return MakeIteratorRange(_constantDrivers); }
        IteratorRange<const RenderCore::Assets::RawAnimationCurve*> GetCurves() const { return MakeIteratorRange(_curves); }
		unsigned GetParameterIndex(const std::string& parameterName) const;

		friend std::ostream& SerializationOperator(
			std::ostream& stream, 
			const NascentAnimationSet& transMachine);

        void            SerializeMethod(::Assets::NascentBlockSerializer& serializer) const;
    private:
        std::vector<AnimationDriver>    _animationDrivers;
        std::vector<ConstantDriver>     _constantDrivers;
        std::vector<std::pair<std::string, Animation>>          _animations;
        std::vector<StringOrHash>        _parameterInterfaceDefinition;
        std::vector<uint8>              _constantData;
		SerializableVector<RenderCore::Assets::RawAnimationCurve> _curves;
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
        NascentSkeletonMachine&				GetSkeletonMachine()			{ return _skeletonMachine; }
        const NascentSkeletonMachine&		GetSkeletonMachine() const		{ return _skeletonMachine; }

		struct Transform
		{
			std::optional<Float4x4> _fullTransform;
			std::optional<Float3> _translation;
			std::optional<Quaternion> _rotationAsQuaternion;
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

		void	SerializeMethod(::Assets::NascentBlockSerializer& serializer) const;

    private:
        NascentSkeletonMachine		_skeletonMachine;
        std::vector<std::pair<uint64_t, std::string>> _dehashTable;

        std::vector<uint32_t> TransformToCmds(const Transform& transform, unsigned& cmdCount);
    };

        //
        //      "Model Command Stream" is the list of model elements
        //      from a single export. Usually it will be mostly geometry
        //      and skin controller elements.
        //

    class NascentModelCommandStream
    {
    public:
        typedef uint64 MaterialGuid;
        static const MaterialGuid s_materialGuid_Invalid = ~0x0ull;

            /////   G E O M E T R Y   I N S T A N C E   /////
        class GeometryInstance
        {
        public:
            unsigned                    _id = ~0u;
            unsigned                    _localToWorldId = ~0u;
            std::vector<MaterialGuid>   _materials;
            unsigned                    _levelOfDetail = ~0u;

            void SerializeMethod(::Assets::NascentBlockSerializer& serializer) const;
        };

            /////   S K I N   C O N T R O L L E R   I N S T A N C E   /////
        class SkinControllerInstance
        {
        public:
            unsigned                    _id = ~0u;
            unsigned                    _localToWorldId = ~0u;
            std::vector<MaterialGuid>   _materials;
            unsigned                    _levelOfDetail = ~0u;

            void SerializeMethod(::Assets::NascentBlockSerializer& serializer) const;
        };

            /////   C A M E R A   I N S T A N C E   /////
        class CameraInstance
        {
        public:
            unsigned    _localToWorldId = ~0u;
        };

        void Add(GeometryInstance&& geoInstance);
        void Add(CameraInstance&& geoInstance);
        void Add(SkinControllerInstance&& geoInstance);

        bool IsEmpty() const { return _geometryInstances.empty() && _cameraInstances.empty() && _skinControllerInstances.empty(); }
        void SerializeMethod(::Assets::NascentBlockSerializer& serializer) const;

        unsigned RegisterInputInterfaceMarker(const std::string& skeletonName, const std::string& name);

        std::vector<uint64_t> BuildHashedInputInterface() const;
        unsigned GetMaxLOD() const;

        std::vector<GeometryInstance>               _geometryInstances;
        std::vector<CameraInstance>                 _cameraInstances;
        std::vector<SkinControllerInstance>         _skinControllerInstances;

        friend std::ostream& SerializationOperator(std::ostream&, const NascentModelCommandStream&);

    private:
        std::vector<std::pair<std::string, std::string>>	_inputInterfaceNames;
    };
}}}


