// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "SkeletonScaffoldInternal.h"
#include "ModelScaffold.h"
#include "ModelImmutableData.h"
#include "RawAnimationCurve.h"
#include "AssetUtils.h"
#include "../../Assets/ChunkFileContainer.h"
#include "../../Assets/DeferredConstruction.h"
#include "../../Math/Quaternion.h"
#include "../../OSServices/Log.h"

namespace RenderCore { namespace Assets
{
    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	Quaternion Decompress_36bit(const void* data);

    TransformationParameterSet      AnimationSet::BuildTransformationParameterSet(
        const AnimationState&           animState__,
        const SkeletonMachine&			transformationMachine,
        const AnimationSetBinding&      binding,
        IteratorRange<const RawAnimationCurve*>        curves) const
    {
        TransformationParameterSet result(transformationMachine.GetDefaultParameters());
        auto float1s	= result.GetFloat1Parameters();
        auto float3s	= result.GetFloat3Parameters();
        auto float4s	= result.GetFloat4Parameters();
        auto float4x4s	= result.GetFloat4x4Parameters();

        AnimationState animState = animState__;

        size_t driverStart = 0, driverEnd = 0;
        size_t constantDriverStartIndex = 0, constantDriverEndIndex = 0;
        if (animState._animation!=0x0) {
            auto end = _animations.end();
            auto i = std::lower_bound(_animations.begin(), end, animState._animation, CompareFirst<uint64_t, Animation>());
            if (i!=end && i->first == animState._animation) {
                driverStart = i->second._beginDriver;
                driverEnd = i->second._endDriver;
                constantDriverStartIndex = i->second._beginConstantDriver;
                constantDriverEndIndex = i->second._endConstantDriver;
                animState._time += i->second._beginTime;
            }
        }

        const SkeletonMachine::InputInterface& inputInterface 
            = transformationMachine.GetInputInterface();
        for (size_t c=driverStart; c<driverEnd; ++c) {
            const AnimationDriver& driver = _animationDrivers[c];
            unsigned transInputIndex = binding.AnimDriverToMachineParameter(driver._parameterIndex);
            if (transInputIndex == ~unsigned(0x0)) {
                continue;   // (unbound output)
            }

            assert(transInputIndex < inputInterface._parameterCount);
            const SkeletonMachine::InputInterface::Parameter& p 
                = inputInterface._parameters[transInputIndex];
			assert(p._type != AnimSamplerType::Quaternion);	// a driver can have a quaternion sampler type, but not a parameter

            if (driver._samplerType == AnimSamplerType::Float4x4) {
                if (driver._curveIndex < curves.size()) {
                    const RawAnimationCurve& curve = curves[driver._curveIndex];
                    assert(p._type == AnimSamplerType::Float4x4);
                    // assert(i->_index < float4x4s.size());
                    float4x4s[p._index] = curve.Calculate<Float4x4>(animState._time);
                }
            } else if (driver._samplerType == AnimSamplerType::Float4) {
                if (driver._curveIndex < curves.size()) {
                    const RawAnimationCurve& curve = curves[driver._curveIndex];
                    if (p._type == AnimSamplerType::Float4) {
                        float4s[p._index] = curve.Calculate<Float4>(animState._time);
                    } else if (p._type == AnimSamplerType::Float3) {
                        float3s[p._index] = Truncate(curve.Calculate<Float4>(animState._time));
                    } else {
                        assert(p._type == AnimSamplerType::Float1);
                        float1s[p._index] = curve.Calculate<Float4>(animState._time)[0];
                    }
                }
            } else if (driver._samplerType == AnimSamplerType::Quaternion) {
                if (driver._curveIndex < curves.size()) {
                    const RawAnimationCurve& curve = curves[driver._curveIndex];
                    if (p._type == AnimSamplerType::Float4) {
                        *(Quaternion*)&float4s[p._index] = curve.Calculate<Quaternion>(animState._time);
                    }
                }
            } else if (driver._samplerType == AnimSamplerType::Float3) {
                if (driver._curveIndex < curves.size()) {
                    const RawAnimationCurve& curve = curves[driver._curveIndex];
                    if (p._type == AnimSamplerType::Float3) {
                        float3s[p._index] = curve.Calculate<Float3>(animState._time);
                    } else {
                        assert(p._type == AnimSamplerType::Float1);
                        float1s[p._index] = curve.Calculate<Float3>(animState._time)[0];
                    }
                }
            } else if (driver._samplerType == AnimSamplerType::Float1) {
                if (driver._curveIndex < curves.size()) {
                    const RawAnimationCurve& curve = curves[driver._curveIndex];
                    float curveresult = curve.Calculate<float>(animState._time);
                    if (p._type == AnimSamplerType::Float1) {
                        float1s[p._index] = curveresult;
                    } else if (p._type == AnimSamplerType::Float3) {
                        assert(driver._samplerOffset < 3);
                        float3s[p._index][driver._samplerOffset] = curveresult;
                    } else if (p._type == AnimSamplerType::Float4) {
                        assert(driver._samplerOffset < 4);
                        float4s[p._index][driver._samplerOffset] = curveresult;
                    }
                }
            }
        }

        for (   size_t c=constantDriverStartIndex; c<constantDriverEndIndex; ++c) {
            const ConstantDriver& driver = _constantDrivers[c];
            unsigned transInputIndex = binding.AnimDriverToMachineParameter(driver._parameterIndex);
            if (transInputIndex == ~unsigned(0x0)) {
                continue;   // (unbound output)
            }

            assert(transInputIndex < inputInterface._parameterCount);
            const SkeletonMachine::InputInterface::Parameter& p 
                = inputInterface._parameters[transInputIndex];
			assert(p._type != AnimSamplerType::Quaternion);	// a driver can have a quaternion sampler type, but not a parameter

            const void* data    = PtrAdd(_constantData.begin(), driver._dataOffset);
            if (driver._samplerType == AnimSamplerType::Float4x4) {
                assert(p._type == AnimSamplerType::Float4x4);
                float4x4s[p._index] = *(const Float4x4*)data;
            } else if (driver._samplerType == AnimSamplerType::Float4) {
                if (p._type == AnimSamplerType::Float4) {
                    float4s[p._index] = *(const Float4*)data;
                } else if (p._type == AnimSamplerType::Float3) {
                    float3s[p._index] = Truncate(*(const Float4*)data);
                }
			} else if (driver._samplerType == AnimSamplerType::Quaternion) {
                if (p._type == AnimSamplerType::Float4) {
					if (driver._format == Format::R12G12B12A4_SNORM) {
						*(Quaternion*)&float4s[p._index] = Decompress_36bit(data);
					} else {
						assert(driver._format == Format::R32G32B32A32_FLOAT);
						float4s[p._index] = *(const Float4*)data;
					}
                }
            } else if (driver._samplerType == AnimSamplerType::Float3) {
                assert(p._type == AnimSamplerType::Float3);
                float3s[p._index] = *(Float3*)data;
            } else if (driver._samplerType == AnimSamplerType::Float1) {
                if (p._type == AnimSamplerType::Float1) {
                    float1s[p._index] = *(float*)data;
                } else if (p._type == AnimSamplerType::Float3) {
                    assert(driver._samplerOffset < 3);
                    float3s[p._index][driver._samplerOffset] = *(const float*)data;
                } else if (p._type == AnimSamplerType::Float4) {
                    assert(driver._samplerOffset < 4);
                    float4s[p._index][driver._samplerOffset] = *(const float*)data;
                }
            }
        }

        return result;
    }

