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
		size_t constantDriverStart = 0, constantDriverEnd = 0;
		if (animState._animation!=0x0) {
			auto i = std::lower_bound(_animations.begin(), _animations.end(), animState._animation, CompareFirst<uint64_t, Animation>());
			if (i!=_animations.end() && i->first == animState._animation) {
				driverStart = i->second._beginDriver;
				driverEnd = i->second._endDriver;
				constantDriverStart = i->second._beginConstantDriver;
				constantDriverEnd = i->second._endConstantDriver;
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

		for (   size_t c=constantDriverStart; c<constantDriverEnd; ++c) {
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

	std::optional<AnimationSet::Animation> AnimationSet::FindAnimation(uint64_t animation) const
	{
		auto i = std::lower_bound(
			_animations.begin(), _animations.end(),
			animation, CompareFirst<uint64_t, Animation>());
		if (i!=_animations.end() && i->first == animation)
			return i->second;
		return {};
	}

	unsigned                AnimationSet::FindParameter(uint64_t parameterName, AnimSamplerComponent component) const
	{
		for (size_t c=0; c<_outputInterface.size(); ++c)
			if (_outputInterface[c]._name == parameterName && _outputInterface[c]._component == component)
				return unsigned(c);
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

	void SerializationOperator(::Assets::BlockSerializer& serializer, const AnimationSet& obj)
	{
		SerializationOperator(serializer, obj._animationDrivers);
		SerializationOperator(serializer, obj._constantDrivers);
		SerializationOperator(serializer, obj._constantData);
		SerializationOperator(serializer, obj._animations);
		SerializationOperator(serializer, obj._outputInterface);
		SerializationOperator(serializer, obj._curves);
		SerializationOperator(serializer, obj._stringNameBlockOffsets);
		SerializationOperator(serializer, obj._stringNameBlock);
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

}}

