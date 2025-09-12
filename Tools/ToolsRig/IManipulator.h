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
namespace RenderOverlays { class IOverlayContext; }
namespace PlatformRig { enum class ProcessInputResult; }

namespace ToolsRig
{
    class IManipulator
    {
    public:
        virtual PlatformRig::ProcessInputResult OnInputEvent(
            const OSServices::InputSnapshot& evnt, 
			const SceneEngine::IntersectionTestContext& hitTestContext) = 0;
        virtual void Render(RenderOverlays::IOverlayContext& overlayContext) = 0;

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
        virtual IteratorRange<const FloatParameter*> GetFloatParameters() const;
        virtual IteratorRange<const BoolParameter*> GetBoolParameters() const;
        virtual IteratorRange<const IntParameter*> GetIntParameters() const;
        virtual void SetActivationState(bool newState);
        virtual bool GetActivationState() const;

        virtual ~IManipulator();
    };
}

