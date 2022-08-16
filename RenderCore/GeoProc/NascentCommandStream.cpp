// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "NascentCommandStream.h"
#include "../Format.h"
#include "../Assets/RawAnimationCurve.h"
#include "../Assets/TransformationCommands.h"
#include "../Assets/ModelMachine.h"
#include "../Assets/AssetUtils.h"
#include "../../Assets/BlockSerializer.h"
#include "../../Assets/AssetsCore.h"
#include "../../OSServices/Log.h"
#include "../../Utility/MemoryUtils.h"
#include "../../Utility/StringUtils.h"
#include "../../Utility/IteratorUtils.h"

namespace RenderCore { namespace Assets { namespace GeoProc
{ 
	NascentAnimationSet::StringOrHash::StringOrHash(const std::string& str) : _stringForm(str), _hashForm(Hash64(str)) {}
	NascentAnimationSet::StringOrHash::StringOrHash(uint64_t h) : _hashForm(h) {}
	bool operator==(const NascentAnimationSet::StringOrHash& lhs, const NascentAnimationSet::StringOrHash& rhs) { return lhs._hashForm == rhs._hashForm; }

	unsigned    NascentAnimationSet::AddParameter(StringOrHash parameterName, AnimSamplerComponent parameterComponent, AnimSamplerType samplerType)
	{
		size_t parameterIndex = _parameterInterfaceDefinition.size();
		auto i = std::find_if( 
			_parameterInterfaceDefinition.cbegin(), _parameterInterfaceDefinition.cend(), 
			[parameterName, parameterComponent](const auto& q) { return q._name == parameterName && q._component == parameterComponent; });
		if (i!=_parameterInterfaceDefinition.end()) {
			assert(i->_samplerType == samplerType);
			parameterIndex = (unsigned)std::distance(_parameterInterfaceDefinition.cbegin(), i);
		} else {
			_parameterInterfaceDefinition.push_back({parameterName, parameterComponent, samplerType});
		}
		return parameterIndex;
	}

	void    NascentAnimationSet::NascentBlock::AddConstantDriver( 
									StringOrHash  			parameterName, 
									AnimSamplerComponent    parameterComponent,
									AnimSamplerType         samplerType,
									const void*         	constantValue, 
									size_t					valueSize,
									Format					format)
	{
		auto parameterIndex = _animSet->AddParameter(parameterName, parameterComponent, samplerType);

		// Expecting a single value -- it should match the bits per pixel value
		// associated with the given format
		assert(unsigned(valueSize) == RenderCore::BitsPerPixel(format)/8);

		unsigned dataOffset = unsigned(_animSet->_constantData.size());
		std::copy(
			(uint8_t*)constantValue, PtrAdd((uint8_t*)constantValue, valueSize),
			std::back_inserter(_animSet->_constantData));
		_animSet->_constantDrivers.push_back({dataOffset, (unsigned)parameterIndex, format});
		_animSet->AppendConstantDriverToBlock(_blockIdx, _animSet->_constantDrivers.size()-1);
	}

	void    NascentAnimationSet::NascentBlock::AddAnimationDriver( 
		StringOrHash parameterName, 
		AnimSamplerComponent parameterComponent,
		AnimSamplerType samplerType,
		unsigned curveId,
		CurveInterpolationType interpolationType)
	{
		auto parameterIndex = _animSet->AddParameter(parameterName, parameterComponent, samplerType);
		_animSet->_animationDrivers.push_back({curveId, (unsigned)parameterIndex, interpolationType});
		_animSet->AppendAnimationDriverToBlock(_blockIdx, _animSet->_animationDrivers.size()-1);
	}

	void NascentAnimationSet::AppendAnimationDriverToBlock(unsigned blockIdx, unsigned driverIdx)
	{
		auto& block = _animationBlocks[blockIdx];
		if (block._beginDriver == block._endDriver) {
			block._beginDriver = driverIdx;
			block._endDriver = driverIdx+1;
		} else {
			assert(block._endDriver == driverIdx);		// must be appended in order
			++block._endDriver;
		}
	}