    AnimationSet::Animation AnimationSet::FindAnimation(uint64_t animation) const
    {
		auto i = std::lower_bound(
			_animations.begin(), _animations.end(),
			animation, CompareFirst<uint64_t, Animation>());
		if (i!=_animations.end() && i->first == animation)
			return i->second;

        Animation result;
        result._beginDriver = result._endDriver = 0;
        result._beginTime = result._endTime = 0.f;
		result._beginConstantDriver = result._endConstantDriver = 0;
        return result;
    }

    unsigned                AnimationSet::FindParameter(uint64_t parameterName) const
    {
        for (size_t c=0; c<_outputInterface.size(); ++c) {
            if (_outputInterface[c] == parameterName) {
                return unsigned(c);
            }
        }
        return ~unsigned(0x0);
    }

	StringSection<>			AnimationSet::LookupStringName(uint64_t animation) const
	{
		auto i = std::lower_bound(
			_animations.begin(), _animations.end(),
			animation, CompareFirst<uint64_t, Animation>());
		if (i==_animations.end() || i->first != animation)
			return {};

		auto idx = std::distance(_animations.begin(), i);
		return MakeStringSection(
			_stringNameBlock.begin() + _stringNameBlockOffsets[idx],
			_stringNameBlock.begin() + _stringNameBlockOffsets[idx+1]);
	}

