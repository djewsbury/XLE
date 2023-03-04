// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../../Utility/IteratorUtils.h"
#include <string>
#include <memory>

namespace SceneEngine 
{
    class IntersectionTestContext;
    class IIntersectionScene;
}

namespace OSServices { class InputSnapshot; }
namespace RenderCore { class IThreadContext; }
namespace RenderCore { namespace Techniques { class ParsingContext; class IPipelineAcceleratorPool; }}

namespace ToolsRig
{
    class IManipulator
    {
    public:
        virtual bool OnInputEvent(
            const OSServices::InputSnapshot& evnt, 
			const SceneEngine::IntersectionTestContext& hitTestContext,
            const SceneEngine::IIntersectionScene* hitTestScene) = 0;
        virtual void Render(
            RenderCore::IThreadContext& context, 
            RenderCore::Techniques::ParsingContext& parserContext,
            const std::shared_ptr<RenderCore::Techniques::IPipelineAcceleratorPool>& pipelineAccelerators) = 0;

        virtual const char* GetName() const = 0;
        virtual std::string GetStatusText() const = 0;

        template<typename T> struct Parameter
        {
            enum class ScaleType { Linear, Logarithmic };
            size_t _valueOffset = 0;
            T _min = T(0), _max = T(0);
            ScaleType _scaleType = ScaleType::Linear;
            const char* _name = nullptr;
        };

        typedef Parameter<float> FloatParameter;
        typedef Parameter<int> IntParameter;

        struct BoolParameter
        {
            size_t      _valueOffset = 0;
            unsigned    _bitIndex = 0;
            const char* _name = nullptr;
        };

            // (warning -- result will probably contain pointers to internal memory within this manipulator)
        virtual IteratorRange<const FloatParameter*> GetFloatParameters() const = 0;     
        virtual IteratorRange<const BoolParameter*> GetBoolParameters() const = 0;
        virtual IteratorRange<const IntParameter*> GetIntParameters() const = 0;
        virtual void SetActivationState(bool newState) = 0;

        virtual ~IManipulator() = default;
    };
}

