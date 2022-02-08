// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "NascentSkeletonMachine.h"
#include "../Assets/TransformationCommands.h"
#include "../RenderUtils.h"
#include "../../Assets/Assets.h"
#include "../../Assets/BlockSerializer.h"
#include "../../Math/Transformations.h"
#include "../../Math/MathSerialization.h"
#include "../../Utility/MemoryUtils.h"
#include "../../Utility/StreamUtils.h"
#include "../../Utility/Streams/SerializationUtils.h"

namespace RenderCore { namespace Assets { namespace GeoProc
{
	std::unique_ptr<Float4x4[]>           NascentSkeletonMachine::GenerateOutputTransforms(
		const Assets::AnimatedParameterSet&   parameterSet) const
	{
		std::unique_ptr<Float4x4[]> result = std::make_unique<Float4x4[]>(size_t(_outputMatrixCount));
		RenderCore::Assets::GenerateOutputTransforms(
			MakeIteratorRange(result.get(), result.get() + _outputMatrixCount),
			&parameterSet, 
			MakeIteratorRange(_commandStream));
		return result;
	}

	void        NascentSkeletonMachine::WriteOutputMarker(StringSection<> skeletonName, StringSection<> jointName)
	{
		uint32_t marker = ~0u;
		if (!TryRegisterJointName(marker, skeletonName, jointName))
			Throw(::Exceptions::BasicLabel("Failure while attempt to register joint name: (%s:%s)", skeletonName.AsString().c_str(), jointName.AsString().c_str()));

		_outputMatrixCount = std::max(_outputMatrixCount, marker+1);
		_commandStream.push_back((uint32)Assets::TransformCommand::WriteOutputMatrix);
		_commandStream.push_back(marker);
	}

	void		NascentSkeletonMachine::Pop(unsigned popCount)
	{
		_pendingPops += popCount;
	}

	void NascentSkeletonMachine::PushCommand(uint32 cmd)
	{
		ResolvePendingPops();
		_commandStream.push_back(cmd);
	}

	void NascentSkeletonMachine::PushCommand(TransformCommand cmd)
	{
		ResolvePendingPops();
		_commandStream.push_back((uint32)cmd);
	}

	void NascentSkeletonMachine::PushCommand(const void* ptr, size_t size)
	{
		ResolvePendingPops();
		assert((size % sizeof(uint32)) == 0);
		_commandStream.insert(_commandStream.end(), (const uint32*)ptr, (const uint32*)PtrAdd(ptr, size));
	}

	void NascentSkeletonMachine::ResolvePendingPops()
	{
		if (_pendingPops) {
			_commandStream.push_back((uint32)Assets::TransformCommand::PopLocalToWorld);
			_commandStream.push_back(_pendingPops);
			_pendingPops = 0;
		}
	}

	void NascentSkeletonMachine::Optimize(ITransformationMachineOptimizer& optimizer)
	{
		ResolvePendingPops();
		auto optimized = OptimizeTransformationMachine(MakeIteratorRange(_commandStream), optimizer);
		_commandStream = std::move(optimized);
	}

	void	NascentSkeletonMachine::RemapOutputMatrices(IteratorRange<const unsigned*> outputMatrixMapping)
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

	void NascentSkeletonMachine::FilterOutputInterface(IteratorRange<const std::pair<std::string, std::string>*> filterIn)
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

    NascentSkeletonMachine::NascentSkeletonMachine() : _pendingPops(0), _outputMatrixCount(0) {}
    NascentSkeletonMachine::~NascentSkeletonMachine() {}

///////////////////////////////////////////////////////////////////////////////////////////////////

    template<> auto NascentSkeletonMachine::GetParameterTables<float>()			-> std::vector<ParameterTag>& { return _float1ParameterNames; }
    template<> auto NascentSkeletonMachine::GetParameterTables<Float3>()		-> std::vector<ParameterTag>& { return _float3ParameterNames; }
    template<> auto NascentSkeletonMachine::GetParameterTables<Float4>()		-> std::vector<ParameterTag>& { return _float4ParameterNames; }
	template<> auto NascentSkeletonMachine::GetParameterTables<Quaternion>()	-> std::vector<ParameterTag>& { return _float4ParameterNames; }
    template<> auto NascentSkeletonMachine::GetParameterTables<Float4x4>()		-> std::vector<ParameterTag>& { return _float4x4ParameterNames; }

	#pragma pack(push)
	#pragma pack(1)
		struct NascentSkeletonInterface_Param    /* must match SkeletonMachine::InputInterface::Parameter */
		{
			uint64				_name;
			uint32				_index;
			AnimSamplerType		_type;
			static const bool SerializeRaw = true;
		};
	#pragma pack(pop)

