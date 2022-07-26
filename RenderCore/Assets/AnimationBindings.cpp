// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "AnimationBindings.h"
#include "TransformationCommands.h"
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

		TransformationMachineOptimizer_Null nullOptimizer;
		_specializedSkeletonMachine = OptimizeTransformationMachine(_specializedSkeletonMachine, nullOptimizer);

		_outputMatrixCount = input.GetOutputMatrixCount();
	}

	void AnimationSetBinding::GenerateOutputTransforms(
		IteratorRange<Float4x4*> output,
		IteratorRange<const void*> parameterBlock) const
	{
		RenderCore::Assets::GenerateOutputTransforms(
			output, parameterBlock, 
			MakeIteratorRange(_specializedSkeletonMachine));
	}

	SkeletonBinding::SkeletonBinding(   const SkeletonMachine::OutputInterface&		output,
										IteratorRange<const uint64_t*> 				input)
	{
		std::vector<unsigned> result(input.size(), ~0u);

		for (size_t c=0; c<input.size(); ++c) {
			uint64_t name = input[c];
			for (size_t c2=0; c2<output._outputMatrixNameCount; ++c2) {
				if (output._outputMatrixNames[c2] == name) {
					result[c] = unsigned(c2);
					break;
				}
			}
		}
			
		_modelJointIndexToMachineOutput = std::move(result);
	}

	SkeletonBinding::SkeletonBinding(   const SkeletonMachine::OutputInterface&		primaryOutput,
										const SkeletonMachine::OutputInterface&		secondaryOutput,
										IteratorRange<const uint64_t*> 				input)
	{
		std::vector<unsigned> result(input.size(), ~0u);

		for (size_t c=0; c<input.size(); ++c) {
			uint64_t name = input[c];
			for (size_t c2=0; c2<primaryOutput._outputMatrixNameCount; ++c2)
				if (primaryOutput._outputMatrixNames[c2] == name) {
					result[c] = unsigned(c2);
					break;
				}
			if (result[c] == ~0u)
				for (size_t c2=0; c2<secondaryOutput._outputMatrixNameCount; ++c2)
					if (secondaryOutput._outputMatrixNames[c2] == name) {
						result[c] = unsigned(c2) + primaryOutput._outputMatrixNameCount;
						break;
					}
		}
			
		_modelJointIndexToMachineOutput = std::move(result);
	}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	static Float4x4 TransformationCommandsToMatrix(IteratorRange<const void*> cmds)
	{
		uint32_t temp[cmds.size()+2];
		memcpy(temp, cmds.begin(), cmds.size());
		temp[cmds.size()] = (uint32_t)TransformCommand::WriteOutputMatrix;
		temp[cmds.size()+1] = 0;
		Float4x4 result;
		GenerateOutputTransforms(
			MakeIteratorRange(&result, &result+1),
			{},
			MakeIteratorRange(temp, &temp[cmds.size()+2]));
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
			if (_defaultRotationCmds.empty()) {
				Quaternion q;
				q.identity();
				return q;
			}
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
			case TransformCommand::BindingPoint_2: componentCount = 2; break;
			case TransformCommand::BindingPoint_3: componentCount = 3; break;
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

	static void SetupFullTransformBinding(
		std::vector<uint32_t>& result,
		std::vector<AnimationSet::ParameterBindingRules>& bindingRules, 
		std::vector<uint8_t>& outputBlockItemsDefaults,
		DefaultedTransformation& defaults,
		unsigned animParameterIndex, AnimSamplerType samplerType)
	{
		if (samplerType == AnimSamplerType::Float4x4) {
			result.push_back((uint32_t)TransformCommand::TransformFloat4x4_Parameter);
			result.push_back(
				ConfigureBindingRules(
					bindingRules, outputBlockItemsDefaults, 
					defaults.AsFloat4x4Parameter(),
					animParameterIndex, samplerType));
		} else {
			assert(0);
		}
	}

	static void SetupTranslationBinding(
		std::vector<uint32_t>& result,
		std::vector<AnimationSet::ParameterBindingRules>& bindingRules, 
		std::vector<uint8_t>& outputBlockItemsDefaults,
		DefaultedTransformation& defaults,
		unsigned animParameterIndex, AnimSamplerType samplerType)
	{
		if (samplerType == AnimSamplerType::Float3) {
			result.push_back((uint32_t)TransformCommand::Translate_Parameter);
			result.push_back(
				ConfigureBindingRules(
					bindingRules, outputBlockItemsDefaults, 
					defaults.AsTranslateParameter(),
					animParameterIndex, samplerType));
		} else {
			assert(0);
		}
	}

	static void SetupRotationBinding(
		std::vector<uint32_t>& result,
		std::vector<AnimationSet::ParameterBindingRules>& bindingRules, 
		std::vector<uint8_t>& outputBlockItemsDefaults,
		DefaultedTransformation& defaults,
		unsigned animParameterIndex, AnimSamplerType samplerType)
	{
		if (samplerType == AnimSamplerType::Quaternion) {
			result.push_back((uint32_t)TransformCommand::RotateQuaternion_Parameter);
			result.push_back(
				ConfigureBindingRules(
					bindingRules, outputBlockItemsDefaults, 
					defaults.AsRotateQuaternionParameter(),
					animParameterIndex, samplerType));
		} else if (samplerType == AnimSamplerType::Float4) {
			result.push_back((uint32_t)TransformCommand::RotateAxisAngle_Parameter);
			result.push_back(
				ConfigureBindingRules(
					bindingRules, outputBlockItemsDefaults, 
					defaults.AsRotateAxisAngleParameter(),
					animParameterIndex, samplerType));
		} else {
			assert(0);
		}
	}

	static void SetupScaleBinding(
		std::vector<uint32_t>& result,
		std::vector<AnimationSet::ParameterBindingRules>& bindingRules, 
		std::vector<uint8_t>& outputBlockItemsDefaults,
		DefaultedTransformation& defaults,
		unsigned animParameterIndex, AnimSamplerType samplerType)
	{
		if (samplerType == AnimSamplerType::Float1) {
			result.push_back((uint32_t)TransformCommand::UniformScale_Parameter);
			result.push_back(
				ConfigureBindingRules(
					bindingRules, outputBlockItemsDefaults, 
					defaults.AsUniformScaleParameter(),
					animParameterIndex, samplerType));
		} else if (samplerType == AnimSamplerType::Float3) {
			result.push_back((uint32_t)TransformCommand::ArbitraryScale_Parameter);
			result.push_back(
				ConfigureBindingRules(
					bindingRules, outputBlockItemsDefaults, 
					defaults.AsArbitraryScaleParameter(),
					animParameterIndex, samplerType));
		} else {
			assert(0);
		}
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
					auto startOfCmd = cmd;
					++cmd;
					uint64_t bindName = *cmd | (uint64_t(*(cmd+1)) << 32ull);
					cmd += 2;
					std::optional<unsigned> rotationParam, translationParam, scaleParam, fullTransformParam;
					std::optional<unsigned> noneParam;

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
								assert(!noneParam);
								noneParam = c;
							}
						}
					}

					if (fullTransformParam) {
						assert(!rotationParam && !translationParam && !scaleParam && !noneParam);
					}
					if (noneParam) {
						assert(!rotationParam && !translationParam && !scaleParam && !fullTransformParam);
					}

					if (rotationParam || translationParam || scaleParam || fullTransformParam) {
						DefaultedTransformation defaults(startOfCmd);
						// we need to mix together what's provided by the animation set with what's provided by
						// the defaults in the transformation commands
						// Component ordering is always translation, rotation, scale
						if (fullTransformParam) {
							SetupFullTransformBinding(
								result, parameterBindingRules, parameterDefaultsBlock,
								defaults,
								fullTransformParam.value(), animSetOutput[fullTransformParam.value()]._samplerType);
						} else {
							if (translationParam) {
								SetupTranslationBinding(
									result, parameterBindingRules, parameterDefaultsBlock,
									defaults,
									translationParam.value(), animSetOutput[translationParam.value()]._samplerType);
							} else if (!defaults._defaultTranslationCmds.empty()) {
								result.insert(result.end(), defaults._defaultTranslationCmds.begin(), defaults._defaultTranslationCmds.end());
							} else if (defaults._fullTransform) {
								result.push_back((uint32_t)TransformCommand::Translate_Static);
								auto translation = defaults._fullTransform.value()._translation;
								result.insert(result.end(), (const uint32_t*)&translation, (const uint32_t*)(&translation+1));
							}

							if (rotationParam) {
								SetupRotationBinding(
									result, parameterBindingRules, parameterDefaultsBlock,
									defaults,
									rotationParam.value(), animSetOutput[rotationParam.value()]._samplerType);
							} else if (!defaults._defaultRotationCmds.empty()) {
								result.insert(result.end(), defaults._defaultRotationCmds.begin(), defaults._defaultRotationCmds.end());
							} else if (defaults._fullTransform) {
								result.push_back((uint32_t)TransformCommand::RotateQuaternion_Static);
								auto rotation = defaults._fullTransform.value()._rotation;
								result.insert(result.end(), (const uint32_t*)&rotation, (const uint32_t*)(&rotation+1));
							}

							if (scaleParam) {
								SetupScaleBinding(
									result, parameterBindingRules, parameterDefaultsBlock,
									defaults,
									scaleParam.value(), animSetOutput[scaleParam.value()]._samplerType);
							} else if (!defaults._defaultScaleCmds.empty()) {
								result.insert(result.end(), defaults._defaultScaleCmds.begin(), defaults._defaultScaleCmds.end());
							} else if (defaults._fullTransform) {
								result.push_back((uint32_t)TransformCommand::ArbitraryScale_Static);
								auto scale = defaults._fullTransform.value()._scale;
								result.insert(result.end(), (const uint32_t*)&scale, (const uint32_t*)(&scale+1));
							}
						}
					} else if (noneParam) {
						DefaultedTransformation defaults(startOfCmd);
						// If the animation parameter is marked with AnimSampleComponent::None, it means the animation parameter doesn't
						// have an inherent component to bind to, and it must actually imply it's type from the binding point it's applied
						// to.
						// This is more similar to the previous behaviour for animation binding. This might require that there is a separate
						// bind point for each component of the transform (assuming not using full matrix transforms)
						if (defaults._fullTransform) {
							SetupFullTransformBinding(
								result, parameterBindingRules, parameterDefaultsBlock,
								defaults,
								noneParam.value(), animSetOutput[noneParam.value()]._samplerType);
						} else if (!defaults._defaultTranslationCmds.empty()) {
							assert(defaults._defaultRotationCmds.empty() && defaults._defaultScaleCmds.empty());		// only one component can be defaulted
							SetupTranslationBinding(
								result, parameterBindingRules, parameterDefaultsBlock,
								defaults,
								noneParam.value(), animSetOutput[noneParam.value()]._samplerType);
						} else if (!defaults._defaultRotationCmds.empty()) {
							assert(defaults._defaultTranslationCmds.empty() && defaults._defaultScaleCmds.empty());		// only one component can be defaulted
							SetupRotationBinding(
								result, parameterBindingRules, parameterDefaultsBlock,
								defaults,
								noneParam.value(), animSetOutput[noneParam.value()]._samplerType);
						} else if (!defaults._defaultScaleCmds.empty()) {
							SetupScaleBinding(
								result, parameterBindingRules, parameterDefaultsBlock,
								defaults,
								noneParam.value(), animSetOutput[noneParam.value()]._samplerType);
						} else {
							assert(0);		// no defaults at all; we can't imply the component for this animation parameter
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

