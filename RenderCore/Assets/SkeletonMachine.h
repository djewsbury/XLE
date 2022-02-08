// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "TransformationCommands.h"
#include "../../Math/Vector.h"
#include "../../Math/Matrix.h"
#include "../../Utility/Streams/SerializationUtils.h"
#include "../../Utility/IteratorUtils.h"
#include "../../Utility/StringUtils.h"
#include "../../Core/Types.h"

namespace RenderCore { namespace Assets 
{
    class SkinningBindingBox;
    class RawAnimationCurve;

    #pragma pack(push)
    #pragma pack(1)

    ////////////////////////////////////////////////////////////////////////////////////////////
    //      s k e l e t o n         //

    class SkeletonMachine
    {
    public:
        unsigned                        GetOutputMatrixCount() const        { return _outputMatrixCount; }

        void GenerateOutputTransforms   (   IteratorRange<Float4x4*> output,
                                            IteratorRange<const void*> parameterBlock = {}) const;

		void CalculateParentPointers(IteratorRange<unsigned*> output) const;

        struct OutputInterface
        {
            uint64_t*   _outputMatrixNames = nullptr;
            size_t      _outputMatrixNameCount = 0;
        };

        const OutputInterface&  GetOutputInterface() const  { return _outputInterface; }

		std::vector<StringSection<>> GetOutputMatrixNames() const;
        IteratorRange<const uint32_t*> GetCommandStream() const { return MakeIteratorRange(_commandStream, PtrAdd(_commandStream, _commandStreamSize)); }

        SkeletonMachine();
        ~SkeletonMachine();
    protected:
        uint32_t*			_commandStream;
        size_t              _commandStreamSize;
        unsigned            _outputMatrixCount;

        OutputInterface     _outputInterface;

		SerializableVector<char>		_outputMatrixNames;
    };

    #pragma pack(pop)

}}

