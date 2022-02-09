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
	enum class TransformCommand : uint32_t;
	class ITransformationMachineOptimizer;
}}

namespace RenderCore { namespace Assets { namespace GeoProc { namespace Internal
{
    class NascentSkeletonHelper
    {
    public:
        unsigned        GetOutputMatrixCount    () const        { return _outputMatrixCount; }
        bool            IsEmpty                 () const        { return _commandStream.empty(); }

        template<typename Serializer>
            void    SerializeMethod(Serializer& outputSerializer) const;

        std::unique_ptr<Float4x4[]>		GenerateOutputTransforms() const;

		using JointTag = std::pair<std::string, std::string>;

		IteratorRange<const JointTag*>	GetOutputInterface() const { return MakeIteratorRange(_jointTags); }
		void							SetOutputInterface(IteratorRange<const JointTag*> jointNames);
		std::vector<uint64_t>			BuildHashedOutputInterface() const;

		void			FilterOutputInterface(IteratorRange<const std::pair<std::string, std::string>*> filterIn);

        const std::vector<uint32_t>&		GetCommandStream() const { return _commandStream; }

        friend std::ostream& SerializationOperator(
			std::ostream& stream, 
			const NascentSkeletonHelper& transMachine);

        void    PushCommand(uint32_t cmd);
        void    PushCommand(TransformCommand cmd);
        void    PushCommand(const void* ptr, size_t size);
		void	WriteOutputMarker(StringSection<> skeletonName, StringSection<> jointName);
		void	Pop(unsigned popCount);

        void    Optimize(ITransformationMachineOptimizer& optimizer);
		void	RemapOutputMatrices(IteratorRange<const unsigned*> outputMatrixMapping);

        NascentSkeletonHelper();
        NascentSkeletonHelper(NascentSkeletonHelper&& machine) = default;
        NascentSkeletonHelper& operator=(NascentSkeletonHelper&& moveFrom) = default;
        ~NascentSkeletonHelper();

    protected:
        std::vector<uint32_t>     _commandStream;
        unsigned                _outputMatrixCount;
        int						_pendingPops; // Only required during construction

		std::vector<JointTag>			_jointTags;

        bool			TryRegisterJointName(uint32_t& outputMarker, StringSection<> skeletonName, StringSection<> jointName);

		void			ResolvePendingPops();
    };

}}}}



