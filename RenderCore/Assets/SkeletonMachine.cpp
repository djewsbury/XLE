// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "SkeletonMachine.h"

namespace RenderCore { namespace Assets
{

	void SkeletonMachine::GenerateOutputTransforms(   
		IteratorRange<Float4x4*> output,
		const AnimatedParameterSet*   parameterSet) const
	{
		if (output.size() < _outputMatrixCount)
			Throw(::Exceptions::BasicLabel("Output buffer to SkeletonMachine::GenerateOutputTransforms is too small"));
		RenderCore::Assets::GenerateOutputTransforms(
			output, parameterSet, 
			MakeIteratorRange(_commandStream, _commandStream + _commandStreamSize));
	}

	void SkeletonMachine::CalculateParentPointers(IteratorRange<unsigned*> output) const
	{
		RenderCore::Assets::CalculateParentPointers(output, MakeIteratorRange(_commandStream, _commandStream + _commandStreamSize));
	}

	std::vector<StringSection<>> SkeletonMachine::GetOutputMatrixNames() const
	{
		std::vector<StringSection<>> result;
		result.reserve(_outputInterface._outputMatrixNameCount);
		auto nameStart = _outputMatrixNames.begin();
		for (auto i=_outputMatrixNames.begin(); i!=_outputMatrixNames.end();) {
			if (*i == 0) {
				result.push_back(MakeStringSection(nameStart, i));
				++i;
				nameStart = i;
			} else {
				++i;
			}
		}
		return result;
	}

	SkeletonMachine::SkeletonMachine()
	{
		_commandStream = nullptr;
		_commandStreamSize = 0;
		_outputMatrixCount = 0;
	}

	SkeletonMachine::~SkeletonMachine()
	{
	}

}}

