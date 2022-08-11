// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "AnimationSet.h"
#include "SkeletonMachine.h"

namespace RenderCore { namespace Assets
{
	class AnimationSetBinding
	{
	public:
		IteratorRange<const AnimationSet::ParameterBindingRules*> GetParameterBindingRules() const { return _animBindingRules; }
		IteratorRange<const uint8_t*> GetParameterDefaultsBlock() const { return _parameterDefaultsBlock; }

		void GenerateOutputTransforms   (   IteratorRange<Float4x4*> output,
                                            IteratorRange<const void*> parameterBlock) const;
		unsigned GetOutputMatrixCount() const { return _outputMatrixCount; }

		AnimationSetBinding(const AnimationSet::OutputInterface&		output,
							const SkeletonMachine&    					input);
		AnimationSetBinding() = default;

	private:
		std::vector<uint32_t>   _specializedSkeletonMachine;
		std::vector<AnimationSet::ParameterBindingRules> _animBindingRules;
		std::vector<uint8_t> 	_parameterDefaultsBlock;
		unsigned _outputMatrixCount = 0;
	};

	class SkeletonBinding
	{
	public:
		unsigned GetModelJointCount() const { return (unsigned)_modelJointIndexToMachineOutput.size(); }
		unsigned ModelJointToMachineOutput(unsigned index) const { return _modelJointIndexToMachineOutput[index]; }
		IteratorRange<const unsigned*> ModelJointToMachineOutput() const { return _modelJointIndexToMachineOutput; }

		SkeletonBinding(    const SkeletonMachine::OutputInterface&		output,
							IteratorRange<const uint64_t*> 				input);
		SkeletonBinding(    const SkeletonMachine::OutputInterface&		primaryOutput,
							const SkeletonMachine::OutputInterface&		secondaryutput,
							IteratorRange<const uint64_t*> 				input);
		SkeletonBinding() = default;

	private:
		std::vector<unsigned>   _modelJointIndexToMachineOutput;
	};

	std::vector<uint32_t> SpecializeTransformationMachine(
		/* out */ std::vector<AnimationSet::ParameterBindingRules>& parameterBindingRules,
		/* out */ std::vector<uint8_t>&			parameterDefaultsBlock,
		IteratorRange<const uint32_t*>			commandStream,
		const AnimationSet::OutputInterface& 	animSetOutput);
}}

