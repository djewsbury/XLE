// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "FrameRig.h"
#include "AllocationProfiler.h"
#include "OverlaySystem.h"
#include "PlatformApparatuses.h"

#include "../RenderCore/IThreadContext.h"
#include "../RenderCore/IAnnotator.h"
#include "../RenderCore/IDevice.h"
#include "../RenderOverlays/DebuggingDisplay.h"
#include "../RenderOverlays/OverlayContext.h"
#include "../RenderOverlays/Font.h"

#include "../RenderCore/Techniques/ParsingContext.h"
#include "../RenderCore/Techniques/RenderPassUtils.h"
#include "../RenderCore/Techniques/RenderPass.h"
#include "../RenderCore/Techniques/CommonBindings.h"
#include "../RenderCore/Techniques/ImmediateDrawables.h"
#include "../RenderCore/Techniques/SubFrameEvents.h"
#include "../RenderCore/Techniques/Techniques.h"
#include "../RenderCore/Techniques/Services.h"
#include "../RenderCore/Techniques/Apparatuses.h"
#include "../RenderCore/Techniques/DeferredShaderResource.h"
#include "../BufferUploads/IBufferUploads.h"

#include "../Assets/Assets.h"

#include "../OSServices/Log.h"
#include "../OSServices/TimeUtils.h"
#include "../ConsoleRig/ResourceBox.h"
#include "../ConsoleRig/Console.h"
#include "../Utility/IntrusivePtr.h"
#include "../Utility/StringFormat.h"
#include "../Utility/Profiling/CPUProfiler.h"

#include <tuple>
#include <iomanip>

#include "../RenderCore/Metal/DeviceContext.h"

#include "../Assets/CompileAndAsyncManager.h"
#include "../Assets/IntermediatesStore.h"
#include "../Assets/AssetServices.h"

namespace PlatformRig
{
    using namespace RenderOverlays;
    using namespace RenderOverlays::DebuggingDisplay;

    class FrameRateRecorder
    {
    public:
        void PushFrameDuration(uint64_t duration);
        std::tuple<float, float, float> GetPerformanceStats() const;

        FrameRateRecorder();
        ~FrameRateRecorder();

    private:
        uint64_t      _frequency;
        uint64_t      _durationHistory[64];
        unsigned    _bufferStart, _bufferEnd;
    };

    class FrameRig::Pimpl
    {
    public:
        AccumulatedAllocations::Snapshot _prevFrameAllocationCount;
        FrameRateRecorder _frameRate;
        uint64_t _prevFrameStartTime;
        float _timerToSeconds;
        unsigned _frameRenderCount;
        uint64_t _frameLimiter;
        uint64_t _timerFrequency;

        uint64_t _mainOverlayRigTargetConfig = 0;
        uint64_t _debugScreensTargetConfig = 0; 

        Pimpl()
        : _prevFrameStartTime(0) 
        , _timerFrequency(OSServices::GetPerformanceCounterFrequency())
        , _frameRenderCount(0)
        , _frameLimiter(0)
        {
            _timerToSeconds = 1.0f / float(_timerFrequency);
        }
    };

    class FrameRigResources
    {
    public:
        std::shared_ptr<RenderOverlays::Font> _frameRateFont;
        std::shared_ptr<RenderOverlays::Font> _smallFrameRateFont;
        std::shared_ptr<RenderOverlays::Font> _tabHeadingFont;

        FrameRigResources(
            std::shared_ptr<RenderOverlays::Font> frameRateFont,
            std::shared_ptr<RenderOverlays::Font> smallFrameRateFont,
            std::shared_ptr<RenderOverlays::Font> tabHeadingFont)
        : _frameRateFont(std::move(frameRateFont)), _smallFrameRateFont(std::move(smallFrameRateFont)), _tabHeadingFont(std::move(tabHeadingFont))
        {}

        static void ConstructToFuture(::Assets::FuturePtr<FrameRigResources>& future)
        {
            ::Assets::WhenAll(
                RenderOverlays::MakeFont("Shojumaru", 32),
                RenderOverlays::MakeFont("PoiretOne", 14),
                RenderOverlays::MakeFont("Raleway", 20)).ThenConstructToFuture(future);
        }
    };

///////////////////////////////////////////////////////////////////////////////