    AnimationSet::AnimationSet() {}
    AnimationSet::~AnimationSet() {}

	void AnimationSet::SerializeMethod(::Assets::NascentBlockSerializer& serializer) const
	{
		SerializationOperator(serializer, _animationDrivers);
		SerializationOperator(serializer, _constantDrivers);
		SerializationOperator(serializer, _constantData);
		SerializationOperator(serializer, _animations);
		SerializationOperator(serializer, _outputInterface);
		SerializationOperator(serializer, _stringNameBlockOffsets);
		SerializationOperator(serializer, _stringNameBlock);
	}

    AnimationImmutableData::AnimationImmutableData() {}
    AnimationImmutableData::~AnimationImmutableData() {}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

    AnimationSetBinding::AnimationSetBinding(
        const AnimationSet::OutputInterface&            output,
        const SkeletonMachine::InputInterface&    input)
    {
            //
            //      for each animation set output value, match it with a 
            //      value in the transformation machine interface
            //      The interfaces are not sorted, we we just have to 
            //      do brute force searches. But there shouldn't be many
            //      parameters (so it should be fairly quick)
            //
        std::vector<unsigned> result;
        result.resize(output.size());
        for (size_t c=0; c<output.size(); ++c) {
            uint64_t parameterName = output[c];
            result[c] = ~unsigned(0x0);

            for (size_t c2=0; c2<input._parameterCount; ++c2) {
                if (input._parameters[c2]._name == parameterName) {
                    result[c] = unsigned(c2);
                    break;
                }
            }

            #if defined(_DEBUG)
                if (result[c] == ~unsigned(0x0)) {
                    Log(Warning) << "Animation driver output cannot be bound to transformation machine input" << std::endl;
                }
            #endif
        }

        _animDriverToMachineParameter = std::move(result);
    }

    AnimationSetBinding::AnimationSetBinding() {}
    AnimationSetBinding::AnimationSetBinding(AnimationSetBinding&& moveFrom) never_throws
    : _animDriverToMachineParameter(std::move(moveFrom._animDriverToMachineParameter))
    {}
    AnimationSetBinding& AnimationSetBinding::operator=(AnimationSetBinding&& moveFrom) never_throws
    {
        _animDriverToMachineParameter = std::move(moveFrom._animDriverToMachineParameter);
        return *this;
    }
    AnimationSetBinding::~AnimationSetBinding() {}

    SkeletonBinding::SkeletonBinding(   const SkeletonMachine::OutputInterface&		output,
                                        const ModelCommandStream::InputInterface&   input)
    {
        std::vector<unsigned> result(input._jointCount, ~0u);

        for (size_t c=0; c<input._jointCount; ++c) {
            uint64_t name = input._jointNames[c];
            for (size_t c2=0; c2<output._outputMatrixNameCount; ++c2) {
                if (output._outputMatrixNames[c2] == name) {
                    result[c] = unsigned(c2);
                    break;
                }
            }

            #if defined(_DEBUG)
                // if (result[c] == ~unsigned(0x0)) {
                //     LogWarning << "Couldn't bind skin matrix to transformation machine output.";
                // }
            #endif
        }
            
        _modelJointIndexToMachineOutput = std::move(result);
    }

    SkeletonBinding::SkeletonBinding() {}
    SkeletonBinding::~SkeletonBinding() {}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

    void SkeletonMachine::GenerateOutputTransforms(   
        IteratorRange<Float4x4*> output,
        const TransformationParameterSet*   parameterSet) const
    {
        if (output.size() < _outputMatrixCount)
            Throw(::Exceptions::BasicLabel("Output buffer to SkeletonMachine::GenerateOutputTransforms is too small"));
        RenderCore::Assets::GenerateOutputTransforms(
            output, parameterSet, 
            MakeIteratorRange(_commandStream, _commandStream + _commandStreamSize));
    }

