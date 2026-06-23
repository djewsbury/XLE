// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../../Math/Matrix.h"
#include "../../Utility/Streams/SerializationUtils.h"
#include "../../Utility/IteratorUtils.h"
#include "../../Utility/StringUtils.h"

namespace RenderCore { namespace Assets 
{
    #pragma pack(push)
    #pragma pack(1)

    ////////////////////////////////////////////////////////////////////////////////////////////
    //      s k e l e t o n         //

    class SkeletonMachine
    {
    public:
        unsigned GetOutputMatrixCount() const        { return _outputInterface.size(); }

        void GenerateOutputTransforms   (   IteratorRange<Float4x4*> output,
                                            IteratorRange<const void*> parameterBlock = {}) const;

		void CalculateParentPointers(IteratorRange<unsigned*> output) const;

        using OutputInterface = IteratorRange<const uint64_t*>;
        OutputInterface  GetOutputInterface() const  { return _outputInterface; }

		std::vector<StringSection<>> GetOutputMatrixNames() const;
        IteratorRange<const uint32_t*> GetCommandStream() const { return _commandStream; }

        SkeletonMachine();
        ~SkeletonMachine();
    protected:
        SerializableVector<uint32_t>    _commandStream;
        SerializableVector<uint64_t>    _outputInterface;
		SerializableVector<char>		_outputMatrixNames;
    };

    #pragma pack(pop)

}}

