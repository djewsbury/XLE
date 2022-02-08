// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "AnimationBindings.h"
#include "../../OSServices/Log.h"

namespace RenderCore { namespace Assets
{
	AnimationSetBinding::AnimationSetBinding(
		const AnimationSet::OutputInterface&		output,
		const SkeletonMachine&    					input)
	{
		_specializedSkeletonMachine = SpecializeTransformationMachine(
			_animBindingRules, _parameterDefaultsBlock, 
			input.GetCommandStream(),
			output);
	}

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

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	static Float4x4 TransformationCommandsToMatrix(IteratorRange<const void*> cmds)
	{
		uint32_t temp[cmds.size()+1];
		memcpy(temp, cmds.begin(), cmds.size());
		temp[cmds.size()] = (uint32_t)TransformCommand::WriteOutputMatrix;
		temp[cmds.size()+1] = 0;
		Float4x4 result;
		GenerateOutputTransforms(
			MakeIteratorRange(&result, &result+1),
			{},
			MakeIteratorRange(temp, &temp[cmds.size()+1]));
		return result;
	}

	struct DefaultedTransformation
	{
		std::optional<ScaleRotationTranslationQ> _fullTransform;

		std::vector<uint32_t> _defaultTranslationCmds;
		std::vector<uint32_t> _defaultRotationCmds;
		std::vector<uint32_t> _defaultScaleCmds;

		Float4x4 AsFloat4x4Parameter()
		{
			if (_fullTransform)
				return AsFloat4x4(_fullTransform.value());
			auto translation = TransformationCommandsToMatrix(_defaultTranslationCmds);
			auto rotation = TransformationCommandsToMatrix(_defaultRotationCmds);
			auto scale = TransformationCommandsToMatrix(_defaultScaleCmds);
			return Combine(Combine(translation, rotation), scale);
		}

		Float3 AsTranslateParameter()
		{
			if (_fullTransform)
				return _fullTransform.value()._translation;
			if (_defaultTranslationCmds.empty()) return Zero<Float3>();
			auto translation = TransformationCommandsToMatrix(_defaultTranslationCmds);
			return ExtractTranslation(translation);
		}

		Quaternion AsRotateQuaternionParameter()
		{
			if (_fullTransform)
				return _fullTransform.value()._rotation;
			if (_defaultRotationCmds.empty()) return Identity<Quaternion>();
			auto rotation = TransformationCommandsToMatrix(_defaultRotationCmds);
			auto rotMat = Truncate3x3(rotation);
			Quaternion result;
			cml::quaternion_rotation_matrix(result, rotMat);
			return result;
		}
		
		ArbitraryRotation AsRotateAxisAngleParameter()
		{
			if (_fullTransform) {
				auto res = ArbitraryRotation{AsFloat3x3(_fullTransform.value()._rotation)};
				res._angle = Rad2Deg(res._angle);		// hack -- this expect degrees, not radians
				return res;
			}
			if (_defaultRotationCmds.empty()) return ArbitraryRotation{};
			auto rotation = TransformationCommandsToMatrix(_defaultRotationCmds);
			return Truncate3x3(rotation);
		}

		float AsUniformScaleParameter()
		{
			if (_fullTransform)
				return (_fullTransform.value()._scale[0] + _fullTransform.value()._scale[1] + _fullTransform.value()._scale[2]) / 3.f;
			if (_defaultScaleCmds.empty()) return 1.f;
			auto scale = TransformationCommandsToMatrix(_defaultScaleCmds);
			auto t = ScaleRotationTranslationM{scale}._scale;
			return (t[0]+t[1]+t[2]) / 3.f;
		}

		Float3 AsArbitraryScaleParameter()
		{
			if (_fullTransform)
				return _fullTransform.value()._scale;
			if (_defaultScaleCmds.empty()) return Float3{1.f, 1.f, 1.f};
			auto scale = TransformationCommandsToMatrix(_defaultScaleCmds);
			return ScaleRotationTranslationM{scale}._scale;
		}