    auto FrameRig::ExecuteFrame(
        std::shared_ptr<RenderCore::IThreadContext> context,
        RenderCore::IPresentationChain& presChain,
		RenderCore::Techniques::ParsingContext& parserContext,
        HierarchicalCPUProfiler* cpuProfiler) -> FrameResult
    {
        CPUProfileEvent_Conditional pEvnt("FrameRig::ExecuteFrame", cpuProfiler);
        assert(&parserContext.GetThreadContext() == context.get());

        uint64_t startTime = OSServices::GetPerformanceCounter();
        if (_pimpl->_frameLimiter) {
            CPUProfileEvent_Conditional pEvnt2("FrameLimiter", cpuProfiler);
            while (startTime < _pimpl->_prevFrameStartTime + _pimpl->_frameLimiter) {
                Threading::YieldTimeSlice();
                startTime = OSServices::GetPerformanceCounter();
            }
        }

        #if defined(_DEBUG)
            const bool intermittentlyCommitAssets = true;
            if (intermittentlyCommitAssets) {
                using namespace std::chrono_literals;
                static auto startTime = std::chrono::steady_clock::now();
                if ((std::chrono::steady_clock::now() - startTime) > 20s) {
                    ::Assets::Services::GetAsyncMan().GetIntermediateStore()->FlushToDisk();
                    startTime = std::chrono::steady_clock::now();
                }
            }
        #endif

        float frameElapsedTime = 1.f/60.f;
        if (_pimpl->_prevFrameStartTime!=0) {
            frameElapsedTime = (startTime - _pimpl->_prevFrameStartTime) * _pimpl->_timerToSeconds;
        }
        _pimpl->_prevFrameStartTime = startTime;

        RenderCore::Techniques::SetThreadContext(context);

        bool endAnnotatorFrame = false;
		TRY {

            auto presentationTarget = context->BeginFrame(presChain);
            auto presentationTargetDesc = presentationTarget->GetDesc();

            context->GetAnnotator().Frame_Begin(_pimpl->_frameRenderCount);		// (on Vulkan, we must do this after IThreadContext::BeginFrame(), because that primes the command list in the vulkan device)
            endAnnotatorFrame = true;

                //  We must invalidate the cached state at least once per frame.
                //  It appears that the driver might forget bound constant buffers
                //  during the begin frame or present
            context->InvalidateCachedState();

			// Bind the presentation target as the default output for the parser context
			// (including setting the normalized width and height)
			parserContext.GetTechniqueContext()._attachmentPool->Bind(RenderCore::Techniques::AttachmentSemantics::ColorLDR, presentationTarget);
            auto targetDesc = presentationTarget->GetDesc();
            auto& stitchingContext = parserContext.GetFragmentStitchingContext();
            stitchingContext.DefineAttachment(
                RenderCore::Techniques::PreregisteredAttachment {
                    RenderCore::Techniques::AttachmentSemantics::ColorLDR,
                    targetDesc,
                    RenderCore::Techniques::PreregisteredAttachment::State::Uninitialized,
                    RenderCore::BindFlag::PresentationSrc
                });
            stitchingContext._workingProps = RenderCore::FrameBufferProperties { targetDesc._textureDesc._width, targetDesc._textureDesc._height };
            parserContext.GetViewport() = RenderCore::ViewportDesc { 0.f, 0.f, (float)targetDesc._textureDesc._width, (float)targetDesc._textureDesc._height };

			////////////////////////////////

            bool mainOverlaySucceeded = false;
			TRY {
				if (_mainOverlaySys) {
                    #if defined(_DEBUG)
                        assert(_pimpl->_mainOverlayRigTargetConfig == RenderCore::Techniques::HashPreregisteredAttachments(stitchingContext.GetPreregisteredAttachments(), stitchingContext._workingProps));
                    #endif
                    _mainOverlaySys->Render(parserContext);
                    mainOverlaySucceeded = true;
                } 
			}
			CATCH_ASSETS(parserContext)
			CATCH(const std::exception& e) {
				StringMeldAppend(parserContext._stringHelpers->_errorString) << "Exception in main overlay system render: " << e.what() << "\n";
			}
			CATCH_END

            if (!mainOverlaySucceeded) {
                // We must at least clear, because the _debugScreenOverlaySystem might have something to render
                // (also redefine AttachmentSemantics::ColorLDR as initialized here)
                RenderCore::Metal::DeviceContext::Get(*context)->Clear(*presentationTarget->CreateTextureView(RenderCore::BindFlag::RenderTarget), Float4(0,0,0,1));
                using namespace RenderCore::Techniques;
                stitchingContext.DefineAttachment(PreregisteredAttachment {AttachmentSemantics::ColorLDR, targetDesc, PreregisteredAttachment::State::Initialized});
            }

			TRY {
				if (_debugScreenOverlaySystem)
                    _debugScreenOverlaySystem->Render(parserContext);
			}
			CATCH_ASSETS(parserContext)
			CATCH(const std::exception& e) {
				StringMeldAppend(parserContext._stringHelpers->_errorString) << "Exception in debug screens overlay system render: " << e.what() << "\n";
			}
			CATCH_END

			////////////////////////////////

			// auto f = _pimpl->_frameRate.GetPerformanceStats();
			// auto heapMetrics = AccumulatedAllocations::GetCurrentHeapMetrics();
			// 
			// DrawFrameRate(
			//     context, res._frameRateFont.get(), res._smallDebugFont.get(), std::get<0>(f), std::get<1>(f), std::get<2>(f), 
			//     heapMetrics._usage, _pimpl->_prevFrameAllocationCount._allocationCount);

			{
				if (Tweakable("FrameRigStats", false) && (_pimpl->_frameRenderCount % 64) == (64-1)) {
					auto f = _pimpl->_frameRate.GetPerformanceStats();
					Log(Verbose) << "Ave FPS: " << 1000.f / std::get<0>(f) << std::endl;
						// todo -- we should get a rolling average of these values
					if (_pimpl->_prevFrameAllocationCount._allocationCount) {
						Log(Verbose) << "(" << _pimpl->_prevFrameAllocationCount._freeCount << ") frees and (" << _pimpl->_prevFrameAllocationCount._allocationCount << ") allocs during frame. Ave alloc: (" << _pimpl->_prevFrameAllocationCount._allocationsSize / _pimpl->_prevFrameAllocationCount._allocationCount << ")." << std::endl;
					}
				}
			}

			parserContext.GetTechniqueContext()._attachmentPool->UnbindAll();

            if (_subFrameEvents)
                _subFrameEvents->_onPrePresent.Invoke(*context);

            if (parserContext._requiredBufferUploadsCommandList)
                RenderCore::Techniques::Services::GetBufferUploads().StallUntilCompletion(*context, parserContext._requiredBufferUploadsCommandList);

            RenderCore::Metal::Internal::SetImageLayout(
                *RenderCore::Metal::DeviceContext::Get(*context), *checked_cast<RenderCore::Metal::Resource*>(presentationTarget.get()),
                RenderCore::Metal::Internal::ImageLayout::ColorAttachmentOptimal, 0, VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT,
                RenderCore::Metal::Internal::ImageLayout::PresentSrc, 0, VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT);

            context->GetAnnotator().Frame_End();        // calling Frame_End() can prevent creating a new command list immediately after the Present() call (which ends the previous command list)

			{
				CPUProfileEvent_Conditional pEvnt2("Present", cpuProfiler);
				context->Present(presChain);
			}

            if (_subFrameEvents)
                _subFrameEvents->_onPostPresent.Invoke(*context);

		} CATCH(const std::exception& e) {
			Log(Error) << "Suppressed error in frame rig render: " << e.what() << std::endl;
		    if (endAnnotatorFrame)
                context->GetAnnotator().Frame_End();
            RenderCore::Techniques::SetThreadContext(nullptr);
	    } CATCH_END
	
        if (_subFrameEvents)
            _subFrameEvents->_onFrameBarrier.Invoke();

        uint64_t duration = OSServices::GetPerformanceCounter() - startTime;
        _pimpl->_frameRate.PushFrameDuration(duration);
        ++_pimpl->_frameRenderCount;
        auto accAlloc = AccumulatedAllocations::GetInstance();
        if (accAlloc) {
            _pimpl->_prevFrameAllocationCount = accAlloc->GetAndClear();
        }

        if (parserContext.HasPendingAssets()) {
            ::Threading::Sleep(16);  // slow down while we're building pending resources
        } else {
            Threading::YieldTimeSlice();    // this might be too extreme. We risk not getting execution back for a long while
        }

        return { frameElapsedTime, parserContext.HasPendingAssets() };
    }

