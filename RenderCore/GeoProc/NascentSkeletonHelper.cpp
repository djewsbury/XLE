// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "NascentSkeletonHelper.h"
#include "../Assets/TransformationCommands.h"
#include "../RenderUtils.h"
#include "../../Assets/Assets.h"
#include "../../Assets/BlockSerializer.h"
#include "../../Math/Transformations.h"
#include "../../Math/MathSerialization.h"
#include "../../Utility/MemoryUtils.h"
#include "../../Utility/StreamUtils.h"
#include "../../Utility/Streams/SerializationUtils.h"

namespace RenderCore { namespace Assets { namespace GeoProc { namespace Internal
{
	std::unique_ptr<Float4x4[]>           NascentSkeletonHelper::GenerateOutputTransforms() const
	{
		std::unique_ptr<Float4x4[]> result = std::make_unique<Float4x4[]>(size_t(_outputMatrixCount));
		RenderCore::Assets::GenerateOutputTransforms(
			MakeIteratorRange(result.get(), result.get() + _outputMatrixCount),
			{},
			MakeIteratorRange(_commandStream));
		return result;
	}

	void        NascentSkeletonHelper::WriteOutputMarker(StringSection<> skeletonName, StringSection<> jointName)
	{
		uint32_t marker = ~0u;
		if (!TryRegisterJointName(marker, skeletonName, jointName))
			Throw(::Exceptions::BasicLabel("Failure while attempt to register joint name: (%s:%s)", skeletonName.AsString().c_str(), jointName.AsString().c_str()));

		_outputMatrixCount = std::max(_outputMatrixCount, marker+1);
		_commandStream.push_back((uint32_t)Assets::TransformCommand::WriteOutputMatrix);
		_commandStream.push_back(marker);
	}

	void		NascentSkeletonHelper::Pop(unsigned popCount)
	{
		_pendingPops += popCount;
	}

	void NascentSkeletonHelper::PushCommand(uint32_t cmd)
	{
		ResolvePendingPops();
		_commandStream.push_back(cmd);
	}

	void NascentSkeletonHelper::PushCommand(TransformCommand cmd)
	{
		ResolvePendingPops();
		_commandStream.push_back((uint32_t)cmd);
	}

	void NascentSkeletonHelper::PushCommand(const void* ptr, size_t size)
	{
		ResolvePendingPops();
		assert((size % sizeof(uint32_t)) == 0);
		_commandStream.insert(_commandStream.end(), (const uint32_t*)ptr, (const uint32_t*)PtrAdd(ptr, size));
	}

	void NascentSkeletonHelper::ResolvePendingPops()
	{
		if (_pendingPops) {
			_commandStream.push_back((uint32_t)Assets::TransformCommand::PopLocalToWorld);
			_commandStream.push_back(_pendingPops);
			_pendingPops = 0;
		}
	}

	void NascentSkeletonHelper::Optimize(ITransformationMachineOptimizer& optimizer)
	{
		ResolvePendingPops();
		auto optimized = OptimizeTransformationMachine(MakeIteratorRange(_commandStream), optimizer);
		_commandStream = std::move(optimized);
	}

	void	NascentSkeletonHelper::RemapOutputMatrices(IteratorRange<const unsigned*> outputMatrixMapping)
	{
		ResolvePendingPops();
		_commandStream = RenderCore::Assets::RemapOutputMatrices(
			MakeIteratorRange(_commandStream), 
			outputMatrixMapping);

		unsigned newOutputMatrixCount = 0;
		for (unsigned c=0; c<std::min((unsigned)outputMatrixMapping.size(), _outputMatrixCount); ++c)
			newOutputMatrixCount = std::max(newOutputMatrixCount, outputMatrixMapping[c]+1);
		_outputMatrixCount = newOutputMatrixCount;
	}

	void NascentSkeletonHelper::FilterOutputInterface(IteratorRange<const std::pair<std::string, std::string>*> filterIn)
	{
		auto oldOutputInterface = GetOutputInterface();
		std::vector<std::pair<std::string, std::string>> newOutputInterface;

		std::vector<unsigned> oldIndexToNew(oldOutputInterface.size(), ~0u);
		for (unsigned c=0; c<oldOutputInterface.size(); c++) {
			auto i = std::find(newOutputInterface.begin(), newOutputInterface.end(), oldOutputInterface[c]);
			if (i!=newOutputInterface.end()) {
				oldIndexToNew[c] = (unsigned)std::distance(newOutputInterface.begin(), i);
			} else {
				auto f = std::find(filterIn.begin(), filterIn.end(), oldOutputInterface[c]);
				if (f!=filterIn.end()) {
					oldIndexToNew[c] = (unsigned)(newOutputInterface.size());
					newOutputInterface.push_back(oldOutputInterface[c]);
				}
			}
		}

		RemapOutputMatrices(MakeIteratorRange(oldIndexToNew));
		SetOutputInterface(MakeIteratorRange(newOutputInterface));
	}

