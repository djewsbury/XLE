// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../../Math/Vector.h"
#include "../../Math/Matrix.h"
#include "../../Utility/StringUtils.h"
#include "../../Utility/IteratorUtils.h"
#include "../../OSServices/Log.h"
#include "../../Core/Types.h"
#include <vector>

namespace RenderCore { namespace Assets
{
	enum class TransformCommand : uint32;
	enum class AnimSamplerType;
	class ITransformationMachineOptimizer;
	class AnimatedParameterSet;
}}

namespace RenderCore { namespace Assets { namespace GeoProc
{
    class NascentSkeletonMachine
    {
    public:
        unsigned        GetOutputMatrixCount    () const        { return _outputMatrixCount; }
        bool            IsEmpty                 () const        { return _commandStream.empty(); }

        template<typename Serializer>
            void    SerializeMethod(Serializer& outputSerializer) const;

        std::unique_ptr<Float4x4[]>		GenerateOutputTransforms(const AnimatedParameterSet& parameterSet) const;

		using JointTag = std::pair<std::string, std::string>;
		using ParameterTag = std::string;

		IteratorRange<const JointTag*>	GetOutputInterface() const { return MakeIteratorRange(_jointTags); }
		void							SetOutputInterface(IteratorRange<const JointTag*> jointNames);
		std::vector<uint64_t>			BuildHashedOutputInterface() const;

		T1(Type) bool	TryAddParameter(uint32_t& paramIndex, StringSection<> paramName);

		void			FilterOutputInterface(IteratorRange<const std::pair<std::string, std::string>*> filterIn);

        const std::vector<uint32>&		GetCommandStream() const { return _commandStream; }

        friend std::ostream& SerializationOperator(
			std::ostream& stream, 
			const NascentSkeletonMachine& transMachine, 
			const AnimatedParameterSet& defaultParameters);

        void    PushCommand(uint32 cmd);
        void    PushCommand(TransformCommand cmd);
        void    PushCommand(const void* ptr, size_t size);
		void	WriteOutputMarker(StringSection<> skeletonName, StringSection<> jointName);
		void	Pop(unsigned popCount);

        void    Optimize(ITransformationMachineOptimizer& optimizer);
		void	RemapOutputMatrices(IteratorRange<const unsigned*> outputMatrixMapping);

        NascentSkeletonMachine();
        NascentSkeletonMachine(NascentSkeletonMachine&& machine) = default;
        NascentSkeletonMachine& operator=(NascentSkeletonMachine&& moveFrom) = default;
        ~NascentSkeletonMachine();

    protected:
        std::vector<uint32>     _commandStream;
        unsigned                _outputMatrixCount;
        int						_pendingPops; // Only required during construction

		std::vector<ParameterTag>		_float1ParameterNames;
		std::vector<ParameterTag>		_float3ParameterNames;
		std::vector<ParameterTag>		_float4ParameterNames;
		std::vector<ParameterTag>		_float4x4ParameterNames;

		std::vector<JointTag>			_jointTags;

		template<typename Type>
			std::vector<ParameterTag>& GetParameterTables();

        bool			TryRegisterJointName(uint32_t& outputMarker, StringSection<> skeletonName, StringSection<> jointName);

		void			ResolvePendingPops();
    };

        ////////////////// template implementation //////////////////

    template<typename Type>
        bool NascentSkeletonMachine::TryAddParameter( 
            uint32_t& paramIndex,
			StringSection<> paramName)
    {
        auto& tables = GetParameterTables<Type>();
        auto i = std::find_if(tables.begin(), tables.end(), 
			[paramName](const std::string& s) { return XlEqString(paramName, s); });
        if (i!=tables.end()) {
            paramIndex = (uint32_t)std::distance(tables.begin(), i);
            return true;
        }

        paramIndex = (uint32_t)tables.size();
        tables.push_back(paramName.AsString());
        return true;
    }

}}}