    auto FrameRig::ExecuteFrame(
        std::shared_ptr<RenderCore::IThreadContext> context,
        RenderCore::IPresentationChain& presChain,
        RenderCore::Techniques::FrameRenderingApparatus& frameRenderingApparatus,
        RenderCore::Techniques::DrawingApparatus* drawingApparatus) -> FrameResult
    {
        RenderCore::Techniques::TechniqueContext techniqueContext;
        if (drawingApparatus) {
            techniqueContext._commonResources = drawingApparatus->_commonResources;
            techniqueContext._drawablesPacketsPool = drawingApparatus->_drawablesPacketsPool;
            techniqueContext._graphicsPipelinePool = drawingApparatus->_graphicsPipelinePool;
            techniqueContext._uniformDelegateManager = drawingApparatus->_mainUniformDelegateManager;
        }
        techniqueContext._attachmentPool = frameRenderingApparatus._attachmentPool;
        techniqueContext._frameBufferPool = frameRenderingApparatus._frameBufferPool;
        RenderCore::Techniques::ParsingContext parserContext{techniqueContext, *context};
        return ExecuteFrame(
            std::move(context), presChain,
            parserContext, 
            frameRenderingApparatus._frameCPUProfiler.get());
    }

    auto FrameRig::ExecuteFrame(
        WindowApparatus& windowApparatus,
        RenderCore::Techniques::FrameRenderingApparatus& frameRenderingApparatus,
        RenderCore::Techniques::DrawingApparatus* drawingApparatus) -> FrameResult
    {
        return ExecuteFrame(
            windowApparatus._immediateContext, *windowApparatus._presentationChain,
            frameRenderingApparatus, drawingApparatus);
    }

