// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "AnimationSet.h"
#include "SkeletonMachine.h"
#include "ModelScaffoldInternal.h"		// for ModelCommandStream

namespace RenderCore { namespace Assets
{
	class AnimationSetBinding
	{
	public:
		unsigned GetCount() const { return (unsigned)_animDriverToMachineParameter.size(); }
		unsigned AnimDriverToMachineParameter(unsigned index) const { return _animDriverToMachineParameter[index]; }

		AnimationSetBinding(const AnimationSet::OutputInterface&            output,
							const SkeletonMachine::InputInterface&    input);
		AnimationSetBinding();
		AnimationSetBinding(AnimationSetBinding&& moveFrom) never_throws;
		AnimationSetBinding& operator=(AnimationSetBinding&& moveFrom) never_throws;
		~AnimationSetBinding();

	private:
		std::vector<unsigned>   _animDriverToMachineParameter;
	};

	class SkeletonBinding
	{
	public:
		unsigned GetModelJointCount() const { return (unsigned)_modelJointIndexToMachineOutput.size(); }
		unsigned ModelJointToMachineOutput(unsigned index) const { return _modelJointIndexToMachineOutput[index]; }

		SkeletonBinding(    const SkeletonMachine::OutputInterface&		output,
							const ModelCommandStream::InputInterface&   input);
		SkeletonBinding();
		~SkeletonBinding();

	private:
		std::vector<unsigned>   _modelJointIndexToMachineOutput;
	};

	std::vector<uint32_t> SpecializeTransformationMachine(
		IteratorRange<const uint32_t*>      commandStream,
		AnimationSet::OutputInterface& 		animSetOutput);
}}