	void SkeletonMachine::CalculateParentPointers(IteratorRange<unsigned*> output) const
	{
		RenderCore::Assets::CalculateParentPointers(output, MakeIteratorRange(_commandStream, _commandStream + _commandStreamSize));
	}

	std::vector<StringSection<>> SkeletonMachine::GetOutputMatrixNames() const
	{
		std::vector<StringSection<>> result;
		result.reserve(_outputInterface._outputMatrixNameCount);
		auto nameStart = _outputMatrixNames.begin();
		for (auto i=_outputMatrixNames.begin(); i!=_outputMatrixNames.end();) {
			if (*i == 0) {
				result.push_back(MakeStringSection(nameStart, i));
				++i;
				nameStart = i;
			} else {
				++i;
			}
		}
		return result;
	}

    SkeletonMachine::SkeletonMachine()
    {
        _commandStream = nullptr;
        _commandStreamSize = 0;
        _outputMatrixCount = 0;
    }

    SkeletonMachine::~SkeletonMachine()
    {
    }

///////////////////////////////////////////////////////////////////////////////////////////////////

    const SkeletonMachine&   SkeletonScaffold::GetTransformationMachine() const                
    {
        return *(const SkeletonMachine*)::Assets::Block_GetFirstObject(_rawMemoryBlock.get());
    }

    const ::Assets::ArtifactRequest SkeletonScaffold::ChunkRequests[]
    {
        ::Assets::ArtifactRequest { "Scaffold", ChunkType_Skeleton, 0, ::Assets::ArtifactRequest::DataType::BlockSerializer },
    };
    
    SkeletonScaffold::SkeletonScaffold(IteratorRange<::Assets::ArtifactRequestResult*> chunks, const ::Assets::DependencyValidation& depVal)
	: _depVal(depVal)
    {
		assert(chunks.size() == 1);
		_rawMemoryBlock = std::move(chunks[0]._buffer);
    }

    SkeletonScaffold::SkeletonScaffold(SkeletonScaffold&& moveFrom) never_throws
    : _rawMemoryBlock(std::move(moveFrom._rawMemoryBlock))
	, _depVal(std::move(moveFrom._depVal))
    {}

    SkeletonScaffold& SkeletonScaffold::operator=(SkeletonScaffold&& moveFrom) never_throws
    {
		if (_rawMemoryBlock)
            GetTransformationMachine().~SkeletonMachine();
        _rawMemoryBlock = std::move(moveFrom._rawMemoryBlock);
		_depVal = std::move(moveFrom._depVal);
        return *this;
    }

    SkeletonScaffold::SkeletonScaffold() {}

    SkeletonScaffold::~SkeletonScaffold()
    {
        if (_rawMemoryBlock)
		    GetTransformationMachine().~SkeletonMachine();
    }

///////////////////////////////////////////////////////////////////////////////////////////////////

    const AnimationImmutableData&   AnimationSetScaffold::ImmutableData() const                
    {
        return *(const AnimationImmutableData*)::Assets::Block_GetFirstObject(_rawMemoryBlock.get());
    }

    const ::Assets::ArtifactRequest AnimationSetScaffold::ChunkRequests[]
    {
        ::Assets::ArtifactRequest { "Scaffold", ChunkType_AnimationSet, 0, ::Assets::ArtifactRequest::DataType::BlockSerializer },
    };
    
    AnimationSetScaffold::AnimationSetScaffold(IteratorRange<::Assets::ArtifactRequestResult*> chunks, const ::Assets::DependencyValidation& depVal)
	: _depVal(depVal)
    {
		assert(chunks.size() == 1);
		_rawMemoryBlock = std::move(chunks[0]._buffer);
    }

    AnimationSetScaffold::AnimationSetScaffold(AnimationSetScaffold&& moveFrom) never_throws
    : _rawMemoryBlock(std::move(moveFrom._rawMemoryBlock))
	, _depVal(std::move(moveFrom._depVal))
    {}