    void FrameRig::UpdatePresentationChain(RenderCore::IPresentationChain& presChain)
    {
        auto desc = presChain.GetDesc();

        using namespace RenderCore;
        auto targetDesc = CreateDesc(
            desc->_bindFlags, 
            0, GPUAccess::Write,
            TextureDesc::Plain2D(desc->_width, desc->_height, desc->_format, 1, 0, desc->_samples),
            "presentation-target");

        auto fbProps = RenderCore::FrameBufferProperties { desc->_width, desc->_height, desc->_samples };
        if (_mainOverlaySys) {
            std::vector<RenderCore::Techniques::PreregisteredAttachment> preregisteredAttachments;
            preregisteredAttachments.push_back(
                RenderCore::Techniques::PreregisteredAttachment {
                    RenderCore::Techniques::AttachmentSemantics::ColorLDR,
                    targetDesc,
                    RenderCore::Techniques::PreregisteredAttachment::State::Uninitialized
                });
            _mainOverlaySys->OnRenderTargetUpdate(MakeIteratorRange(preregisteredAttachments), fbProps);
            _pimpl->_mainOverlayRigTargetConfig = RenderCore::Techniques::HashPreregisteredAttachments(MakeIteratorRange(preregisteredAttachments), fbProps);
        } else {
            _pimpl->_mainOverlayRigTargetConfig = 0;
        }

        if (_debugScreenOverlaySystem) {
            std::vector<RenderCore::Techniques::PreregisteredAttachment> preregisteredAttachments;
            preregisteredAttachments.push_back(
                RenderCore::Techniques::PreregisteredAttachment {
                    RenderCore::Techniques::AttachmentSemantics::ColorLDR,
                    targetDesc,
                    RenderCore::Techniques::PreregisteredAttachment::State::Initialized,
                    RenderCore::BindFlag::RenderTarget
                });
            _debugScreenOverlaySystem->OnRenderTargetUpdate(MakeIteratorRange(preregisteredAttachments), fbProps);
            _pimpl->_debugScreensTargetConfig = RenderCore::Techniques::HashPreregisteredAttachments(MakeIteratorRange(preregisteredAttachments), fbProps);
        } else {
            _pimpl->_debugScreensTargetConfig = 0;
        }
    }

