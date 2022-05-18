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

	void    NascentAnimationSet::AddConstantDriver( 
									StringOrHash  			parameterName, 
									AnimSamplerComponent    parameterComponent,
									const void*         	constantValue, 
									size_t					valueSize,
									Format					format,
									AnimSamplerType     	samplerType, 
									unsigned            	samplerOffset)
	{
		size_t parameterIndex = _parameterInterfaceDefinition.size();
		auto i = std::find_if( 
			_parameterInterfaceDefinition.cbegin(), _parameterInterfaceDefinition.cend(), 
			[parameterName, parameterComponent](const auto& q) { return q.first == parameterName && q.second == parameterComponent; });
		if (i!=_parameterInterfaceDefinition.end()) {
			parameterIndex = (unsigned)std::distance(_parameterInterfaceDefinition.cbegin(), i);
		} else {
			_parameterInterfaceDefinition.emplace_back(parameterName, parameterComponent);
		}

		// Expecting a single value -- it should match the bits per pixel value
		// associated with the given format
		assert(unsigned(valueSize) == RenderCore::BitsPerPixel(format)/8);

		unsigned dataOffset = unsigned(_constantData.size());
		std::copy(
			(uint8*)constantValue, PtrAdd((uint8*)constantValue, valueSize),
			std::back_inserter(_constantData));

		_constantDrivers.push_back({dataOffset, (unsigned)parameterIndex, format, samplerType, samplerOffset});
	}

	void    NascentAnimationSet::AddAnimationDriver( 
		StringOrHash parameterName, 
		AnimSamplerComponent parameterComponent,
		unsigned curveId, 
		AnimSamplerType samplerType, unsigned samplerOffset)
	{
		size_t parameterIndex = _parameterInterfaceDefinition.size();
		auto i = std::find_if( 
			_parameterInterfaceDefinition.cbegin(), _parameterInterfaceDefinition.cend(), 
			[parameterName, parameterComponent](const auto& q) { return q.first == parameterName && q.second == parameterComponent; });
		if (i!=_parameterInterfaceDefinition.end()) {
			parameterIndex = (unsigned)std::distance(_parameterInterfaceDefinition.cbegin(), i);
		} else {
			_parameterInterfaceDefinition.emplace_back(parameterName, parameterComponent);
		}

		_animationDrivers.push_back({curveId, (unsigned)parameterIndex, samplerType, samplerOffset});
	}

	unsigned NascentAnimationSet::GetParameterIndex(const std::string& parameterName, AnimSamplerComponent parameterComponent) const
	{
		auto i2 = std::find_if( 
			_parameterInterfaceDefinition.cbegin(), _parameterInterfaceDefinition.cend(), 
			[parameterName, parameterComponent](const auto& q) { return q.first == parameterName && q.second == parameterComponent; });
		if (i2==_parameterInterfaceDefinition.end()) 
			return ~0u;
		return (unsigned)std::distance(_parameterInterfaceDefinition.cbegin(), i2);
	}

	bool    NascentAnimationSet::HasAnimationDriver(StringOrHash parameterName) const
	{
		for (auto i2=_parameterInterfaceDefinition.cbegin(); i2!=_parameterInterfaceDefinition.cend(); ++i2) {
			if (!(i2->first == parameterName)) continue;

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

	void    NascentAnimationSet::MergeInAsAnIndividualAnimation(
		const NascentAnimationSet& copyFrom, const std::string& name)
	{
			//
			//      Merge the animation drivers in the given input animation, and give 
			//      them the supplied name
			//
		float minTime = std::numeric_limits<float>::max(), maxTime = -std::numeric_limits<float>::max();
		size_t startIndex = _animationDrivers.size();
		size_t constantStartIndex = _constantDrivers.size();
		for (auto i=copyFrom._animationDrivers.cbegin(); i!=copyFrom._animationDrivers.end(); ++i) {
			if (i->_curveIndex >= copyFrom._curves.size()) continue;
			const auto* animCurve = &copyFrom._curves[i->_curveIndex];
			if (animCurve) {
				float curveStart = animCurve->StartTime();
				float curveEnd = animCurve->EndTime();
				minTime = std::min(minTime, curveStart);
				maxTime = std::max(maxTime, curveEnd);

				auto param = copyFrom._parameterInterfaceDefinition[i->_parameterIndex];
				_curves.emplace_back(Assets::RawAnimationCurve(*animCurve));
				AddAnimationDriver(
					param.first, param.second, unsigned(_curves.size()-1), 
					i->_samplerType, i->_samplerOffset);
			}
		}

		for (auto i=copyFrom._constantDrivers.cbegin(); i!=copyFrom._constantDrivers.end(); ++i) {
			auto param = copyFrom._parameterInterfaceDefinition[i->_parameterIndex];
			AddConstantDriver(
				param.first, param.second, PtrAdd(AsPointer(copyFrom._constantData.begin()), i->_dataOffset), 
				BitsPerPixel(i->_format)/8, i->_format,
				i->_samplerType, i->_samplerOffset);
		}

		_animations.push_back(
			std::make_pair(
				name,
				Animation{
					(unsigned)startIndex, (unsigned)_animationDrivers.size(), 
					(unsigned)constantStartIndex, (unsigned)_constantDrivers.size(),
					minTime, maxTime}));
	}

	void    NascentAnimationSet::MergeInAsManyAnimations(const NascentAnimationSet& copyFrom, const std::string& namePrefix)
	{
		std::vector<unsigned> parameterRemapping;
		parameterRemapping.reserve(copyFrom._parameterInterfaceDefinition.size());
		for (const auto&p:copyFrom._parameterInterfaceDefinition) {
			auto i2 = std::find(_parameterInterfaceDefinition.cbegin(), _parameterInterfaceDefinition.cend(), p);
			if (i2 != _parameterInterfaceDefinition.cend()) {
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
					d._format,
					d._samplerType,
					d._samplerOffset
				});
		}

		size_t animationDriverOffset = _animationDrivers.size();
		_animationDrivers.reserve(_animationDrivers.size()+copyFrom._animationDrivers.size());
		for (const auto&d:copyFrom._animationDrivers) {
			_animationDrivers.push_back(
				AnimationDriver {
					unsigned(curveOffset + d._curveIndex),
					parameterRemapping[d._parameterIndex],
					d._samplerType,
					d._samplerOffset
				});
		}

		_animations.reserve(_animations.size()+copyFrom._animations.size());
		for (const auto&a:copyFrom._animations) {
			auto newAnim = a.second;
			if (newAnim._beginDriver != newAnim._endDriver) {
				newAnim._beginDriver += (unsigned)animationDriverOffset;
				newAnim._endDriver += (unsigned)animationDriverOffset;
			}
			if (newAnim._beginConstantDriver != newAnim._endConstantDriver) {
				newAnim._beginConstantDriver += (unsigned)constantDriverOffset;
				newAnim._endConstantDriver += (unsigned)constantDriverOffset;
			}
			_animations.push_back(std::make_pair(a.first, newAnim));
		}
	}

	void	NascentAnimationSet::MakeIndividualAnimation(const std::string& name)
	{
		// Make an Animation record that covers all of the curves registered.
		// This is intended for cases where there's only a single animation within the NascentAnimationSet
		float minTime = std::numeric_limits<float>::max(), maxTime = -std::numeric_limits<float>::max();
		for (auto i=_animationDrivers.cbegin(); i!=_animationDrivers.end(); ++i) {
			if (i->_curveIndex >= _curves.size()) continue;
			const auto* animCurve = &_curves[i->_curveIndex];
			if (animCurve) {
				float curveStart = animCurve->StartTime();
				float curveEnd = animCurve->EndTime();
				minTime = std::min(minTime, curveStart);
				maxTime = std::max(maxTime, curveEnd);
			}
		}

		_animations.push_back(
			std::make_pair(
				name,
				Animation{
					(unsigned)0, (unsigned)_animationDrivers.size(), 
					(unsigned)0, (unsigned)_constantDrivers.size(),
					minTime, maxTime}));
	}

	void	NascentAnimationSet::AddAnimation(
			const std::string& name, 
			unsigned driverBegin, unsigned driverEnd,
			unsigned constantBegin, unsigned constantEnd,
			float minTime, float maxTime)
	{
		_animations.push_back(
			std::make_pair(
				name,
				Animation{
					driverBegin, driverEnd, 
					constantBegin, constantEnd,
					minTime, maxTime}));
	}

	unsigned NascentAnimationSet::AddCurve(RenderCore::Assets::RawAnimationCurve&& curve)
	{
		auto result = (unsigned)_curves.size();
		_curves.emplace_back(std::move(curve));
		return result;
	}

	AnimSamplerType NascentAnimationSet::FindSamplerType(unsigned parameterIndex) const
	{
		std::optional<AnimSamplerType> result;

		for (const auto&d:_animationDrivers) {
			if (d._parameterIndex != parameterIndex) continue;
			if (result.value_or(d._samplerType) != d._samplerType)
				Throw(std::runtime_error("Different drivers use different sampler types for the same parameter found while serializing NascentAnimationSet"));
			result = d._samplerType;
		}

		for (const auto&d:_constantDrivers) {
			if (d._parameterIndex != parameterIndex) continue;
			if (result.value_or(d._samplerType) != d._samplerType)
				Throw(std::runtime_error("Different drivers use different sampler types for the same parameter found while serializing NascentAnimationSet"));
			result = d._samplerType;
		}

		if (!result)
			Throw(std::runtime_error("Redundant animation parameter found while serializing NascentAnimationSet"));
		return result.value();
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

		finalAnimationSet._outputInterface.reserve(obj._parameterInterfaceDefinition.size());
		for (unsigned c=0; c<obj._parameterInterfaceDefinition.size(); ++c) {
			auto samplerType = obj.FindSamplerType(c);
			auto& p = obj._parameterInterfaceDefinition[c];
			finalAnimationSet._outputInterface.push_back({p.first._hashForm, p.second, samplerType});
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
		const std::pair<NascentAnimationSet::StringOrHash, AnimSamplerComponent>& p)
	{
		return stream << p.first << "[" << AsString(p.second) << "]";
	}

	std::ostream& SerializationOperator(
		std::ostream& stream, 
		const NascentAnimationSet& animSet)
	{
		// write out some metrics / debugging information
		stream << "--- Output animation parameters (" << animSet._parameterInterfaceDefinition.size() << ")" << std::endl;
		for (unsigned c=0; c<animSet._parameterInterfaceDefinition.size(); ++c)
			stream << "[" << c << "] " << animSet._parameterInterfaceDefinition[c] << std::endl;

		stream << "--- Animations (" << animSet._animations.size() << ")" << std::endl;
		for (unsigned c=0; c<animSet._animations.size(); ++c) {
			auto& anim = animSet._animations[c];
			stream << "[" << c << "] " << anim.first << " " << anim.second._beginTime << " to " << anim.second._endTime << std::endl;
		}

		stream << "--- Animations drivers (" << animSet._animationDrivers.size() << ")" << std::endl;
		for (unsigned c=0; c<animSet._animationDrivers.size(); ++c) {
			auto& driver = animSet._animationDrivers[c];
			stream << "[" << c << "] Curve index: " << driver._curveIndex << " Parameter index: " << driver._parameterIndex << " (" << animSet._parameterInterfaceDefinition[driver._parameterIndex] << ") with sampler: " << AsString(driver._samplerType) << " and sampler offset " << driver._samplerOffset << std::endl;
		}

		stream << "--- Constant drivers (" << animSet._constantDrivers.size() << ")" << std::endl;
		for (unsigned c=0; c<animSet._constantDrivers.size(); ++c) {
			auto& driver = animSet._constantDrivers[c];
			stream << "[" << c << "] Parameter index: " << driver._parameterIndex << " (" << animSet._parameterInterfaceDefinition[driver._parameterIndex] << ") with sampler: " << AsString(driver._samplerType) << " and sampler offset " << driver._samplerOffset << std::endl;
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
		_dehashTable.emplace_back(hashName, parameterName.AsString());

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




	unsigned NascentModelCommandStream::RegisterInputInterfaceMarker(const std::string& skeleton, const std::string& name)
	{
		auto j = std::make_pair(skeleton, name);
		auto existing = std::find(_inputInterfaceNames.begin(), _inputInterfaceNames.end(), j);
		if (existing != _inputInterfaceNames.end()) {
			return (unsigned)std::distance(_inputInterfaceNames.begin(), existing);
		}

		auto result = (unsigned)_inputInterfaceNames.size();
		_inputInterfaceNames.push_back({skeleton, name});
		return result;
	}

	void NascentModelCommandStream::Add(GeometryInstance&& geoInstance)
	{
		_geometryInstances.emplace_back(std::move(geoInstance));
	}

	void NascentModelCommandStream::Add(CameraInstance&& camInstance)
	{
		_cameraInstances.emplace_back(std::move(camInstance));
	}

	void NascentModelCommandStream::Add(SkinControllerInstance&& skinControllerInstance)
	{
		_skinControllerInstances.emplace_back(std::move(skinControllerInstance));
	}

	void SerializationOperator(::Assets::BlockSerializer& serializer, const NascentModelCommandStream::GeometryInstance& obj)
	{
		SerializationOperator(serializer, obj._id);
		SerializationOperator(serializer, obj._localToWorldId);
		serializer.SerializeSubBlock(MakeIteratorRange(obj._materials));
		SerializationOperator(serializer, obj._materials.size());
		SerializationOperator(serializer, obj._levelOfDetail);
	}

	void SerializationOperator(::Assets::BlockSerializer& serializer, const NascentModelCommandStream::SkinControllerInstance& obj)
	{
		SerializationOperator(serializer, obj._id);
		SerializationOperator(serializer, obj._localToWorldId);
		serializer.SerializeSubBlock(MakeIteratorRange(obj._materials));
		SerializationOperator(serializer, obj._materials.size());
		SerializationOperator(serializer, obj._levelOfDetail);
	}

	void SerializationOperator(::Assets::BlockSerializer& serializer, const NascentModelCommandStream& obj, unsigned geoIdOffset)
	{
		// _geometryInstances & _skinControllerInstances are identical in their serialized form
		std::optional<unsigned> currentTransformMarker;
		const std::vector<NascentModelCommandStream::MaterialGuid>* currentMaterialAssignment = nullptr;
		for (const auto& geo:obj._geometryInstances) {
			if (!currentTransformMarker.has_value() || geo._localToWorldId != currentTransformMarker.value()) {
				serializer << MakeCmdAndRawData(ModelCommand::SetTransformMarker, geo._localToWorldId);
				currentTransformMarker = geo._localToWorldId;
			}
			if (!currentMaterialAssignment || *currentMaterialAssignment != geo._materials) {
				serializer << MakeCmdAndRanged(ModelCommand::SetMaterialAssignments, geo._materials);
				currentMaterialAssignment = &geo._materials;
			}
			serializer << MakeCmdAndRawData(ModelCommand::GeoCall, geo._id + geoIdOffset);
		}

		for (const auto& geo:obj._skinControllerInstances) {
			if (!currentTransformMarker.has_value() || geo._localToWorldId != currentTransformMarker.value()) {
				serializer << MakeCmdAndRawData(ModelCommand::SetTransformMarker, geo._localToWorldId);
				currentTransformMarker = geo._localToWorldId;
			}
			if (!currentMaterialAssignment || *currentMaterialAssignment != geo._materials) {
				serializer << MakeCmdAndRanged(ModelCommand::SetMaterialAssignments, geo._materials);
				currentMaterialAssignment = &geo._materials;
			}
			serializer << MakeCmdAndRawData(ModelCommand::GeoCall, geo._id + geoIdOffset);
		}

		// write out the InputInterface
		auto hashedInterface = obj.BuildHashedInputInterface();
		serializer << CmdAndRawData{(uint32_t)ModelCommand::InputInterface, hashedInterface};
	}

	std::vector<uint64_t> NascentModelCommandStream::BuildHashedInputInterface() const
	{
		std::vector<uint64_t> hashedInterface;
		hashedInterface.reserve(_inputInterfaceNames.size());
		for (const auto&j:_inputInterfaceNames) hashedInterface.push_back(HashCombine(Hash64(j.first), Hash64(j.second)));
		return hashedInterface;
	}

	unsigned NascentModelCommandStream::GetMaxLOD() const
	{
		unsigned maxLOD = 0u;
		for (const auto&i:_geometryInstances) maxLOD = std::max(i._levelOfDetail, maxLOD);
		for (const auto&i:_skinControllerInstances) maxLOD = std::max(i._levelOfDetail, maxLOD);
		return maxLOD;
	}

	std::ostream& SerializationOperator(std::ostream& stream, const NascentModelCommandStream& cmdStream)
	{
		stream << " --- Geometry instances:" << std::endl;
		unsigned c=0;
		for (const auto& i:cmdStream._geometryInstances) {
			stream << "  [" << c++ << "] GeoId: " << i._id << " Transform: " << i._localToWorldId << " LOD: " << i._levelOfDetail << std::endl;
			stream << "     Materials: " << std::hex;
			for (size_t q=0; q<i._materials.size(); ++q) {
				if (q != 0) stream << ", ";
				stream << i._materials[q];
			}
			stream << std::dec << std::endl;
		}

		stream << " --- Skin controller instances:" << std::endl;
		c=0;
		for (const auto& i:cmdStream._skinControllerInstances) {
			stream << "  [" << c++ << "] GeoId: " << i._id << " Transform: " << i._localToWorldId << std::endl;
			stream << "     Materials: " << std::hex;
			for (size_t q=0; q<i._materials.size(); ++q) {
				if (q != 0) stream << ", ";
				stream << i._materials[q];
			}
			stream << std::dec << std::endl;
		}

		stream << " --- Camera instances:" << std::endl;
		c=0;
		for (const auto& i:cmdStream._cameraInstances)
			stream << "  [" << c++ << "] Transform: " << i._localToWorldId << std::endl;

		stream << " --- Input interface:" << std::endl;
		c=0;
		for (const auto& i:cmdStream._inputInterfaceNames)
			stream << "  [" << c++ << "] " << i.first << " : " << i.second  << std::endl;
		return stream;
	}

}}}
