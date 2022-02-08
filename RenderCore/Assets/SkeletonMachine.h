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
    class AnimatedParameterSet;
    class RawAnimationCurve;

    #pragma pack(push)
    #pragma pack(1)

    ////////////////////////////////////////////////////////////////////////////////////////////
    //      s k e l e t o n         //

    class SkeletonMachine
    {
    public:
        unsigned                        GetOutputMatrixCount() const        { return _outputMatrixCount; }
        const AnimatedParameterSet&     GetDefaultParameters() const        { return _defaultParameters; }

        void GenerateOutputTransforms   (   IteratorRange<Float4x4*> output,
                                            const AnimatedParameterSet*   parameterSet) const;

		void CalculateParentPointers(IteratorRange<unsigned*> output) const;

        struct InputInterface
        {
            struct Parameter
            {
                uint64  _name = ~0ull;
                uint32_t  _index = 0u;
                AnimSamplerType  _type = (AnimSamplerType)0;
            };

            Parameter*  _parameters = nullptr;
            size_t      _parameterCount = 0;
        };

        struct OutputInterface
        {
            uint64_t*   _outputMatrixNames = nullptr;
            size_t      _outputMatrixNameCount = 0;
        };

        const InputInterface&   GetInputInterface() const   { return _inputInterface; }
        const OutputInterface&  GetOutputInterface() const  { return _outputInterface; }

		std::vector<StringSection<>> GetOutputMatrixNames() const;

        SkeletonMachine();
        ~SkeletonMachine();
    protected:
        uint32_t*			_commandStream;
        size_t              _commandStreamSize;
        unsigned            _outputMatrixCount;

        InputInterface      _inputInterface;
        OutputInterface     _outputInterface;

		SerializableVector<char>		_outputMatrixNames;

		AnimatedParameterSet      _defaultParameters;

        const uint32_t*   GetCommandStream()      { return _commandStream; }
        const size_t    GetCommandStreamSize()  { return _commandStreamSize; }
    };

    #pragma pack(pop)

}}