    void FrameRig::SetFrameLimiter(unsigned maxFPS)
    {
        if (maxFPS) { _pimpl->_frameLimiter = _pimpl->_timerFrequency / uint64_t(maxFPS); }
        else { _pimpl->_frameLimiter = 0; }
    }

    void FrameRig::SetMainOverlaySystem(std::shared_ptr<IOverlaySystem> overlaySystem)
    {
        _mainOverlaySys = std::move(overlaySystem);
    }
    
    void FrameRig::SetDebugScreensOverlaySystem(std::shared_ptr<IOverlaySystem> overlaySystem)
    {
        _debugScreenOverlaySystem = std::move(overlaySystem);
    }

    FrameRig::FrameRig(
        const std::shared_ptr<RenderCore::Techniques::SubFrameEvents>& subFrameEvents)
    : _subFrameEvents(subFrameEvents)
    {
        _pimpl = std::make_unique<Pimpl>();

		Log(Verbose) << "---- Beginning FrameRig ------------------------------------------------------------------" << std::endl;
        auto accAlloc = AccumulatedAllocations::GetInstance();
        if (accAlloc) {
            auto acc = accAlloc->GetAndClear();
            if (acc._allocationCount)
                Log(Verbose) << "(" << acc._freeCount << ") frees and (" << acc._allocationCount << ") allocs during startup. Ave alloc: (" << acc._allocationsSize / acc._allocationCount << ")." << std::endl;
            auto metrics = accAlloc->GetCurrentHeapMetrics();
            if (metrics._blockCount)
                Log(Verbose) << "(" << metrics._blockCount << ") active normal block allocations in (" << metrics._usage / (1024.f*1024.f) << "M bytes). Ave: (" << metrics._usage / metrics._blockCount << ")." << std::endl;
        }
    }

    FrameRig::~FrameRig() 
    {
    }

///////////////////////////////////////////////////////////////////////////////

    void FrameRateRecorder::PushFrameDuration(uint64_t duration)
    {
            // (note, in this scheme, one entry is always empty -- so actual capacity is really dimof(_durationHistory)-1)
        _durationHistory[_bufferEnd] = duration;
        _bufferEnd      = (_bufferEnd+1)%dimof(_durationHistory);
        if (_bufferEnd == _bufferStart) {
            _bufferStart = (_bufferStart+1)%dimof(_durationHistory);
        }
    }

    std::tuple<float, float, float> FrameRateRecorder::GetPerformanceStats() const
    {
        unsigned    entryCount = 0;
        uint64_t      accumulation = 0;
        uint64_t      minTime = std::numeric_limits<uint64_t>::max(), maxTime = 0;
        for (unsigned c=_bufferStart; c!=_bufferEnd; c=(c+1)%dimof(_durationHistory)) {
            accumulation += _durationHistory[c];
            minTime = std::min(minTime, _durationHistory[c]);
            maxTime = std::max(maxTime, _durationHistory[c]);
            ++entryCount;
        }

        double averageDuration = double(accumulation) / double(_frequency/1000) / double(entryCount);
        double minDuration = minTime / double(_frequency/1000);
        double maxDuration = maxTime / double(_frequency/1000);
        return std::make_tuple(float(averageDuration), float(minDuration), float(maxDuration));
    }

    FrameRateRecorder::FrameRateRecorder()
    {
        _bufferStart = _bufferEnd = 0;
        _frequency = OSServices::GetPerformanceCounterFrequency();
    }

    FrameRateRecorder::~FrameRateRecorder() {}

///////////////////////////////////////////////////////////////////////////////