    void NascentAnimationSet::AppendConstantDriverToBlock(unsigned blockIdx, unsigned driverIdx)
	{
		auto& block = _animationBlocks[blockIdx];
		if (block._beginConstantDriver == block._endConstantDriver) {
			block._beginConstantDriver = driverIdx;
			block._endConstantDriver = driverIdx+1;
		} else {
			assert(block._endConstantDriver == driverIdx);		// must be appended in order
			++block._endConstantDriver;
		}
	}

	unsigned NascentAnimationSet::GetParameterIndex(const std::string& parameterName, AnimSamplerComponent parameterComponent) const
	{
		auto i2 = std::find_if( 
			_parameterInterfaceDefinition.cbegin(), _parameterInterfaceDefinition.cend(), 
			[parameterName, parameterComponent](const auto& q) { return q._name == parameterName && q._component == parameterComponent; });
		if (i2==_parameterInterfaceDefinition.end()) 
			return ~0u;
		return (unsigned)std::distance(_parameterInterfaceDefinition.cbegin(), i2);
	}

	bool    NascentAnimationSet::HasAnimationDriver(StringOrHash parameterName) const
	{
		for (auto i2=_parameterInterfaceDefinition.cbegin(); i2!=_parameterInterfaceDefinition.cend(); ++i2) {
			if (!(i2->_name == parameterName)) continue;

			auto parameterIndex = (unsigned)std::distance(_parameterInterfaceDefinition.cbegin(), i2);

			for (auto i=_animationDrivers.begin(); i!=_animationDrivers.end(); ++i) {
				if (i->_parameterIndex == parameterIndex)
					return true;
			}

			for (auto i=_constantDrivers.begin(); i!=_constantDrivers.end(); ++i) {
				if (i->_parameterIndex == parameterIndex)
					return true;
			}
		}
		return false;
	}

	void    NascentAnimationSet::MergeInAsManyAnimations(const NascentAnimationSet& copyFrom, const std::string& namePrefix)
	{
		std::vector<unsigned> parameterRemapping;
		parameterRemapping.reserve(copyFrom._parameterInterfaceDefinition.size());
		for (const auto&p:copyFrom._parameterInterfaceDefinition) {
			auto i2 = std::find_if( 
				_parameterInterfaceDefinition.cbegin(), _parameterInterfaceDefinition.cend(), 
				[parameterName=p._name, parameterComponent=p._component](const auto& q) { return q._name == parameterName && q._component == parameterComponent; });

			if (i2 != _parameterInterfaceDefinition.cend()) {
				assert(i2->_samplerType == p._samplerType);
				parameterRemapping.push_back(unsigned(i2-_parameterInterfaceDefinition.cbegin()));
			} else {
				parameterRemapping.push_back(unsigned(_parameterInterfaceDefinition.size()));
				_parameterInterfaceDefinition.push_back(p);
			}
		}

		size_t curveOffset = _curves.size();
		_curves.insert(_curves.end(), copyFrom._curves.begin(), copyFrom._curves.end());
		size_t dataOffset = _constantData.size();
		_constantData.insert(_constantData.end(), copyFrom._constantData.begin(), copyFrom._constantData.end());

		size_t constantDriverOffset = _constantDrivers.size();
		_constantDrivers.reserve(_constantDrivers.size()+copyFrom._constantDrivers.size());
		for (const auto&d:copyFrom._constantDrivers) {
			_constantDrivers.push_back(
				ConstantDriver {
					unsigned(dataOffset + d._dataOffset),
					parameterRemapping[d._parameterIndex],
					d._format
				});
		}

		size_t animationDriverOffset = _animationDrivers.size();
		_animationDrivers.reserve(_animationDrivers.size()+copyFrom._animationDrivers.size());
		for (const auto&d:copyFrom._animationDrivers) {
			_animationDrivers.push_back(
				AnimationDriver {
					unsigned(curveOffset + d._curveIndex),
					parameterRemapping[d._parameterIndex],
					d._interpolationType
				});
		}

		size_t animationBlockOffset = _animationBlocks.size();
		_animationBlocks.reserve(_animationBlocks.size()+copyFrom._animationBlocks.size());
		for (const auto&a:copyFrom._animationBlocks) {
			auto newBlock = a;
			if (newBlock._beginDriver != newBlock._endDriver) {
				newBlock._beginDriver += (unsigned)animationDriverOffset;
				newBlock._endDriver += (unsigned)animationDriverOffset;
			}
			if (newBlock._beginConstantDriver != newBlock._endConstantDriver) {
				newBlock._beginConstantDriver += (unsigned)constantDriverOffset;
				newBlock._endConstantDriver += (unsigned)constantDriverOffset;
			}
			_animationBlocks.push_back(newBlock);
		}

		_animations.reserve(_animations.size()+copyFrom._animations.size());
		for (const auto&a:copyFrom._animations) {
			auto newAnim = a.second;
			newAnim._startBlock += animationBlockOffset;
			newAnim._endBlock += animationBlockOffset;
			_animations.push_back(std::make_pair(namePrefix+a.first, newAnim));
		}
	}