		DefaultedTransformation(const uint32_t* cmd)
		{
			unsigned componentCount = 0;
			switch ((TransformCommand)*cmd) {
			case TransformCommand::BindingPoint_1: componentCount = 1; break;
			case TransformCommand::BindingPoint_2: componentCount = 1; break;
			case TransformCommand::BindingPoint_3: componentCount = 1; break;
			case TransformCommand::BindingPoint_0:
			default: break;
			}

			// Note that the ordering given here is lost in the process
			// We're expecting translation, rotation, scale -- in that order (but each are optional)
			cmd += 1+2;		// skip over command & binding name
			for (unsigned c=0; c<componentCount; ++c) {
				switch ((TransformCommand)*cmd) {
				case TransformCommand::TransformFloat4x4_Static:
					_fullTransform = ScaleRotationTranslationQ{*(Float4x4*)(cmd+1)};
					break;
				case TransformCommand::Translate_Static:
					assert(!_fullTransform && _defaultRotationCmds.empty() && _defaultScaleCmds.empty());
					_defaultTranslationCmds.insert(_defaultTranslationCmds.end(), cmd, NextTransformationCommand(cmd));
					break;
				case TransformCommand::RotateX_Static:
				case TransformCommand::RotateY_Static:
				case TransformCommand::RotateZ_Static:
				case TransformCommand::RotateAxisAngle_Static:
				case TransformCommand::RotateQuaternion_Static:
					assert(!_fullTransform && _defaultScaleCmds.empty());
					_defaultRotationCmds.insert(_defaultRotationCmds.end(), cmd, NextTransformationCommand(cmd));
					break;

				case TransformCommand::UniformScale_Static:
					assert(!_fullTransform);
					_defaultScaleCmds.insert(_defaultScaleCmds.end(), cmd, NextTransformationCommand(cmd));
					break;
				default: break;
				}
				cmd = NextTransformationCommand(cmd);
			}
		}
	};

	template<typename Type>
		static unsigned ConfigureBindingRules(
			std::vector<AnimationSet::ParameterBindingRules>& bindingRules, 
			std::vector<uint8_t>& outputBlockItemsDefaults,
			const Type& defaultValue,
			unsigned animParameterIndex, AnimSamplerType samplerType)
	{
		assert(bindingRules[animParameterIndex]._outputOffset == ~0u);
		auto result = bindingRules[animParameterIndex]._outputOffset = (unsigned)outputBlockItemsDefaults.size();
		bindingRules[animParameterIndex]._samplerType = samplerType;
		outputBlockItemsDefaults.insert(outputBlockItemsDefaults.end(), (const uint8_t*)&defaultValue, (const uint8_t*)(&defaultValue+1));
		return result;
	}