    static const InteractableId Id_FrameRigDisplayMain = InteractableId_Make("FrameRig");
    static const InteractableId Id_FrameRigDisplaySubMenu = InteractableId_Make("FrameRigSubMenu");

    template<typename T> inline const T& FormatButton(InterfaceState& interfaceState, InteractableId id, const T& normalState, const T& mouseOverState, const T& pressedState)
    {
        if (interfaceState.HasMouseOver(id))
            return interfaceState.IsMouseButtonHeld(0)?pressedState:mouseOverState;
        return normalState;
    }

    static const std::string String_IconBegin("xleres/defaultresources/icon_");
    static const std::string String_IconEnd(".png");

    class FrameRigDisplay : public RenderOverlays::DebuggingDisplay::IWidget
    {
    public:
        void    Render(IOverlayContext& context, Layout& layout, Interactables&interactables, InterfaceState& interfaceState);
        ProcessInputResult    ProcessInput(InterfaceState& interfaceState, const InputSnapshot& input);

        FrameRigDisplay(
            std::shared_ptr<DebugScreensSystem> debugSystem,
            const AccumulatedAllocations::Snapshot& prevFrameAllocationCount, const FrameRateRecorder& frameRate);
        ~FrameRigDisplay();
    protected:
        const AccumulatedAllocations::Snapshot* _prevFrameAllocationCount;
        const FrameRateRecorder* _frameRate;
        unsigned _subMenuOpen;

        std::weak_ptr<DebugScreensSystem> _debugSystem;
    };