    NascentSkeletonHelper::NascentSkeletonHelper() : _pendingPops(0), _outputMatrixCount(0) {}
    NascentSkeletonHelper::~NascentSkeletonHelper() {}

///////////////////////////////////////////////////////////////////////////////////////////////////

	template <>
		void    NascentSkeletonHelper::SerializeMethod(::Assets::BlockSerializer& outputSerializer) const
	{
		//
		//		Write the command stream
		//
		outputSerializer.SerializeSubBlock(MakeIteratorRange(_commandStream));
        outputSerializer.SerializeValue(_commandStream.size());
        outputSerializer.SerializeValue(_outputMatrixCount);

		//
		//      Now, output interface...
		//
		auto jointHashNames = BuildHashedOutputInterface();
		outputSerializer.SerializeSubBlock(MakeIteratorRange(jointHashNames));
		outputSerializer.SerializeValue(jointHashNames.size());

		std::vector<char> boneNames;
		for (const auto&j:_jointTags) {
			boneNames.insert(boneNames.end(), j.second.begin(), j.second.end());
			boneNames.push_back(0);
		}
		SerializationOperator(outputSerializer, boneNames);
	}

	void NascentSkeletonHelper::SetOutputInterface(IteratorRange<const JointTag*> jointNames)
	{
		_jointTags.clear();
		_jointTags.insert(_jointTags.end(), jointNames.begin(), jointNames.end());
	}

	std::vector<uint64_t> NascentSkeletonHelper::BuildHashedOutputInterface() const
	{
		std::vector<uint64_t> hashedInterface;
		hashedInterface.reserve(_jointTags.size());
		for (const auto&j:_jointTags) hashedInterface.push_back(HashCombine(Hash64(j.first), Hash64(j.second)));
		return hashedInterface;
	}

	std::ostream& SerializationOperator(
		std::ostream& stream, 
		const NascentSkeletonHelper& transMachine)
	{
		stream << "Output matrices: " << transMachine._jointTags.size() << std::endl;
		stream << "Command stream size: " << transMachine._commandStream.size() * sizeof(uint32_t) << std::endl;

		stream << " --- Output interface:" << std::endl;
		for (auto i=transMachine._jointTags.begin(); i!=transMachine._jointTags.end(); ++i)
			stream << "  [" << std::distance(transMachine._jointTags.begin(), i) << "] " << i->first << " : " << i->second << ", Output transform index: (" << std::distance(transMachine._jointTags.begin(), i) << ")" << std::endl;

		stream << " --- Command stream:" << std::endl;
		const auto& cmds = transMachine._commandStream;
		TraceTransformationMachine(
			stream, MakeIteratorRange(cmds),
			[&transMachine](unsigned outputMatrixIndex) -> std::string
			{
				if (outputMatrixIndex < transMachine._jointTags.size())
					return transMachine._jointTags[outputMatrixIndex].first + " : " + transMachine._jointTags[outputMatrixIndex].second;
				return {};
			},
			[paramDehashTable=MakeIteratorRange(transMachine._parameterDehashTable)](uint64_t parameterHash) -> std::string
			{
				auto i = LowerBound2(paramDehashTable, parameterHash);
				if (i != paramDehashTable.end() && i->first == parameterHash)
					return i->second;
				StringMeld<64> meld;
				meld << "0x" << std::hex << parameterHash;
				return meld.AsString();
			});

		{
			auto defaultOutputTransforms = transMachine.GenerateOutputTransforms();
			stream << " --- Output transforms with default parameters:" << std::endl;
			for (unsigned c=0; c<transMachine.GetOutputMatrixCount(); ++c) {
				stream << "[" << c << "] Local-To-World (" << transMachine._jointTags[c].first << ":" << transMachine._jointTags[c].second << "): ";
				CompactTransformDescription(stream, defaultOutputTransforms[c]);
				stream << std::endl;
			}
		}

		return stream;
	}

    bool    NascentSkeletonHelper::TryRegisterJointName(uint32_t& outputMarker, StringSection<> skeletonName, StringSection<> jointName)
    {
		outputMarker = (uint32_t)_jointTags.size();
		_jointTags.push_back({skeletonName.AsString(), jointName.AsString()});	// (note -- not checking for duplicates)
        return true;
    }

}}}}


