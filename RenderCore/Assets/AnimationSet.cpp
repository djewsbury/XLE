// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "AnimationSet.h"
#include "RawAnimationCurve.h"
#include "../../Assets/BlockSerializer.h"
#include "../../Math/Matrix.h"
#include "../../Math/Quaternion.h"
#include "../../Math/Vector.h"
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

		float timeInFramesFromBlockBegin = 0.f;
		size_t driverStart = 0, driverEnd = 0;
		size_t constantDriverStart = 0, constantDriverEnd = 0;
		if (animState._animation!=0x0) {
			auto i = std::lower_bound(_animations.begin(), _animations.end(), animState._animation, CompareFirst<uint64_t, Animation>());
			if (i!=_animations.end() && i->first == animState._animation) {
				timeInFramesFromBlockBegin = animState._time * i->second._framesPerSecond;
				auto b = i->second._startBlock;
				while ((b+1) != i->second._endBlock && timeInFramesFromBlockBegin >= _animationBlocks[b+1]._beginFrame) ++b;
				timeInFramesFromBlockBegin -= _animationBlocks[b]._beginFrame;	// we can end up with a negative here if there's a gap between blocks
				driverStart = _animationBlocks[b]._beginDriver;
				driverEnd = _animationBlocks[b]._endDriver;
				constantDriverStart = _animationBlocks[b]._beginConstantDriver;
				constantDriverEnd = _animationBlocks[b]._endConstantDriver;
				// Note that we never interpolate between blocks. We're assuming that the first & last keyframes from each block are duplicated
				// in the surrounding blocks (likewise first & last keyframes in the animation should be identical in looping animations)
			}
		}

		for (size_t c=driverStart; c<driverEnd; ++c) {
			const AnimationDriver& driver = _animationDrivers[c];
			auto& br = bindingRules[driver._parameterIndex];
			if (br._outputOffset  == ~0x0) continue;   // (unbound output)

			auto* dst = PtrAdd(outputBlock.begin(), br._outputOffset);

			if (br._samplerType == AnimSamplerType::Float4x4) {
				assert(driver._curveIndex < _curves.size());
				const RawAnimationCurve& curve = _curves[driver._curveIndex];
				assert(PtrAdd(dst,sizeof(Float4x4)) <= outputBlock.end());
				*(Float4x4*)dst = curve.Calculate<Float4x4>(timeInFramesFromBlockBegin, driver._interpolationType);
			} else if (br._samplerType == AnimSamplerType::Float4) {
				assert(driver._curveIndex < _curves.size());
				const RawAnimationCurve& curve = _curves[driver._curveIndex];
				assert(PtrAdd(dst,sizeof(Float4)) <= outputBlock.end());
				*(Float4*)dst = curve.Calculate<Float4>(timeInFramesFromBlockBegin, driver._interpolationType);
			} else if (br._samplerType == AnimSamplerType::Quaternion) {
				assert(driver._curveIndex < _curves.size());
				const RawAnimationCurve& curve = _curves[driver._curveIndex];
				assert(PtrAdd(dst, sizeof(Quaternion)) <= outputBlock.end());
				*(Quaternion*)dst = curve.Calculate<Quaternion>(timeInFramesFromBlockBegin, driver._interpolationType);
			} else if (br._samplerType == AnimSamplerType::Float3) {
				assert(driver._curveIndex < _curves.size());
				const RawAnimationCurve& curve = _curves[driver._curveIndex];
				assert(PtrAdd(dst,sizeof(Float3)) <= outputBlock.end());
				*(Float3*)dst = curve.Calculate<Float3>(timeInFramesFromBlockBegin, driver._interpolationType);
			} else if (br._samplerType == AnimSamplerType::Float1) {
				assert(driver._curveIndex < _curves.size());
				const RawAnimationCurve& curve = _curves[driver._curveIndex];
				assert(PtrAdd(dst,sizeof(float)) <= outputBlock.end());
				*(float*)dst = curve.Calculate<float>(timeInFramesFromBlockBegin, driver._interpolationType);
			}
		}

		for (size_t c=constantDriverStart; c<constantDriverEnd; ++c) {
			const ConstantDriver& driver = _constantDrivers[c];
			auto& br = bindingRules[driver._parameterIndex];
			if (br._outputOffset  == ~0x0) continue;   // (unbound output)

			const void* data = PtrAdd(_constantData.begin(), driver._dataOffset);
			auto* dst = PtrAdd(outputBlock.begin(), br._outputOffset);

			if (br._samplerType == AnimSamplerType::Float4x4) {
				assert(driver._format == Format::Matrix4x4);
				*(Float4x4*)dst = *(const Float4x4*)data;
			} else if (br._samplerType == AnimSamplerType::Float4) {
				assert(driver._format == Format::R32G32B32A32_FLOAT);
				*(Float4*)dst = *(const Float4*)data;
			} else if (br._samplerType == AnimSamplerType::Quaternion) {
				if (driver._format == Format::R12G12B12A4_SNORM) {
					*(Quaternion*)dst = Decompress_36bit(data);
				} else {
					assert(driver._format == Format::R32G32B32A32_FLOAT);
					*(Quaternion*)dst = *(const Quaternion*)data;
				}
			} else if (br._samplerType == AnimSamplerType::Float3) {
				assert(driver._format == Format::R32G32B32_FLOAT);
				*(Float3*)dst = *(const Float3*)data;
			} else if (br._samplerType == AnimSamplerType::Float1) {
				assert(driver._format == Format::R32_FLOAT);
				*(float*)dst = *(const float*)data;
			}
		}
	}

	std::optional<AnimationSet::AnimationQuery> AnimationSet::FindAnimation(uint64_t animation) const
	{
		auto i = std::lower_bound(
			_animations.begin(), _animations.end(),
			animation, CompareFirst<uint64_t, Animation>());
		if (i!=_animations.end() && i->first == animation) {
			AnimationQuery result;
			result._framesPerSecond = i->second._framesPerSecond;
			if (i->second._startBlock != i->second._endBlock) {
				// assuming start frame is zero for this duration
				result._durationInFrames = _animationBlocks[(i->second._endBlock-1)]._endFrame-1;
			} else
				result._durationInFrames = 0;

			auto idx = std::distance(_animations.begin(), i);
			result._stringName = MakeStringSection(
				_stringNameBlock.begin() + _stringNameBlockOffsets[idx],
				_stringNameBlock.begin() + _stringNameBlockOffsets[idx+1]);
			return result;
		}
		return {};
	}

	unsigned AnimationSet::FindParameter(uint64_t parameterName, AnimSamplerComponent component) const
	{
		for (size_t c=0; c<_outputInterface.size(); ++c)
			if (_outputInterface[c]._name == parameterName && _outputInterface[c]._component == component)
				return unsigned(c);
		return ~unsigned(0x0);
	}

	AnimationSet::AnimationSet() {}
	AnimationSet::~AnimationSet() {}

	void SerializationOperator(::Assets::BlockSerializer& serializer, const AnimationSet& obj)
	{
		SerializationOperator(serializer, obj._animationDrivers);
		SerializationOperator(serializer, obj._constantDrivers);
		SerializationOperator(serializer, obj._constantData);
		SerializationOperator(serializer, obj._animationBlocks);
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
		case AnimSamplerComponent::TranslationGeoSpace: return "TranslationGeoSpace";
		}
		return "<<unknown>>";
	}

	const char* AsString(CurveInterpolationType value)
	{
		switch (value) {
		case CurveInterpolationType::None: return "None";
		case CurveInterpolationType::Linear: return "Linear";
		case CurveInterpolationType::Bezier: return "Bezier";
		case CurveInterpolationType::Hermite: return "Hermite";
		case CurveInterpolationType::CatmullRom: return "CatmullRom";
		case CurveInterpolationType::NURBS: return "NURBS";
		}
		return "<<unknown>>";
	}

}}