    void    FrameRigDisplay::Render(IOverlayContext& context, Layout& layout, 
                                    Interactables&interactables, InterfaceState& interfaceState)
    {
        auto* res = ConsoleRig::TryActualizeCachedBox<FrameRigResources>();
        if (!res) return;

        using namespace RenderOverlays;
        using namespace RenderOverlays::DebuggingDisplay;
        auto outerRect = layout.GetMaximumSize();

        static Coord rectWidth = 175;
        static Coord padding = 12;
        static Coord margin = 8;
        const auto bigLineHeight = Coord(res->_frameRateFont->GetFontProperties()._lineHeight);
        const auto smallLineHeight = Coord(res->_smallFrameRateFont->GetFontProperties()._lineHeight);
        const auto tabHeadingLineHeight = Coord(res->_tabHeadingFont->GetFontProperties()._lineHeight);
        const Coord rectHeight = bigLineHeight + 3 * margin + smallLineHeight;
        Rect displayRect(
            Coord2(outerRect._bottomRight[0] - rectWidth - padding, outerRect._topLeft[1] + padding),
            Coord2(outerRect._bottomRight[0] - padding, outerRect._topLeft[1] + padding + rectHeight));
        Layout innerLayout(displayRect);
        innerLayout._paddingInternalBorder = margin;
        innerLayout._paddingBetweenAllocations = margin;

        static ColorB normalColor = ColorB(70, 31, 0, 0x9f);
        static ColorB mouseOverColor = ColorB(70, 31, 0, 0xff);
        static ColorB pressed = ColorB(128, 50, 0, 0xff);
        FillAndOutlineRoundedRectangle(context, displayRect, 
            FormatButton(interfaceState, Id_FrameRigDisplayMain, normalColor, mouseOverColor, pressed), 
            ColorB::White,
            (interfaceState.HasMouseOver(Id_FrameRigDisplayMain))?4.f:2.f, 1.f / 4.f);

        static ColorB menuBkgrnd(128, 96, 64, 64);
        static ColorB menuBkgrndHigh(128, 96, 64, 192);
        static ColorB tabHeaderColor(0xffffffff);

        auto f = _frameRate->GetPerformanceStats();

        DrawText()
            .Alignment(TextAlignment::Left)
            .Font(*res->_frameRateFont)
            .Draw(context, innerLayout.Allocate(Coord2(80, bigLineHeight)), StringMeld<64>() << std::setprecision(1) << std::fixed << 1000.f / std::get<0>(f));

        DrawText()
            .Font(*res->_smallFrameRateFont)
            .Alignment(TextAlignment::Left)
			.Draw(context, innerLayout.Allocate(Coord2(rectWidth - 80 - innerLayout._paddingInternalBorder*2 - innerLayout._paddingBetweenAllocations, smallLineHeight * 2)), StringMeld<64>() << std::setprecision(1) << std::fixed << (1000.f / std::get<2>(f)) << "-" << (1000.f / std::get<1>(f)));

        auto heapMetrics = AccumulatedAllocations::GetCurrentHeapMetrics();
        auto frameAllocations = _prevFrameAllocationCount->_allocationCount;

        DrawText()
            .Alignment(TextAlignment::Center)
            .FormatAndDraw(context, innerLayout.AllocateFullWidth(smallLineHeight), "%.2fM (%i)", heapMetrics._usage / (1024.f*1024.f), frameAllocations);

        interactables.Register({displayRect, Id_FrameRigDisplayMain});

        auto ds = _debugSystem.lock();
        if (ds) {
            const char* categories[] = {
                "Console", "Terrain", "Browser", "Placements", "Profiler", "Settings", "Test"
            };
            if (_subMenuOpen && (_subMenuOpen-1) < dimof(categories)) {
                    // draw menu of available debug screens
                const Coord2 iconSize(93/2, 88/2);
                unsigned menuHeight = 0;
                Coord2 pt = displayRect._bottomRight + Coord2(0, margin);
                for (signed c=dimof(categories)-1; c>=0; --c) {

                    bool highlight = interfaceState.HasMouseOver(Id_FrameRigDisplaySubMenu+c);

                    Rect rect;
                    if ((_subMenuOpen-1) == unsigned(c) || highlight) {

                            //  Draw the text name for this icon under the icon
                        Coord nameWidth = (Coord)StringWidth(*res->_tabHeadingFont, MakeStringSection(categories[c]));
                        rect = Rect(
                            pt - Coord2(std::max(iconSize[0], nameWidth), 0),
                            pt + Coord2(0, Coord(iconSize[1] + res->_tabHeadingFont->GetFontProperties()._lineHeight)));

                        auto iconLeft = Coord((rect._topLeft[0] + rect._bottomRight[0] - iconSize[0]) / 2.f);
                        Coord2 iconTopLeft(iconLeft, rect._topLeft[1]);
                        Rect iconRect(iconTopLeft, iconTopLeft + iconSize);

                        FillRectangle(context, rect, menuBkgrnd);

                        auto texture = ::Assets::Actualize<RenderCore::Techniques::DeferredShaderResource>(String_IconBegin + categories[c] + String_IconEnd);
                        context.RequireCommandList(texture->GetCompletionCommandList());
                        context.DrawTexturedQuad(
                            ProjectionMode::P2D, 
                            AsPixelCoords(iconRect._topLeft),
                            AsPixelCoords(iconRect._bottomRight),
                            texture->GetShaderResource());
                        DrawText()
                            .Color(tabHeaderColor)
                            .Alignment(TextAlignment::Bottom)
                            .Draw(context, rect, categories[c]);

                    } else {

                        rect = Rect(pt - Coord2(iconSize[0], 0), pt + Coord2(0, iconSize[1]));
                        auto texture = ::Assets::Actualize<RenderCore::Techniques::DeferredShaderResource>(String_IconBegin + categories[c] + String_IconEnd);
                        context.RequireCommandList(texture->GetCompletionCommandList());
                        context.DrawTexturedQuad(
                            ProjectionMode::P2D, 
                            AsPixelCoords(rect._topLeft),
                            AsPixelCoords(rect._bottomRight),
                            texture->GetShaderResource());

                    }

                    interactables.Register({rect, Id_FrameRigDisplaySubMenu+c});
                    pt = rect._topLeft - Coord2(margin, 0);
                    menuHeight = std::max(menuHeight, unsigned(rect._bottomRight[1] - rect._topLeft[1]));
                }

                    //  List all of the screens that are part of this category. They become hot spots
                    //  to activate that screen

                Layout screenListLayout(Rect(Coord2(0, pt[1] + menuHeight + margin), outerRect._bottomRight));

                const Coord2 smallIconSize(93/4, 88/4);
                auto lineHeight = std::max(smallIconSize[1], tabHeadingLineHeight);
                const auto screens = ds->GetWidgets();
                for (auto i=screens.cbegin(); i!=screens.cend(); ++i) {
                    if (i->_name.find(categories[_subMenuOpen-1]) != std::string::npos) {
                        unsigned width = (unsigned)StringWidth(*res->_tabHeadingFont, MakeStringSection(i->_name));
                        auto rect = screenListLayout.AllocateFullWidth(lineHeight);
                        rect._topLeft[0] = rect._bottomRight[0] - width;

                        FillRectangle(context, 
                            Rect(rect._topLeft - Coord2(2 + margin + smallIconSize[0],2), rect._bottomRight + Coord2(2,2)), 
                            interfaceState.HasMouseOver(i->_hashCode) ? menuBkgrndHigh : menuBkgrnd);

                        auto texture = ::Assets::Actualize<RenderCore::Techniques::DeferredShaderResource>(String_IconBegin + categories[_subMenuOpen-1] + String_IconEnd);
                        context.RequireCommandList(texture->GetCompletionCommandList());
                        context.DrawTexturedQuad(
                            ProjectionMode::P2D, 
                            AsPixelCoords(Coord2(rect._topLeft - Coord2(smallIconSize[0] + margin, 0))),
                            AsPixelCoords(Coord2(rect._topLeft[0]-margin, rect._bottomRight[1])),
                            texture->GetShaderResource());
                        DrawText()
                            .Color(tabHeaderColor)
                            .Alignment(TextAlignment::Left)
                            .Draw(context, rect, i->_name);

                        interactables.Register({rect, i->_hashCode});
                    }
                }
            }
        }
    }