	template <>
		void    NascentSkeletonMachine::SerializeMethod(::Assets::NascentBlockSerializer& outputSerializer) const
	{
		//
		//		Write the command stream
		//
		outputSerializer.SerializeSubBlock(MakeIteratorRange(_commandStream));
        outputSerializer.SerializeValue(_commandStream.size());
        outputSerializer.SerializeValue(_outputMatrixCount);

		//
		//      We have to write out both an input and an output interface
		//      First input interface...
		//
		std::vector<NascentSkeletonInterface_Param> runTimeInputInterface;
		typedef std::vector<ParameterTag> T;
		std::pair<const T*, AnimSamplerType> tables[] = {
			std::make_pair(&_float1ParameterNames,      AnimSamplerType::Float1),
			std::make_pair(&_float3ParameterNames,      AnimSamplerType::Float3),
			std::make_pair(&_float4ParameterNames,      AnimSamplerType::Float4),
			std::make_pair(&_float4x4ParameterNames,    AnimSamplerType::Float4x4)
		};

		for (unsigned t=0; t<dimof(tables); ++t) {
			for (auto i=tables[t].first->begin(); i!=tables[t].first->end(); ++i) {
				NascentSkeletonInterface_Param p;
				p._type     = tables[t].second;
				p._index    = uint32(std::distance(tables[t].first->begin(), i));
				p._name     = Hash64(*i);
				runTimeInputInterface.push_back(p);
			}
		}
		outputSerializer.SerializeSubBlock(MakeIteratorRange(runTimeInputInterface));
		outputSerializer.SerializeValue(runTimeInputInterface.size());

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

	void NascentSkeletonMachine::SetOutputInterface(IteratorRange<const JointTag*> jointNames)
	{
		_jointTags.clear();
		_jointTags.insert(_jointTags.end(), jointNames.begin(), jointNames.end());
	}

	std::vector<uint64_t> NascentSkeletonMachine::BuildHashedOutputInterface() const
	{
		std::vector<uint64_t> hashedInterface;
		hashedInterface.reserve(_jointTags.size());
		for (const auto&j:_jointTags) hashedInterface.push_back(HashCombine(Hash64(j.first), Hash64(j.second)));
		return hashedInterface;
	}

	std::ostream& SerializationOperator(
		std::ostream& stream, 
		const NascentSkeletonMachine& transMachine, 
		const AnimatedParameterSet& defaultParameters)
	{
		stream << "Output matrices: " << transMachine._jointTags.size() << std::endl;
		stream << "Command stream size: " << transMachine._commandStream.size() * sizeof(uint32) << std::endl;

		const std::vector<NascentSkeletonMachine::ParameterTag>* paramTables[] = {
			&transMachine._float1ParameterNames,
			&transMachine._float3ParameterNames,
			&transMachine._float4ParameterNames,
			&transMachine._float4x4ParameterNames,
		};
		const char* paramTableNames[] = { "float1", "float3", "float4", "float4x4" };

		stream << " --- Animation parameters input interface:" << std::endl;
		for (unsigned t=0; t<dimof(paramTables); ++t) {
			stream << "\tTable of " << paramTableNames[t] << std::endl;
			auto& table = *paramTables[t];
		
			for (unsigned p=0; p<table.size(); ++p) {
				stream << "\t\t[" << p << "] " << table[p];
				switch (t) {
				case 0:
					stream << ", default: " << defaultParameters.GetFloat1Parameters()[p];
					break;
				case 1:
				{
					auto& f3 = defaultParameters.GetFloat3Parameters()[p];
					stream << ", default: " << f3[0] << ", " << f3[1] << ", " << f3[2];
				}
				break;
				case 2:
				{
					auto& f4 = defaultParameters.GetFloat4Parameters()[p];
					stream << ", default: " << f4[0] << ", " << f4[1] << ", " << f4[2] << ", " << f4[3];
				}
				break;
				case 3:
				{
					auto& f4x4 = defaultParameters.GetFloat4x4Parameters()[p];
					stream << ", default: ";
					CompactTransformDescription(stream, f4x4);
				}
				break;
				default:
					stream << "unknown type";
				}
				stream << std::endl;
			}
		}

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
				return std::string();
			},
			[&transMachine](AnimSamplerType samplerType, unsigned parameterIndex) -> std::string
			{
				using T = std::vector<NascentSkeletonMachine::ParameterTag>;
				std::pair<const T*, AnimSamplerType> tables[] = {
					std::make_pair(&transMachine._float1ParameterNames,		AnimSamplerType::Float1),
					std::make_pair(&transMachine._float3ParameterNames,		AnimSamplerType::Float3),
					std::make_pair(&transMachine._float4ParameterNames,		AnimSamplerType::Float4),
					std::make_pair(&transMachine._float4x4ParameterNames,		AnimSamplerType::Float4x4),
					std::make_pair(&transMachine._float4ParameterNames,		AnimSamplerType::Quaternion)
				};

				auto& names = *tables[unsigned(samplerType)].first;
				if (parameterIndex < names.size()) return names[parameterIndex];
				else return std::string();
			});

		{
			auto defaultOutputTransforms = transMachine.GenerateOutputTransforms(defaultParameters);
			stream << " --- Output transforms with default parameters:" << std::endl;
			for (unsigned c=0; c<transMachine.GetOutputMatrixCount(); ++c) {
				stream << "[" << c << "] Local-To-World (" << transMachine._jointTags[c].first << ":" << transMachine._jointTags[c].second << "): ";
				CompactTransformDescription(stream, defaultOutputTransforms[c]);
				stream << std::endl;
			}
		}

		return stream;
	}

    bool    NascentSkeletonMachine::TryRegisterJointName(uint32& outputMarker, StringSection<> skeletonName, StringSection<> jointName)
    {
		outputMarker = (uint32)_jointTags.size();
		_jointTags.push_back({skeletonName.AsString(), jointName.AsString()});	// (note -- not checking for duplicates)
        return true;
    }

}}}