	void	NascentAnimationSet::MakeIndividualAnimation(const std::string& name, float framesPerSecond)
	{
		// Make an Animation record that covers all of the curves registered.
		// This is intended for cases where there's only a single animation within the NascentAnimationSet
		unsigned minFrame = std::numeric_limits<unsigned>::max(), maxFrame = 0;
		for (auto i=_animationDrivers.cbegin(); i!=_animationDrivers.end(); ++i) {
			if (i->_curveIndex >= _curves.size()) continue;
			const auto* animCurve = &_curves[i->_curveIndex];
			if (animCurve) {
				minFrame = std::min(minFrame, (unsigned)animCurve->TimeAtFirstKeyframe());
				maxFrame = std::max(maxFrame, (unsigned)animCurve->TimeAtLastKeyframe());
			}
		}

		_animationBlocks.push_back(
			AnimationBlock{
				(unsigned)0, (unsigned)_animationDrivers.size(), 
				(unsigned)0, (unsigned)_constantDrivers.size(),
				minFrame, maxFrame+1});

		_animations.push_back(
			std::make_pair(
				name,
				Animation{
					(unsigned)_animationBlocks.size()-1,
					(unsigned)_animationBlocks.size(),
					framesPerSecond}));
	}

	auto NascentAnimationSet::AddAnimation(const std::string& name, IteratorRange<const BlockSpan*> blocks, float framesPerSecond) -> std::vector<NascentBlock>
	{
		assert(!blocks.empty());
		assert(framesPerSecond != 0.f);
		Animation newAnimation;
		newAnimation._startBlock = _animationBlocks.size();
		newAnimation._endBlock = _animationBlocks.size() + blocks.size();
		newAnimation._framesPerSecond = framesPerSecond;
		_animations.emplace_back(name, newAnimation);

		_animationBlocks.reserve(_animationBlocks.size() + blocks.size());
		for (auto b:blocks)
			_animationBlocks.push_back(AnimationBlock{0, 0, 0, 0, b._beginFrame, b._endFrame});

		std::vector<NascentBlock> result;
		result.reserve(blocks.size());
		for (unsigned c=0; c<blocks.size(); ++c)
			result.push_back(NascentBlock{*this, newAnimation._startBlock+c});
		return result;
	}

	unsigned NascentAnimationSet::NascentBlock::AddCurve(RenderCore::Assets::RawAnimationCurve&& curve)
	{
		return _animSet->AddCurve(std::move(curve));
	}

	unsigned NascentAnimationSet::AddCurve(RenderCore::Assets::RawAnimationCurve&& curve)
	{
		auto result = (unsigned)_curves.size();
		_curves.emplace_back(std::move(curve));
		return result;
	}

