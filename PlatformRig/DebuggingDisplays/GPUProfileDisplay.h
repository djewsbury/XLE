// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#ifndef _GPU_PROFILE_DISPLAY_H_
#define _GPU_PROFILE_DISPLAY_H_
#pragma once

#include "../../RenderOverlays/DebuggingDisplay.h"

namespace RenderCore { class IAnnotator; }

namespace PlatformRig { namespace Overlays
{
    using namespace RenderOverlays;
    using namespace RenderOverlays::DebuggingDisplay;

    class GPUProfileDisplay : public IWidget ///////////////////////////////////////////////////////////
    {
    public:
        GPUProfileDisplay(RenderCore::IAnnotator& profiler);
        ~GPUProfileDisplay();
        void    Render(IOverlayContext& context, Layout& layout, Interactables&interactables, InterfaceState& interfaceState);
        bool    ProcessInput(InterfaceState& interfaceState, const InputContext& inputContext, const InputSnapshot& input);

		void    ProcessGPUEvents(const void* eventsBufferStart, const void* eventsBufferEnd);

    private:
        typedef float GPUDuration;
        typedef uint64 GPUTime;
        typedef unsigned FrameId;

        static const unsigned DurationHistoryLength = 1024;
        struct Section
        {
            const char* _id;
            GPUDuration _durationHistory[DurationHistoryLength];
            unsigned _durationHistoryLength;
            float _graphMin, _graphMax;
            unsigned _flags;

            enum Flags
            {
                Flag_Pause = 1<<0,
                Flag_Hide = 1<<1
            };
        };
        Section _sections[20];

        class GPUFrameConstruction;
        std::unique_ptr<GPUFrameConstruction> _currentFrame;
        GPUTime _endOfLastFrame;

        RenderCore::IAnnotator* _profiler;
		unsigned _listenerId;

        void    PushSectionInfo(const char id[], GPUTime selfTime);
        static float   ToGPUDuration(GPUTime time, GPUTime frequency);
    };
}}

#endif