    AnimationSetScaffold& AnimationSetScaffold::operator=(AnimationSetScaffold&& moveFrom) never_throws
    {
		if (_rawMemoryBlock)
            ImmutableData().~AnimationImmutableData();
        _rawMemoryBlock = std::move(moveFrom._rawMemoryBlock);
		_depVal = std::move(moveFrom._depVal);
        return *this;
    }

    AnimationSetScaffold::AnimationSetScaffold() {}

    AnimationSetScaffold::~AnimationSetScaffold()
    {
        if (_rawMemoryBlock)
            ImmutableData().~AnimationImmutableData();
    }

///////////////////////////////////////////////////////////////////////////////////////////////////

	void TransformationParameterSet::Set(uint32 index, float p) 
	{
		if (_float1Parameters.size() < (index+1)) _float1Parameters.resize(index+1, 0.f);
		_float1Parameters[index] = p;
	}

	void TransformationParameterSet::Set(uint32 index, Float3 p)
	{
		if (_float3Parameters.size() < (index+1)) _float3Parameters.resize(index+1, Zero<Float3>());
		_float3Parameters[index] = p;
	}

	void TransformationParameterSet::Set(uint32 index, Float4 p)
	{
		if (_float4Parameters.size() < (index+1)) _float4Parameters.resize(index+1, Zero<Float4>());
		_float4Parameters[index] = p;
	}

	void TransformationParameterSet::Set(uint32 index, Quaternion q)
	{
		// note that packing here must agree with how we unpack in TransformationCommands.cpp
		// The "Order" parameter we use with cml::quaternion is significant. Here, we're assuming
		// "scalar_first" mode
		static_assert(std::is_same_v<Quaternion::order_type, cml::scalar_first>, "Unexpected quaternion ordering");
		Float4 float4Form { q.real(), q.imaginary()[0], q.imaginary()[1], q.imaginary()[2] };
		Set(index, float4Form);
	}

	void TransformationParameterSet::Set(uint32 index, const Float4x4& p)
	{
		if (_float4x4Parameters.size() < (index+1)) _float4x4Parameters.resize(index+1, Zero<Float4x4>());
		_float4x4Parameters[index] = p;
	}

    TransformationParameterSet::TransformationParameterSet() {}

    TransformationParameterSet::TransformationParameterSet(TransformationParameterSet&& moveFrom)
    :       _float4x4Parameters(    std::move(moveFrom._float4x4Parameters))
    ,       _float4Parameters(      std::move(moveFrom._float4Parameters))
    ,       _float3Parameters(      std::move(moveFrom._float3Parameters))
    ,       _float1Parameters(      std::move(moveFrom._float1Parameters))
    {}

    TransformationParameterSet& TransformationParameterSet::operator=(TransformationParameterSet&& moveFrom)
    {
        _float4x4Parameters = std::move(moveFrom._float4x4Parameters);
        _float4Parameters   = std::move(moveFrom._float4Parameters);
        _float3Parameters   = std::move(moveFrom._float3Parameters);
        _float1Parameters   = std::move(moveFrom._float1Parameters);
        return *this;
    }

    TransformationParameterSet::TransformationParameterSet(const TransformationParameterSet& copyFrom)
    :       _float4x4Parameters(copyFrom._float4x4Parameters)
    ,       _float4Parameters(copyFrom._float4Parameters)
    ,       _float3Parameters(copyFrom._float3Parameters)
    ,       _float1Parameters(copyFrom._float1Parameters)
    {
    }

    TransformationParameterSet&  TransformationParameterSet::operator=(const TransformationParameterSet& copyFrom)
    {
        _float4x4Parameters = copyFrom._float4x4Parameters;
        _float4Parameters = copyFrom._float4Parameters;
        _float3Parameters = copyFrom._float3Parameters;
        _float1Parameters = copyFrom._float1Parameters;
        return *this;
    }

    void    TransformationParameterSet::SerializeMethod(::Assets::NascentBlockSerializer& outputSerializer) const
    {
        SerializationOperator(outputSerializer, _float4x4Parameters);
        SerializationOperator(outputSerializer, _float4Parameters);
        SerializationOperator(outputSerializer, _float3Parameters);
        SerializationOperator(outputSerializer, _float1Parameters);
    }
}}