	void SerializationOperator(::Assets::BlockSerializer& serializer, const NascentAnimationSet& obj)
	{
		AnimationSet finalAnimationSet;
		finalAnimationSet._animationDrivers.insert(finalAnimationSet._animationDrivers.end(), obj._animationDrivers.begin(), obj._animationDrivers.end());
		finalAnimationSet._constantDrivers.insert(finalAnimationSet._constantDrivers.end(), obj._constantDrivers.begin(), obj._constantDrivers.end());

		finalAnimationSet._animations.reserve(obj._animations.size());
		for (const auto&a:obj._animations)
			finalAnimationSet._animations.push_back(std::make_pair(Hash64(a.first), a.second));
		std::sort(finalAnimationSet._animations.begin(), finalAnimationSet._animations.end(), CompareFirst<uint64_t, AnimationSet::Animation>());
		finalAnimationSet._animationBlocks.insert(finalAnimationSet._animationBlocks.end(), obj._animationBlocks.begin(), obj._animationBlocks.end());

		finalAnimationSet._outputInterface.reserve(obj._parameterInterfaceDefinition.size());
		for (unsigned c=0; c<obj._parameterInterfaceDefinition.size(); ++c) {
			auto& p = obj._parameterInterfaceDefinition[c];
			finalAnimationSet._outputInterface.push_back({p._name._hashForm, p._component, p._samplerType});
		}

		finalAnimationSet._curves.insert(finalAnimationSet._curves.end(), obj._curves.begin(), obj._curves.end());

		finalAnimationSet._constantData.insert(finalAnimationSet._constantData.end(), obj._constantData.begin(), obj._constantData.end());

		// Construct the string name block (note that we have write the names in their final sorted order)
		for (const auto&a:finalAnimationSet._animations) {
			std::string srcName;
			for (const auto&src:obj._animations)
				if (a.first == Hash64(src.first)) {
					srcName = src.first;
					break;
				}
			finalAnimationSet._stringNameBlockOffsets.push_back((unsigned)finalAnimationSet._stringNameBlock.size());
			finalAnimationSet._stringNameBlock.insert(finalAnimationSet._stringNameBlock.end(), srcName.begin(), srcName.end());
		}
		finalAnimationSet._stringNameBlockOffsets.push_back((unsigned)finalAnimationSet._stringNameBlock.size());
		
		SerializationOperator(serializer, finalAnimationSet);
	}

	static std::ostream& SerializationOperator(
		std::ostream& stream, 
		const NascentAnimationSet::StringOrHash& animSet)
	{
		if (animSet._stringForm) stream << animSet._stringForm.value();
		else stream << animSet._hashForm;
		return stream;
	}

	std::ostream& SerializationOperator(
		std::ostream& stream, 
		const NascentAnimationSet& animSet)
	{
		// write out some metrics / debugging information
		stream << "--- Output animation parameters (" << animSet._parameterInterfaceDefinition.size() << ")" << std::endl;
		for (unsigned c=0; c<animSet._parameterInterfaceDefinition.size(); ++c)
			stream << "[" << c << "] " << animSet._parameterInterfaceDefinition[c]._name << "[" << AsString(animSet._parameterInterfaceDefinition[c]._component) << "] " << AsString(animSet._parameterInterfaceDefinition[c]._samplerType) << std::endl;

		stream << "--- Animations (" << animSet._animations.size() << ")" << std::endl;
		for (unsigned c=0; c<animSet._animations.size(); ++c) {
			auto& anim = animSet._animations[c];
			stream << "[" << c << "] " << anim.first << " " << anim.second._framesPerSecond << " fps ";
			for (unsigned b=anim.second._startBlock; b!=anim.second._endBlock; ++b) {
				auto& block = animSet._animationBlocks[b];
				stream << " block {" << block._beginFrame << " to " << block._endFrame << "}";
			}
			stream << std::endl;
		}

		stream << "--- Animations drivers (" << animSet._animationDrivers.size() << ")" << std::endl;
		for (unsigned c=0; c<animSet._animationDrivers.size(); ++c) {
			auto& driver = animSet._animationDrivers[c];
			stream << "[" << c << "] Curve index: " << driver._curveIndex << " Parameter index: " << driver._parameterIndex << " (" 
				<< animSet._parameterInterfaceDefinition[driver._parameterIndex]._name << "[" << AsString(animSet._parameterInterfaceDefinition[driver._parameterIndex]._component) << "]" 
				<< ") interpolation: " << AsString(driver._interpolationType) << std::endl;
		}

		stream << "--- Constant drivers (" << animSet._constantDrivers.size() << ")" << std::endl;
		for (unsigned c=0; c<animSet._constantDrivers.size(); ++c) {
			auto& driver = animSet._constantDrivers[c];
			stream << "[" << c << "] Parameter index: " << driver._parameterIndex << " (" 
				<< animSet._parameterInterfaceDefinition[driver._parameterIndex]._name << "[" << AsString(animSet._parameterInterfaceDefinition[driver._parameterIndex]._component) << "]" 
				<< ")" << std::endl;
		}

		return stream;
	}




	void	NascentSkeleton::WriteStaticTransform(const Transform& transform)
	{
		unsigned cmdCount = 0;
		auto cmds = TransformToCmds(transform, cmdCount);
		if (cmdCount)
			_skeletonMachine.PushCommand(cmds.data(), cmds.size()*sizeof(uint32_t));
	}

