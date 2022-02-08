// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "AnimationSet.h"
#include "RawAnimationCurve.h"
#include "../../Assets/BlockSerializer.h"
#include "../../Utility/Streams/SerializationUtils.h"

namespace RenderCore { namespace Assets
{
 	Quaternion Decompress_36bit(const void* data);

	void AnimationSet::CalculateOutput(
		IteratorRange<void*> outputBlock,
		const AnimationState& animState__,
		IteratorRange<const ParameterBindingRules*> bindingRules) const
	{
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

		for (size_t c=driverStart; c<driverEnd; ++c) {
			const AnimationDriver& driver = _animationDrivers[c];
			auto& br = bindingRules[driver._parameterIndex];
			if (br._outputOffset  == ~0x0) continue;   // (unbound output)

			assert(br._samplerType == driver._samplerType);
			auto* dst = PtrAdd(outputBlock.begin(), br._outputOffset);

			if (driver._samplerType == AnimSamplerType::Float4x4) {
				assert(driver._curveIndex < _curves.size());
				const RawAnimationCurve& curve = _curves[driver._curveIndex];
				assert(PtrAdd(dst,sizeof(Float4x4)) <= outputBlock.end());
				*(Float4x4*)dst = curve.Calculate<Float4x4>(animState._time);
			} else if (driver._samplerType == AnimSamplerType::Float4) {
				assert(driver._curveIndex < _curves.size());
				const RawAnimationCurve& curve = _curves[driver._curveIndex];
				assert(PtrAdd(dst,sizeof(Float4)) <= outputBlock.end());
				*(Float4*)dst = curve.Calculate<Float4>(animState._time);
			} else if (driver._samplerType == AnimSamplerType::Quaternion) {
				assert(driver._curveIndex < _curves.size());
				const RawAnimationCurve& curve = _curves[driver._curveIndex];
				assert(PtrAdd(dst, sizeof(Quaternion)) <= outputBlock.end());
				*(Quaternion*)dst = curve.Calculate<Quaternion>(animState._time);
			} else if (driver._samplerType == AnimSamplerType::Float3) {
				assert(driver._curveIndex < _curves.size());
				const RawAnimationCurve& curve = _curves[driver._curveIndex];
				assert(PtrAdd(dst,sizeof(Float3)) <= outputBlock.end());
				*(Float3*)dst = curve.Calculate<Float3>(animState._time);
			} else if (driver._samplerType == AnimSamplerType::Float1) {
				assert(driver._curveIndex < _curves.size());
				const RawAnimationCurve& curve = _curves[driver._curveIndex];
				assert(PtrAdd(dst,sizeof(float)) <= outputBlock.end());
				*(float*)dst = curve.Calculate<float>(animState._time);
			}
		}

		for (   size_t c=constantDriverStartIndex; c<constantDriverEndIndex; ++c) {
			const ConstantDriver& driver = _constantDrivers[c];
			auto& br = bindingRules[driver._parameterIndex];
			if (br._outputOffset  == ~0x0) continue;   // (unbound output)

			assert(br._samplerType == driver._samplerType);
			const void* data = PtrAdd(_constantData.begin(), driver._dataOffset);
			auto* dst = PtrAdd(outputBlock.begin(), br._outputOffset);

			if (driver._samplerType == AnimSamplerType::Float4x4) {
				*(Float4x4*)dst = *(const Float4x4*)data;
			} else if (driver._samplerType == AnimSamplerType::Float4) {
				*(Float4*)dst = *(const Float4*)data;
			} else if (driver._samplerType == AnimSamplerType::Quaternion) {
				if (driver._format == Format::R12G12B12A4_SNORM) {
					*(Quaternion*)dst = Decompress_36bit(data);
				} else {
					assert(driver._format == Format::R32G32B32A32_FLOAT);
					*(Quaternion*)dst = *(const Quaternion*)data;
				}
			} else if (driver._samplerType == AnimSamplerType::Float3) {
				*(Float3*)dst = *(const Float3*)data;
			} else if (driver._samplerType == AnimSamplerType::Float1) {
				*(float*)dst = *(const float*)data;
			}
		}
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

	const char* AsString(AnimSamplerType value)
	{
		switch (value) {
		case AnimSamplerType::Float1: return "Float1";
		case AnimSamplerType::Float3: return "Float3";
		case AnimSamplerType::Float4: return "Float4";
		case AnimSamplerType::Quaternion: return "Quaternion";
		case AnimSamplerType::Float4x4: return "Float4x4";
		}
		return "<<unknown>>";
	}

	const char* AsString(AnimSamplerComponent value)
	{
		switch (value) {
		case AnimSamplerComponent::None: return "None";
		case AnimSamplerComponent::Translation: return "Translation";
		case AnimSamplerComponent::Rotation: return "Rotation";
		case AnimSamplerComponent::Scale: return "Scale";
		case AnimSamplerComponent::FullTransform: return "FullTransform";
		}
		return "<<unknown>>";
	}

///////////////////////////////////////////////////////////////////////////////////////////////////

#if 0
	void AnimatedParameterSet::Set(uint32 index, float p) 
	{
		if (_float1Parameters.size() < (index+1)) _float1Parameters.resize(index+1, 0.f);
		_float1Parameters[index] = p;
	}

	void AnimatedParameterSet::Set(uint32 index, Float3 p)
	{
		if (_float3Parameters.size() < (index+1)) _float3Parameters.resize(index+1, Zero<Float3>());
		_float3Parameters[index] = p;
	}

	void AnimatedParameterSet::Set(uint32 index, Float4 p)
	{
		if (_float4Parameters.size() < (index+1)) _float4Parameters.resize(index+1, Zero<Float4>());
		_float4Parameters[index] = p;
	}

	void AnimatedParameterSet::Set(uint32 index, Quaternion q)
	{
		// note that packing here must agree with how we unpack in TransformationCommands.cpp
		// The "Order" parameter we use with cml::quaternion is significant. Here, we're assuming
		// "scalar_first" mode
		static_assert(std::is_same_v<Quaternion::order_type, cml::scalar_first>, "Unexpected quaternion ordering");
		Float4 float4Form { q.real(), q.imaginary()[0], q.imaginary()[1], q.imaginary()[2] };
		Set(index, float4Form);
	}

	void AnimatedParameterSet::Set(uint32 index, const Float4x4& p)
	{
		if (_float4x4Parameters.size() < (index+1)) _float4x4Parameters.resize(index+1, Zero<Float4x4>());
		_float4x4Parameters[index] = p;
	}

	AnimatedParameterSet::AnimatedParameterSet() {}

	AnimatedParameterSet::AnimatedParameterSet(AnimatedParameterSet&& moveFrom)
	:       _float4x4Parameters(    std::move(moveFrom._float4x4Parameters))
	,       _float4Parameters(      std::move(moveFrom._float4Parameters))
	,       _float3Parameters(      std::move(moveFrom._float3Parameters))
	,       _float1Parameters(      std::move(moveFrom._float1Parameters))
	{}

	AnimatedParameterSet& AnimatedParameterSet::operator=(AnimatedParameterSet&& moveFrom)
	{
		_float4x4Parameters = std::move(moveFrom._float4x4Parameters);
		_float4Parameters   = std::move(moveFrom._float4Parameters);
		_float3Parameters   = std::move(moveFrom._float3Parameters);
		_float1Parameters   = std::move(moveFrom._float1Parameters);
		return *this;
	}

	AnimatedParameterSet::AnimatedParameterSet(const AnimatedParameterSet& copyFrom)
	:       _float4x4Parameters(copyFrom._float4x4Parameters)
	,       _float4Parameters(copyFrom._float4Parameters)
	,       _float3Parameters(copyFrom._float3Parameters)
	,       _float1Parameters(copyFrom._float1Parameters)
	{
	}

	AnimatedParameterSet&  AnimatedParameterSet::operator=(const AnimatedParameterSet& copyFrom)
	{
		_float4x4Parameters = copyFrom._float4x4Parameters;
		_float4Parameters = copyFrom._float4Parameters;
		_float3Parameters = copyFrom._float3Parameters;
		_float1Parameters = copyFrom._float1Parameters;
		return *this;
	}

	void    AnimatedParameterSet::SerializeMethod(::Assets::NascentBlockSerializer& outputSerializer) const
	{
		SerializationOperator(outputSerializer, _float4x4Parameters);
		SerializationOperator(outputSerializer, _float4Parameters);
		SerializationOperator(outputSerializer, _float3Parameters);
		SerializationOperator(outputSerializer, _float1Parameters);
	}
#endif

}}

