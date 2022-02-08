// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "AnimationBindings.h"
#include "../../OSServices/Log.h"

namespace RenderCore { namespace Assets
{
	AnimationSetBinding::AnimationSetBinding(
		const AnimationSet::OutputInterface&        output,
		const SkeletonMachine::InputInterface&      input)
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
			uint64_t parameterName = output[c]._name;
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

	struct DefaultedTransformation
	{
		std::optional<ScaleRotationTranslationQ> _fullTransform;

		std::vector<uint32_t> _defaultTranslationCmds;
		std::vector<uint32_t> _defaultRotationCmds;
		std::vector<uint32_t> _defaultScaleCmds;

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
			cmd += 1;
			for (unsigned c=0; c<componentCount; ++c) {
				switch ((TransformCommand)*cmd) {
				case TransformCommand::TransformFloat4x4_Static:
					_fullTransform = ScaleRotationTranslationQ{*(Float4x4*)(cmd+1)};
					break;
				case TransformCommand::Translate_Static:
					_defaultTranslationCmds.insert(_defaultTranslationCmds.end(), cmd, NextTransformationCommand(cmd));
					break;
				case TransformCommand::RotateX_Static:
				case TransformCommand::RotateY_Static:
				case TransformCommand::RotateZ_Static:
				case TransformCommand::Rotate_Static:
				case TransformCommand::RotateQuaternion_Static:
					_defaultRotationCmds.insert(_defaultRotationCmds.end(), cmd, NextTransformationCommand(cmd));
					break;

				case TransformCommand::UniformScale_Static:
					_defaultScaleCmds.insert(_defaultScaleCmds.end(), cmd, NextTransformationCommand(cmd));
					break;
				default: break;
				}
				cmd = NextTransformationCommand(cmd);
			}
		}
	};

	static unsigned Offset(
		std::vector<AnimationSet::OutputBlockItem>& items, 
		unsigned& outputBlockSize,
		unsigned outputIndex, AnimSamplerType samplerType);

	std::vector<uint32_t> SpecializeTransformationMachine(
		IteratorRange<const uint32_t*>		commandStream,
		AnimationSet::OutputInterface&		animSetOutput)
	{
		// Given a generate input transformation command list, generate a specialized version that
		// can read and use the animated parameter output as given

		std::vector<uint32_t> result;
		result.reserve(commandStream.size());

		std::vector<AnimationSet::OutputBlockItem> outputBlockItems;
		unsigned outputBlockSize = 0;

		for (auto i=commandStream.begin(); i!=commandStream.end();) {
			auto cmd = i;
			i = NextTransformationCommand(i);
			switch ((TransformCommand)*cmd) {
			case TransformCommand::TransformFloat4x4_Parameter:
			case TransformCommand::Translate_Parameter:
			case TransformCommand::RotateX_Parameter:
			case TransformCommand::RotateY_Parameter:
			case TransformCommand::RotateZ_Parameter:
			case TransformCommand::Rotate_Parameter:
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
							case AnimationSet::OutputPart::Component::Rotation:
								assert(!rotationParam);
								rotationParam = c;
								break;
							case AnimationSet::OutputPart::Component::Translation:
								assert(!translationParam);
								translationParam = c;
								break;
							case AnimationSet::OutputPart::Component::Scale:
								assert(!scaleParam);
								scaleParam = c;
								break;
							case AnimationSet::OutputPart::Component::FullTransform:
								assert(!fullTransformParam);
								fullTransformParam = c;
								break;
							case AnimationSet::OutputPart::Component::None:
								break;		// "none" component ignored
							}
						}
					}
					if (rotationParam || translationParam || scaleParam || fullTransformParam) {
						DefaultedTransformation defaults(cmd);
						// we need to mix together what's provided by the animation set with what's provided by
						// the defaults in the transformation commands
						// Component ordering is always translation, rotation, scale
						if (fullTransformParam) {
							result.push_back((uint32_t)TransformCommand::TransformFloat4x4_Parameter);
							result.push_back(Offset(outputBlockItems, outputBlockSize, fullTransformParam.value(), animSetOutput[fullTransformParam.value()]._samplerType));
						} else {
							if (translationParam) {
								result.push_back((uint32_t)TransformCommand::Translate_Parameter);
								result.push_back(Offset(outputBlockItems, outputBlockSize, translationParam.value(), animSetOutput[translationParam.value()]._samplerType));
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
									result.push_back(Offset(outputBlockItems, outputBlockSize, rotationParam.value(), samplerType));
								} else if (samplerType == AnimSamplerType::Float3) {
									result.push_back((uint32_t)TransformCommand::Rotate_Parameter);
									result.push_back(Offset(outputBlockItems, outputBlockSize, rotationParam.value(), samplerType));
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
									result.push_back(Offset(outputBlockItems, outputBlockSize, scaleParam.value(), samplerType));
								} else if (samplerType == AnimSamplerType::Float3) {
									result.push_back((uint32_t)TransformCommand::ArbitraryScale_Parameter);
									result.push_back(Offset(outputBlockItems, outputBlockSize, scaleParam.value(), samplerType));
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