    auto    FrameRigDisplay::ProcessInput(InterfaceState& interfaceState, const InputSnapshot& input) -> ProcessInputResult
    {
        auto topMost = interfaceState.TopMostWidget();
        if (input.IsPress_LButton() || input.IsRelease_LButton()) {
            if (topMost._id == Id_FrameRigDisplayMain) {
                if (input.IsRelease_LButton()) {
                    _subMenuOpen = unsigned(!_subMenuOpen);
                }
                return ProcessInputResult::Consumed;
            } else if (topMost._id >= Id_FrameRigDisplaySubMenu && topMost._id < Id_FrameRigDisplaySubMenu + 32) {
                if (input.IsRelease_LButton()) {
                    _subMenuOpen = unsigned(topMost._id - Id_FrameRigDisplaySubMenu + 1);
                }
                return ProcessInputResult::Consumed;
            }

            auto ds = _debugSystem.lock();
            const auto screens = ds->GetWidgets();
            if (ds) {
                if (std::find_if(screens.cbegin(), screens.cend(),
                    [&](const DebugScreensSystem::WidgetAndName& w) { return w._hashCode == topMost._id; }) != screens.cend()) {
                    if (input.IsRelease_LButton() && ds->SwitchToScreen(0, topMost._id)) {
                        _subMenuOpen = 0;
                    }
                    return ProcessInputResult::Consumed;
                }
            }
        }

        return ProcessInputResult::Passthrough;
    }

    FrameRigDisplay::FrameRigDisplay(
        std::shared_ptr<DebugScreensSystem> debugSystem,
        const AccumulatedAllocations::Snapshot& prevFrameAllocationCount, const FrameRateRecorder& frameRate)
    {
        _frameRate = &frameRate;
        _prevFrameAllocationCount = &prevFrameAllocationCount;
        _debugSystem = std::move(debugSystem);
        _subMenuOpen = 0;
    }

    FrameRigDisplay::~FrameRigDisplay()
    {}

    std::shared_ptr<IWidget> FrameRig::CreateDisplay(std::shared_ptr<DebugScreensSystem> debugSystem)
    {
        return std::make_shared<FrameRigDisplay>(std::move(debugSystem), _pimpl->_prevFrameAllocationCount, _pimpl->_frameRate);
    }

}