	std::vector<uint32_t> SpecializeTransformationMachine(
		/* out */ std::vector<AnimationSet::ParameterBindingRules>& parameterBindingRules,
		/* out */ std::vector<uint8_t>&			parameterDefaultsBlock,
		IteratorRange<const uint32_t*>			commandStream,
		const AnimationSet::OutputInterface&	animSetOutput)
	{
		// Given a generate input transformation command list, generate a specialized version that
		// can read and use the animated parameter output as given

		std::vector<uint32_t> result;
		result.reserve(commandStream.size());

		assert(parameterBindingRules.empty());
		assert(parameterDefaultsBlock.empty());
		parameterBindingRules.resize(animSetOutput.size());

		for (auto i=commandStream.begin(); i!=commandStream.end();) {
			auto cmd = i;
			i = NextTransformationCommand(i);
			switch ((TransformCommand)*cmd) {
			case TransformCommand::TransformFloat4x4_Parameter:
			case TransformCommand::Translate_Parameter:
			case TransformCommand::RotateX_Parameter:
			case TransformCommand::RotateY_Parameter:
			case TransformCommand::RotateZ_Parameter:
			case TransformCommand::RotateAxisAngle_Parameter:
			case TransformCommand::RotateQuaternion_Parameter:
			case TransformCommand::UniformScale_Parameter:
			case TransformCommand::ArbitraryScale_Parameter:
				Throw(std::runtime_error("Attempting to specialize a transformation machine in SpecializeTransformationMachine() that has already been specialized"));

			case TransformCommand::BindingPoint_0:
			case TransformCommand::BindingPoint_1:
			case TransformCommand::BindingPoint_2:
			case TransformCommand::BindingPoint_3:
				// This is a binding point. We should be followed by 0 or more
				// default transformation components.
				{
					++cmd;
					uint64_t bindName = *cmd | (uint64_t(*(cmd+1)) << 32ull);
					cmd += 2;
					std::optional<unsigned> rotationParam, translationParam, scaleParam, fullTransformParam;
					for (unsigned c=0; c<animSetOutput.size(); ++c) {
						if (animSetOutput[c]._name == bindName) {
							switch (animSetOutput[c]._component) {
							case AnimSamplerComponent::Rotation:
								assert(!rotationParam);
								rotationParam = c;
								break;
							case AnimSamplerComponent::Translation:
								assert(!translationParam);
								translationParam = c;
								break;
							case AnimSamplerComponent::Scale:
								assert(!scaleParam);
								scaleParam = c;
								break;
							case AnimSamplerComponent::FullTransform:
								assert(!fullTransformParam);
								fullTransformParam = c;
								break;
							case AnimSamplerComponent::None:
								break;		// "none" component ignored
							}
						}
					}
					if (fullTransformParam) {
						assert(!rotationParam && !translationParam && !scaleParam);
					}

					if (rotationParam || translationParam || scaleParam || fullTransformParam) {
						DefaultedTransformation defaults(cmd);
						// we need to mix together what's provided by the animation set with what's provided by
						// the defaults in the transformation commands
						// Component ordering is always translation, rotation, scale
						if (fullTransformParam) {
							result.push_back((uint32_t)TransformCommand::TransformFloat4x4_Parameter);
							result.push_back(
								ConfigureBindingRules(
									parameterBindingRules, parameterDefaultsBlock, 
									defaults.AsFloat4x4Parameter(),
									fullTransformParam.value(), animSetOutput[fullTransformParam.value()]._samplerType));
						} else {
							if (translationParam) {
								result.push_back((uint32_t)TransformCommand::Translate_Parameter);
								result.push_back(
									ConfigureBindingRules(
										parameterBindingRules, parameterDefaultsBlock,
										defaults.AsTranslateParameter(),
										translationParam.value(), animSetOutput[translationParam.value()]._samplerType));
							} else if (!defaults._defaultTranslationCmds.empty()) {
								result.insert(result.end(), defaults._defaultTranslationCmds.begin(), defaults._defaultTranslationCmds.end());
							} else if (defaults._fullTransform) {
								result.push_back((uint32_t)TransformCommand::Translate_Static);
								auto translation = defaults._fullTransform.value()._translation;
								result.insert(result.end(), (const uint32_t*)&translation, (const uint32_t*)(&translation+1));
							}

							if (rotationParam) {
								auto samplerType = animSetOutput[rotationParam.value()]._samplerType;
								if (samplerType == AnimSamplerType::Quaternion) {
									result.push_back((uint32_t)TransformCommand::RotateQuaternion_Parameter);
									result.push_back(
										ConfigureBindingRules(
											parameterBindingRules, parameterDefaultsBlock, 
											defaults.AsRotateQuaternionParameter(),
											rotationParam.value(), samplerType));
								} else if (samplerType == AnimSamplerType::Float4) {
									result.push_back((uint32_t)TransformCommand::RotateAxisAngle_Parameter);
									result.push_back(
										ConfigureBindingRules(
											parameterBindingRules, parameterDefaultsBlock, 
											defaults.AsRotateAxisAngleParameter(),
											rotationParam.value(), samplerType));
								} else {
									assert(0);
								}
							} else if (!defaults._defaultRotationCmds.empty()) {
								result.insert(result.end(), defaults._defaultRotationCmds.begin(), defaults._defaultRotationCmds.end());
							} else if (defaults._fullTransform) {
								result.push_back((uint32_t)TransformCommand::RotateQuaternion_Static);
								auto rotation = defaults._fullTransform.value()._rotation;
								result.insert(result.end(), (const uint32_t*)&rotation, (const uint32_t*)(&rotation+1));
							}

							if (scaleParam) {
								auto samplerType = animSetOutput[scaleParam.value()]._samplerType;
								if (samplerType == AnimSamplerType::Float1) {
									result.push_back((uint32_t)TransformCommand::UniformScale_Parameter);
									// awkward here if the default really should be arbitrary, but the animation has a uniforms scale
									result.push_back(
										ConfigureBindingRules(
											parameterBindingRules, parameterDefaultsBlock,
											defaults.AsUniformScaleParameter(),
											scaleParam.value(), samplerType));
								} else if (samplerType == AnimSamplerType::Float3) {
									result.push_back((uint32_t)TransformCommand::ArbitraryScale_Parameter);
									result.push_back(
										ConfigureBindingRules(
											parameterBindingRules, parameterDefaultsBlock, 
											defaults.AsArbitraryScaleParameter(),
											scaleParam.value(), samplerType));
								} else {
									assert(0);
								}
							} else if (!defaults._defaultScaleCmds.empty()) {
								result.insert(result.end(), defaults._defaultScaleCmds.begin(), defaults._defaultScaleCmds.end());
							} else if (defaults._fullTransform) {
								result.push_back((uint32_t)TransformCommand::ArbitraryScale_Static);
								auto scale = defaults._fullTransform.value()._scale;
								result.insert(result.end(), (const uint32_t*)&scale, (const uint32_t*)(&scale+1));
							}
						}
					} else {
						// no matching parameters at all. We can just take the defaults as-is because they are specified
						// with the same "_Static" as static transformations
						result.insert(result.end(), cmd, i);
					}
				}
				break;

			default:
				result.insert(result.end(), cmd, i);
				break;
			}
		}

		return result;
	}

}}

