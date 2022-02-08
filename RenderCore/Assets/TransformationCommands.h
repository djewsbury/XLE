// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../../Assets/BlockSerializer.h"
#include "../../Math/Transformations.h"
#include "../../Math/Quaternion.h"
#include "../../Utility/Streams/SerializationUtils.h"
#include "../../Utility/IteratorUtils.h"
#include <vector>
#include <functional>

namespace Utility { class OutputStream; }

namespace RenderCore { namespace Assets
{
    enum class TransformCommand : uint32_t
    {
        PushLocalToWorld,       // no parameters
        PopLocalToWorld,        // number of transforms to pop (ie, often 1, but sometimes we want to do multiple pops at once)

            //
            //      Static transformation ops
            //
        TransformFloat4x4_Static,       // 4x4 transformation matrix
        Translate_Static,               // X, Y, Z translation
        RotateX_Static,                 // rotation around X
        RotateY_Static,                 // rotation around Y
        RotateZ_Static,                 // rotation around Z
        RotateAxisAngle_Static,                  // Axis X, Y, Z, rotation
        RotateQuaternion_Static,		// Rotate through a quaternion
        UniformScale_Static,            // scalar
        ArbitraryScale_Static,          // X, Y, Z scales

            //
            //      Param'd transformation ops
            //      Simplpe simular to the static ops, but in this
            //      case reading the value from the parameter set
            //
        TransformFloat4x4_Parameter,
        Translate_Parameter,
        RotateX_Parameter,
        RotateY_Parameter,
        RotateZ_Parameter,
        RotateAxisAngle_Parameter,
        RotateQuaternion_Parameter,
        UniformScale_Parameter,
        ArbitraryScale_Parameter,

            //
            //      Binding point
            //      Used to bind to animation parameter set output
            //      Optionally followed by default rotation / scale / translations
            //      Defaulting happens per-component; so (for example) if translation
            //      but not rotation is given by the animation parameters, the
            //      default rotation will be used
            //      (But in cases like this, relative ordering of those components
            //      is not necessarily preserved)
            //
        BindingPoint_0,
        BindingPoint_1,
        BindingPoint_2,
        BindingPoint_3,

        WriteOutputMatrix,
        TransformFloat4x4AndWrite_Static,
        TransformFloat4x4AndWrite_Parameter,

        Comment
    };

        //////////////////////////////////////////////////////////

    void GenerateOutputTransforms(
        IteratorRange<Float4x4*>            result,
        IteratorRange<const void*>          parameterBlock,
        IteratorRange<const uint32_t*>      commandStream);

    const uint32_t* NextTransformationCommand(const uint32_t*);

	/// <summary>For each output marker, calculate the immediate parent</summary>
	/// The parent of a given marker is defines as the first marker we encounter if we traverse back through
	/// the set of commands that affect the state of that given marker.
	///
	/// In effect, if the command stream is generated from a node hierarchy, then the parent will correspond
	/// to the parent from that source hierarchy (barring optimizations that have been performed post conversion) 
	/// This function writes out an array that is indexed by the child output marker index and contains the parent
	/// output marker index (or ~0u if there is none)
	///
	void CalculateParentPointers(
		IteratorRange<uint32_t*>					result,
		IteratorRange<const uint32_t*>				commandStream);

    void TraceTransformationMachine(
        std::ostream&                   outputStream,
        IteratorRange<const uint32_t*>    commandStream,
        std::function<std::string(unsigned)> outputMatrixToName,
        std::function<std::string(unsigned)> parameterToName);

    class ITransformationMachineOptimizer
    {
    public:
        virtual bool CanMergeIntoOutputMatrix(unsigned outputMatrixIndex) const = 0;
        virtual void MergeIntoOutputMatrix(unsigned outputMatrixIndex, const Float4x4& transform) = 0;
        virtual ~ITransformationMachineOptimizer();
    };

	class TransformationMachineOptimizer_Null : public ITransformationMachineOptimizer
	{
	public:
		bool CanMergeIntoOutputMatrix(unsigned outputMatrixIndex) const { return false; } 
		void MergeIntoOutputMatrix(unsigned outputMatrixIndex, const Float4x4& transform) {};
	};

    std::vector<uint32_t> OptimizeTransformationMachine(
        IteratorRange<const uint32_t*> input,
        ITransformationMachineOptimizer& optimizer);

	std::vector<uint32_t> RemapOutputMatrices(
		IteratorRange<const uint32_t*> input,
		IteratorRange<const unsigned*> outputMatrixMapping);

}}

