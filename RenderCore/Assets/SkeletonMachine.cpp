// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "SkeletonMachine.h"
#include "TransformationCommands.h"

namespace RenderCore { namespace Assets
{

	void SkeletonMachine::GenerateOutputTransforms(   
		IteratorRange<Float4x4*> output,
		IteratorRange<const void*> parameterBlock) const
	{
		if (output.size() < _outputInterface.size())
			Throw(::Exceptions::BasicLabel("Output buffer to SkeletonMachine::GenerateOutputTransforms is too small"));
		RenderCore::Assets::GenerateOutputTransforms(
			output, parameterBlock,
			_commandStream);
	}

	void SkeletonMachine::CalculateParentPointers(IteratorRange<unsigned*> output) const
	{
		RenderCore::Assets::CalculateParentPointers(output, _commandStream);
	}

	std::vector<StringSection<>> SkeletonMachine::GetOutputMatrixNames() const
	{
		std::vector<StringSection<>> result;
		result.reserve(_outputInterface.size());
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

	SkeletonMachine::SkeletonMachine() = default;
	SkeletonMachine::~SkeletonMachine() = default;

}}