	std::vector<uint32_t> NascentSkeleton::TransformToCmds(const Transform& transform, unsigned& cmdCount)
	{
		std::vector<uint32_t> result;
		result.reserve(32);
		if (transform._fullTransform) {
			assert(!transform._translation && !transform._rotationAsQuaternion && !transform._arbitraryScale && !transform._uniformScale);
			result.push_back((uint32_t)TransformCommand::TransformFloat4x4_Static);
			result.insert(result.end(), (const uint32_t*)&transform._fullTransform.value(), (const uint32_t*)(&transform._fullTransform.value()+1));
			++cmdCount;
			return result;
		}

		if (transform._translation) {
			result.push_back((uint32_t)TransformCommand::Translate_Static);
			result.insert(result.end(), (const uint32_t*)&transform._translation.value(), (const uint32_t*)(&transform._translation.value()+1));
			++cmdCount;
		}

		if (transform._rotationAsQuaternion) {
			result.push_back((uint32_t)TransformCommand::RotateQuaternion_Static);
			result.insert(result.end(), (const uint32_t*)&transform._rotationAsQuaternion.value(), (const uint32_t*)(&transform._rotationAsQuaternion.value()+1));
			++cmdCount;
		} else if (transform._rotationAsAxisAngle) {
			result.push_back((uint32_t)TransformCommand::RotateAxisAngle_Static);
			result.insert(result.end(), (const uint32_t*)&transform._rotationAsAxisAngle.value(), (const uint32_t*)(&transform._rotationAsAxisAngle.value()+1));
			++cmdCount;
		}

		if (transform._arbitraryScale) {
			assert(!transform._uniformScale);
			result.push_back((uint32_t)TransformCommand::ArbitraryScale_Static);
			result.insert(result.end(), (const uint32_t*)&transform._arbitraryScale.value(), (const uint32_t*)(&transform._arbitraryScale.value()+1));
			++cmdCount;
		} else if (transform._uniformScale) {
			result.push_back((uint32_t)TransformCommand::UniformScale_Static);
			result.insert(result.end(), (const uint32_t*)&transform._uniformScale.value(), (const uint32_t*)(&transform._uniformScale.value()+1));
			++cmdCount;
		}
		return result;
	}

	void	NascentSkeleton::WriteParameterizedTransform(StringSection<> parameterName, const Transform& transform)
	{
		auto hashName = Hash64(parameterName);
		auto i = LowerBound(_skeletonMachine._parameterDehashTable, hashName);
		if (i == _skeletonMachine._parameterDehashTable.end() || i->first != hashName)
			_skeletonMachine._parameterDehashTable.insert(i, std::make_pair(hashName, parameterName.AsString()));

		unsigned cmdCount = 0;
		auto cmds = TransformToCmds(transform, cmdCount);
		_skeletonMachine.PushCommand(TransformCommand((unsigned)TransformCommand::BindingPoint_0 + cmdCount));
		_skeletonMachine.PushCommand(&hashName, sizeof(hashName));
		if (cmdCount)
			_skeletonMachine.PushCommand(cmds.data(), cmds.size()*sizeof(uint32_t));
	}

	void	NascentSkeleton::WriteOutputMarker(StringSection<> skeletonName, StringSection<> jointName)
	{
		_skeletonMachine.WriteOutputMarker(skeletonName, jointName);
	}

	void	NascentSkeleton::WritePushLocalToWorld()
	{
		_skeletonMachine.PushCommand(TransformCommand::PushLocalToWorld);
	}

	void	NascentSkeleton::WritePopLocalToWorld(unsigned popCount)
	{
		_skeletonMachine.Pop(popCount);
	}

	void SerializationOperator(::Assets::BlockSerializer& serializer, const NascentSkeleton& obj)
	{
		SerializationOperator(serializer, obj._skeletonMachine);
	}

	NascentSkeleton::Transform::Transform(const Float4x4& matrix) : _fullTransform(matrix) {}
	NascentSkeleton::Transform::Transform(const Float3& translation, const Quaternion& rotation, float scale)
	: _translation(translation), _rotationAsQuaternion(rotation), _uniformScale(scale)
	{}

}}}
