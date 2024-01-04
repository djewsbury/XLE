// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "FrameRig.h"
#include "AllocationProfiler.h"
#include "OverlaySystem.h"
#include "PlatformApparatuses.h"
#include "TopBar.h"

#include "../RenderCore/IAnnotator.h"
#include "../RenderCore/IDevice.h"
#include "../RenderOverlays/DebuggingDisplay.h"
#include "../RenderOverlays/OverlayContext.h"
#include "../RenderOverlays/Font.h"
#include "../RenderOverlays/OverlayEffects.h"
#include "../RenderOverlays/ShapesRendering.h"
#include "../RenderOverlays/DrawText.h"
#include "../RenderOverlays/OverlayApparatus.h"

#include "../RenderCore/Techniques/ParsingContext.h"
#include "../RenderCore/Techniques/RenderPassUtils.h"
#include "../RenderCore/Techniques/RenderPass.h"
#include "../RenderCore/Techniques/CommonBindings.h"
#include "../RenderCore/Techniques/SubFrameEvents.h"
#include "../RenderCore/Techniques/Techniques.h"
#include "../RenderCore/Techniques/PipelineAccelerator.h"
#include "../RenderCore/Techniques/Services.h"
#include "../RenderCore/Techniques/Apparatuses.h"
#include "../RenderCore/Techniques/DeferredShaderResource.h"
#include "../RenderCore/Techniques/ImmediateDrawables.h"
#include "../RenderCore/BufferUploads/IBufferUploads.h"

#include "../Assets/Assets.h"
#include "../Assets/Continuation.h"

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
#include "../RenderCore/Vulkan/IDeviceVulkan.h"

// for display rendering (below)
#include "../RenderOverlays/LayoutEngine.h"
#include "../RenderOverlays/CommonWidgets.h"
#include "../Assets/IntermediatesStore.h"
#include "../Assets/AssetServices.h"
#include "../Assets/OperationContext.h"
#include "../Formatters/FormatterUtils.h"
#include "../Tools/EntityInterface/MountedData.h"
#include "DebuggingDisplays/InvalidAssetDisplay.h"

using namespace Assets::Literals;
using namespace PlatformRig::Literals;
using namespace Utility::Literals;

namespace PlatformRig
{
    using namespace RenderOverlays;
    using namespace RenderOverlays::DebuggingDisplay;
    class FrameRigDisplay;

    class FrameRateRecorder
    {
    public:
        void PushFrameInterval(uint64_t duration);
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
        float _timerToSeconds;
        unsigned _frameRenderCount;
        uint64_t _timerFrequency;
        uint64_t _lastFrameBarrierTimePoint = 0;

        RenderCore::Techniques::TechniqueContext _techniqueContext;
        std::shared_ptr<Utility::HierarchicalCPUProfiler> _frameCPUProfiler;

        RenderCore::Techniques::AttachmentReservation _capturedDoubleBufferAttachments;

        std::shared_ptr<FrameRigDisplay> _frameRigDisplay;

        CPUProfileEvent_Conditional _frameEvnt;
        RenderCore::IPresentationChain* _activePresentationChain = nullptr;

        Pimpl()
        : _timerFrequency(OSServices::GetPerformanceCounterFrequency())
        , _frameRenderCount(0)
        {
            _timerToSeconds = 1.0f / float(_timerFrequency);
        }
    };

    class FrameRigDisplay : public IFrameRigDisplay
    {
    public:
        void    Render(IOverlayContext& context, Layout& layout, Interactables&interactables, InterfaceState& interfaceState) override;
        ProcessInputResult    ProcessInput(InterfaceState& interfaceState, const OSServices::InputSnapshot& input) override;

        void SetErrorMsg(std::string msg) { _errorMsg = std::move(msg); }
        void SetStyle(Style style) override { _style = style; }
        void EnableMainStates(bool fpsDisplay) override { _showMainStats = fpsDisplay; }
        void SetLoadingContext(std::shared_ptr<Assets::OperationContext> opContext) override { _loadingContext = opContext; }

        FrameRigDisplay(
            std::shared_ptr<DebugScreensSystem> debugSystem,
            std::shared_ptr<Assets::OperationContext> loadingContext,
            const AccumulatedAllocations::Snapshot& prevFrameAllocationCount, const FrameRateRecorder& frameRate);
        ~FrameRigDisplay();
    protected:
        const AccumulatedAllocations::Snapshot* _prevFrameAllocationCount;
        const FrameRateRecorder* _frameRate;
        unsigned _subMenuOpen;
        std::string _errorMsg;
        Style _style = Style::Normal;
        bool _showMainStats = true;

        std::weak_ptr<DebugScreensSystem> _debugSystem;
        std::shared_ptr<Assets::OperationContext> _loadingContext;

        void RenderScreenSelector(IOverlayContext& context, Layout& layout, Interactables& interactables, InterfaceState& interfaceState, const Rect& displayRect);
    };

    static RenderCore::TextureViewDesc MakeDefaultPresentationChainView(RenderCore::Format);

///////////////////////////////////////////////////////////////////////////////

    RenderCore::Techniques::ParsingContext FrameRig::StartupFrame(
        std::shared_ptr<RenderCore::IThreadContext> context,
        std::shared_ptr<RenderCore::IPresentationChain> presChain)
    {
        using namespace RenderCore;
        auto* cpuProfiler = _pimpl->_frameCPUProfiler.get();
        _pimpl->_frameEvnt = CPUProfileEvent_Conditional{"FrameRig::ExecuteFrame", cpuProfiler};
        RenderCore::Techniques::ParsingContext parserContext{_pimpl->_techniqueContext, *context};
        assert(&parserContext.GetThreadContext() == context.get());

        if (!_pimpl->_lastFrameBarrierTimePoint) _pimpl->_lastFrameBarrierTimePoint = OSServices::GetPerformanceCounter();

        #if defined(_DEBUG)
            const bool intermittentlyCommitAssets = true;
            if (intermittentlyCommitAssets) {
                using namespace std::chrono_literals;
                static auto startTime = std::chrono::steady_clock::now();
                if ((std::chrono::steady_clock::now() - startTime) > 20s) {
                    ::Assets::Services::GetIntermediatesStore().FlushToDisk();
                    startTime = std::chrono::steady_clock::now();
                }
            }
        #endif

        Techniques::SetThreadContext(context);

        if (cpuProfiler)
            if (auto* threadContextVulkan = query_interface_cast<IThreadContextVulkan*>(context.get()))
                threadContextVulkan->AttachCPUProfiler(cpuProfiler);

        _pimpl->_activePresentationChain = presChain.get();

        if (auto* threadContextVulkan = query_interface_cast<IThreadContextVulkan*>(context.get()))
            threadContextVulkan->BeginFrameRenderingCommandList();

        context->GetAnnotator().Frame_Begin(_pimpl->_frameRenderCount);		// (on Vulkan, we must do this after IThreadContext::BeginFrameRenderingCommandList(), because that primes the command list in the vulkan device)

            //  We must invalidate the cached state at least once per frame.
            //  It appears that the driver might forget bound constant buffers
            //  during the begin frame or present
        context->InvalidateCachedState();

        if (_subFrameEvents)
            _subFrameEvents->_onBeginFrame.Invoke(parserContext);
        
        if (_pimpl->_techniqueContext._pipelineAccelerators) {
            auto newVisibility = _pimpl->_techniqueContext._pipelineAccelerators->VisibilityBarrier();
            parserContext.SetPipelineAcceleratorsVisibility(newVisibility);
        }

        // Bind the presentation target as the default output for the parser context
        auto presentationChainDesc = presChain->GetDesc();
        parserContext.BindAttachment(Techniques::AttachmentSemantics::ColorLDR, presChain, BindFlag::PresentationSrc, MakeDefaultPresentationChainView(presentationChainDesc._format));
        parserContext.GetAttachmentReservation().Absorb(std::move(_pimpl->_capturedDoubleBufferAttachments));

        auto& stitchingContext = parserContext.GetFragmentStitchingContext();
        parserContext.GetFrameBufferProperties() = FrameBufferProperties { presentationChainDesc._width, presentationChainDesc._height };
        parserContext.GetViewport() = ViewportDesc { 0.f, 0.f, (float)presentationChainDesc._width, (float)presentationChainDesc._height };

        return parserContext;
    }

    auto FrameRig::ShutdownFrame(
        RenderCore::Techniques::ParsingContext& parserContext) -> FrameResult
    {
        using namespace RenderCore;
        auto* cpuProfiler = _pimpl->_frameCPUProfiler.get();
        auto& context = parserContext.GetThreadContext();

        if (_pimpl->_frameRigDisplay)
            _pimpl->_frameRigDisplay->SetErrorMsg(parserContext._stringHelpers->_errorString);

        auto presentationTarget = Techniques::GetAttachmentResource(parserContext, Techniques::AttachmentSemantics::ColorLDR);
        bool endAnnotatorFrame = true;

        TRY {

            if (_subFrameEvents)
                _subFrameEvents->_onPrePresent.Invoke(context);

            Techniques::Services::GetBufferUploads().StallAndMarkCommandListDependency(context, parserContext._requiredBufferUploadsCommandList);

            {
                Metal::BarrierHelper barrierHelper(context);
                barrierHelper.Add(*presentationTarget, BindFlag::RenderTarget, BindFlag::PresentationSrc);
            }

            endAnnotatorFrame = false;
            context.GetAnnotator().Frame_End();        // calling Frame_End() can prevent creating a new command list immediately after the Present() call (which ends the previous command list)

			{
				CPUProfileEvent_Conditional pEvnt2("Present", cpuProfiler);
				context.Present(*_pimpl->_activePresentationChain);
			}

            if (_subFrameEvents)
                _subFrameEvents->_onPostPresent.Invoke(context);

            _pimpl->_capturedDoubleBufferAttachments = parserContext.GetAttachmentReservation().CaptureDoubleBufferAttachments();

            if (_subFrameEvents)
                _subFrameEvents->_onFrameBarrier.Invoke();

            Techniques::SetThreadContext(nullptr);

        } CATCH(const std::exception& e) {
			Log(Error) << "Suppressed error in frame rig render: " << e.what() << std::endl;
		    if (endAnnotatorFrame)
                context.GetAnnotator().Frame_End();
            Techniques::SetThreadContext(nullptr);
	    } CATCH_END
	
        uint64_t frameBarrierTimePoint = OSServices::GetPerformanceCounter();
        auto frameBarrierTime = frameBarrierTimePoint-_pimpl->_lastFrameBarrierTimePoint;
        _pimpl->_frameRate.PushFrameInterval(frameBarrierTime);
        _pimpl->_lastFrameBarrierTimePoint = frameBarrierTimePoint;

        ++_pimpl->_frameRenderCount;
        auto accAlloc = AccumulatedAllocations::GetInstance();
        if (accAlloc)
            _pimpl->_prevFrameAllocationCount = accAlloc->GetAndClear();

        _pimpl->_frameEvnt = {};
        if (cpuProfiler) {
            if (auto* threadContextVulkan = query_interface_cast<IThreadContextVulkan*>(&context))
                threadContextVulkan->AttachCPUProfiler(nullptr);
            cpuProfiler->FrameBarrier();
        }

        return { frameBarrierTime / float(_pimpl->_timerFrequency), parserContext.HasPendingAssets() };
    }

    RenderCore::Techniques::ParsingContext FrameRig::StartupFrame(
        WindowApparatus& windowApparatus)
    {
        return StartupFrame(windowApparatus._immediateContext, windowApparatus._presentationChain);
    }

    void FrameRig::IntermedialSleep(
        RenderCore::IThreadContext& threadContext,
        bool inBackground,
        const FrameResult& lastFrameResult)
    {
        if (lastFrameResult._hasPendingResources) {
            ::Threading::Sleep(16);  // slow down while we're building pending resources
        } else if (inBackground) {
            ::Threading::Sleep(16); // yield some process time
        } else {
            float threadingPressure = 0.f;
            if (auto* threadContextVulkan = query_interface_cast<RenderCore::IThreadContextVulkan*>(&threadContext))
                threadingPressure = threadContextVulkan->GetThreadingPressure();

            if (threadingPressure > 0.f) {
                // Start dropping frames if we have high threading pressure
                // This happens when there is some expensive background thread generating long cmd lists (or just not submitting frequently)
                ::Threading::Sleep(uint32_t(16 * std::min(60.f, threadingPressure)));
            }
        }
    }

    void FrameRig::IntermedialSleep(
        WindowApparatus& windowApparatus,
        bool inBackground,
        const FrameResult& lastFrameResult)
    {
        IntermedialSleep(*windowApparatus._immediateContext, inBackground, lastFrameResult);
    }

    void FrameRig::UpdatePresentationChain(RenderCore::IPresentationChain& presChain)
    {
        auto desc = presChain.GetDesc();
        auto& device = *presChain.GetDevice();

        using namespace RenderCore;
        // update system attachment formats
        _pimpl->_techniqueContext._systemAttachmentFormats = Techniques::CalculateDefaultSystemFormats(device);
        _pimpl->_techniqueContext._systemAttachmentFormats[(unsigned)Techniques::SystemAttachmentFormat::TargetColor] = desc._format;
    }

    auto FrameRig::GetOverlayConfiguration(RenderCore::IPresentationChain& presChain) const -> OverlayConfiguration
    {
        auto desc = presChain.GetDesc();

        using namespace RenderCore;
        // Should match ParsingContext::BindAttachment (for IPresentationChain)
        auto targetDesc = CreateDesc(
            desc._bindFlags, 
            AllocationRules::ResizeableRenderTarget,
            TextureDesc::Plain2D(desc._width, desc._height, desc._format, 1, 0, desc._samples));

        OverlayConfiguration result;
        result._fbProps = FrameBufferProperties { desc._width, desc._height, desc._samples };

        result._preregAttachments.push_back(
            Techniques::PreregisteredAttachment {
                Techniques::AttachmentSemantics::ColorLDR,
                targetDesc,
                "color-ldr",
                Techniques::PreregisteredAttachment::State::Uninitialized,
                BindFlag::PresentationSrc,
                MakeDefaultPresentationChainView(desc._format)
            });

        result._systemAttachmentFormats = _pimpl->_techniqueContext._systemAttachmentFormats;
        result._hash = RenderCore::Techniques::HashPreregisteredAttachments(MakeIteratorRange(result._preregAttachments), result._fbProps);
        return result;
    }

    std::vector<RenderCore::Techniques::PreregisteredAttachment> InitializeColorLDR(
        IteratorRange<const RenderCore::Techniques::PreregisteredAttachment*> input)
    {
        std::vector<RenderCore::Techniques::PreregisteredAttachment> result = {input.begin(), input.end()};
        auto i = std::find_if(result.begin(), result.end(), [](const auto& q) { return q._semantic == RenderCore::Techniques::AttachmentSemantics::ColorLDR; });
        if (i != result.end())
            i->_state = RenderCore::Techniques::PreregisteredAttachment::State::Initialized;
        return result;
    }

    void FrameRig::ReleaseDoubleBufferAttachments()
    {
        // we may need to clear all of the captured attachments sometimes (for example, before a swap chain resolution change)
        _pimpl->_capturedDoubleBufferAttachments = {};
    }

    float FrameRig::GetSmoothedDeltaTime()
    {
        return std::get<0>(_pimpl->_frameRate.GetPerformanceStats());
    }

    RenderCore::Techniques::TechniqueContext& FrameRig::GetTechniqueContext()
    {
        return _pimpl->_techniqueContext;
    }

    FrameRig::FrameRig(
        RenderCore::Techniques::FrameRenderingApparatus& frameRenderingApparatus,
        RenderCore::Techniques::DrawingApparatus* drawingApparatus)
    : _subFrameEvents(frameRenderingApparatus.GetSubFrameEvents())
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

        auto& techniqueContext = _pimpl->_techniqueContext;
        if (drawingApparatus) {
            InitializeTechniqueContext(techniqueContext, *drawingApparatus);
        }
        techniqueContext._attachmentPool = frameRenderingApparatus._attachmentPool;
        techniqueContext._frameBufferPool = frameRenderingApparatus._frameBufferPool;

        _pimpl->_frameCPUProfiler = frameRenderingApparatus._frameCPUProfiler;
    }

    FrameRig::~FrameRig() 
    {
    }

    static RenderCore::TextureViewDesc MakeDefaultPresentationChainView(RenderCore::Format fmt)
    {
        // Given a desc for a presentation chain target format, calculate the default TextureViewDesc
        // for formats with less than 16 bits per pixel, we will treat the target as having non-linear color
        RenderCore::TextureViewDesc result;
        if (RenderCore::GetComponentPrecision(fmt) < 16)
            result._format._aspect = RenderCore::TextureViewDesc::Aspect::ColorSRGB;
        return result;
    }

///////////////////////////////////////////////////////////////////////////////

    void FrameRateRecorder::PushFrameInterval(uint64_t duration)
    {
        if (_bufferEnd == _bufferStart)
            _bufferStart = (_bufferStart+1)%dimof(_durationHistory);

        _durationHistory[_bufferEnd] = duration;
        _bufferEnd = (_bufferEnd+1)%dimof(_durationHistory);
    }

    std::tuple<float, float, float> FrameRateRecorder::GetPerformanceStats() const
    {
        unsigned    entryCount = 0;
        uint64_t      accumulation = 0;
        uint64_t      minTime = std::numeric_limits<uint64_t>::max(), maxTime = 0;
        // we're never empty, so if _bufferStart == _bufferEnd, we're full up
        unsigned c=_bufferStart;
        for (;;) {
            accumulation += _durationHistory[c];
            minTime = std::min(minTime, _durationHistory[c]);
            maxTime = std::max(maxTime, _durationHistory[c]);
            ++entryCount;

            c=(c+1)%dimof(_durationHistory);
            if (c==_bufferEnd) break;
        }

        assert(entryCount);

        double averageDuration = double(accumulation) / double(_frequency) / double(entryCount);
        double minDuration = minTime / double(_frequency);
        double maxDuration = maxTime / double(_frequency);
        return std::make_tuple(float(averageDuration), float(minDuration), float(maxDuration));
    }

    FrameRateRecorder::FrameRateRecorder()
    {
        _bufferStart = _bufferEnd = 0;      // (we start full)
        _frequency = OSServices::GetPerformanceCounterFrequency();
        // For the first few frames, we want to return a reasonable defaults -- so let's fill up with a fixed value
        for (auto& d:_durationHistory) d = _frequency / 60;
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

    class FrameRigResources
    {
    public:
        std::shared_ptr<RenderOverlays::Font> _frameRateFont;
        std::shared_ptr<RenderOverlays::Font> _smallFrameRateFont;
        std::shared_ptr<RenderOverlays::Font> _tabHeadingFont;
        std::shared_ptr<RenderOverlays::Font> _errorReportingFont;

        FrameRigResources(
            std::shared_ptr<RenderOverlays::Font> frameRateFont,
            std::shared_ptr<RenderOverlays::Font> smallFrameRateFont,
            std::shared_ptr<RenderOverlays::Font> tabHeadingFont,
            std::shared_ptr<RenderOverlays::Font> errorReportingFont)
        : _frameRateFont(std::move(frameRateFont)), _smallFrameRateFont(std::move(smallFrameRateFont)), _tabHeadingFont(std::move(tabHeadingFont)), _errorReportingFont(std::move(errorReportingFont))
        {}

        static void ConstructToPromise(std::promise<std::shared_ptr<FrameRigResources>>&& promise)
        {
            ::Assets::WhenAll(
                RenderOverlays::MakeFont("OrbitronBlack", 32), // RenderOverlays::MakeFont("Shojumaru", 32),
                RenderOverlays::MakeFont("Metropolitano", 12),
                RenderOverlays::MakeFont("Raleway", 20),
                RenderOverlays::MakeFont("Anka", 20)).ThenConstructToPromise(std::move(promise));
        }
    };

    static void DrawMainStats(
        IOverlayContext& context,
        ImmediateLayout& layout,
        FrameRigResources& res,
        const std::tuple<float, float, float>& fpsStats,
        const AccumulatedAllocations::CurrentHeapMetrics& heapMetrics,
        unsigned frameAllocationCount)
    {
        const Coord rectWidth = layout.GetMaximumSize().Width();
        const unsigned fpsAreaWidth = 80;

        char buffer[64];

        DrawText()
            .Font(*res._frameRateFont)
            .Alignment(TextAlignment::Left)
            .Draw(context, layout.AllocateFullHeight(fpsAreaWidth), StringMeldInPlace(buffer) << std::setprecision(1) << std::fixed << 1.f / std::get<0>(fpsStats));

        /*
        auto remaining = layout.AllocateFullHeightFraction(1.f);
        auto middle = (remaining._topLeft[1] + remaining._bottomRight[1])/2;

        DrawText()
            .Font(*res._smallFrameRateFont)
            .Alignment(TextAlignment::BottomLeft)
            .Draw(context, {remaining._topLeft, {remaining._bottomRight[0], middle-2}}, StringMeldInPlace(buffer) << std::setprecision(1) << std::fixed << (1.f / std::get<2>(fpsStats)) << "-" << (1.f / std::get<1>(fpsStats)));

        DrawText()
            .Font(*res._smallFrameRateFont)
            .Alignment(TextAlignment::TopLeft)
            .Draw(context, {{remaining._topLeft[0], middle+2}, remaining._bottomRight}, StringMeldInPlace(buffer) << std::setprecision(2) << std::fixed << heapMetrics._usage / (1024.f*1024.f) << "M (" << frameAllocationCount << ")");
        */
    }

    void FrameRigDisplay::RenderScreenSelector(IOverlayContext& context, Layout& layout, Interactables& interactables, InterfaceState& interfaceState, const Rect& displayRect)
    {
        auto* res = ConsoleRig::TryActualizeCachedBox<FrameRigResources>();
        if (!res) return;

        const Coord margin = 8;

        const ColorB normalColor = ColorB(70, 31, 0, 0x9f);
        const ColorB mouseOverColor = ColorB(70, 31, 0, 0xff);
        const ColorB pressed = ColorB(128, 50, 0, 0xff);
        const ColorB menuBkgrnd(128, 96, 64, 64);
        const ColorB menuBkgrndHigh(128, 96, 64, 192);
        const ColorB tabHeaderColor(0xffffffff);

        const auto tabHeadingLineHeight = Coord(res->_tabHeadingFont->GetFontProperties()._lineHeight);

        auto outerRect = layout.GetMaximumSize();

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

                        auto texture = ::Assets::ActualizeAssetPtr<RenderCore::Techniques::DeferredShaderResource>(String_IconBegin + categories[c] + String_IconEnd);
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
                        auto texture = ::Assets::ActualizeAssetPtr<RenderCore::Techniques::DeferredShaderResource>(String_IconBegin + categories[c] + String_IconEnd);
                        context.RequireCommandList(texture->GetCompletionCommandList());
                        context.DrawTexturedQuad(
                            ProjectionMode::P2D, 
                            AsPixelCoords(rect._topLeft),
                            AsPixelCoords(rect._bottomRight),
                            texture->GetShaderResource());

                    }

                    interactables.Register(rect, Id_FrameRigDisplaySubMenu+c);
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

                        auto texture = ::Assets::ActualizeAssetPtr<RenderCore::Techniques::DeferredShaderResource>(String_IconBegin + categories[_subMenuOpen-1] + String_IconEnd);
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

                        interactables.Register(rect, i->_hashCode);
                    }
                }
            }
        }
    }

    struct FrameRigDisplayStaticData
    {
        unsigned _verticalOffset = 100;

        FrameRigDisplayStaticData() = default;

        template<typename Formatter>
            FrameRigDisplayStaticData(Formatter& fmttr)
        {
            uint64_t keyname;
            while (TryKeyedItem(fmttr, keyname)) {
                switch (keyname) {
                case "VerticalOffset"_h: _verticalOffset = Formatters::RequireCastValue<decltype(_verticalOffset)>(fmttr); break;
                default: SkipValueOrElement(fmttr); break;
                }
            }
        }
    };

    void    FrameRigDisplay::Render(IOverlayContext& context, Layout& layout,
                                    Interactables& interactables, InterfaceState& interfaceState)
    {
        auto* res = ConsoleRig::TryActualizeCachedBox<FrameRigResources>();
        if (!res) return;

        using namespace RenderOverlays;
        auto outerRect = layout.GetMaximumSize();

        if (_showMainStats) {
            const Coord rectWidth = 200;
            const Coord padding = 12;
            const Coord margin = 8;
            const auto bigLineHeight = Coord(res->_frameRateFont->GetFontProperties()._lineHeight);
            const auto smallLineHeight = Coord(res->_smallFrameRateFont->GetFontProperties()._lineHeight);
            const Coord rectHeight = bigLineHeight + 3 * margin + smallLineHeight;
            Rect displayRect(
                Coord2(outerRect._bottomRight[0] - rectWidth - padding, outerRect._topLeft[1] + padding),
                Coord2(outerRect._bottomRight[0] - padding, outerRect._topLeft[1] + padding + rectHeight));
            if (auto* topBar = context.GetService<ITopBarManager>())
                displayRect = topBar->FrameRigDisplay(context);
            Layout innerLayout(displayRect);
            innerLayout._paddingInternalBorder = margin;
            innerLayout._paddingBetweenAllocations = margin;

            auto f = _frameRate->GetPerformanceStats();
            auto heapMetrics = AccumulatedAllocations::GetCurrentHeapMetrics();
            auto frameAllocations = _prevFrameAllocationCount->_allocationCount;

            DrawMainStats(context, innerLayout, *res, f, heapMetrics, frameAllocations);

            interactables.Register(displayRect, Id_FrameRigDisplayMain);
        }

        if (_style == Style::Normal) {
            auto ds = _debugSystem.lock();
            if (ds && !ds->CurrentScreen(0)) {
                auto* staticData = &EntityInterface::MountedData<FrameRigDisplayStaticData>::LoadOrDefault("cfg/displays/framerig"_initializer);

                Layout outerKeyHelpRect = outerRect;
                outerKeyHelpRect._maximumSize._topLeft[1] += staticData->_verticalOffset;

                auto& styler = CommonWidgets::Styler::Get();
                DrawContext drawContext{context, interactables, interfaceState};

                std::vector<const char*> keys, keyLabels;
                keys.push_back("H"); keyLabels.push_back("Help");

                if (_loadingContext && !_loadingContext->IsIdle()) {
                    keys.push_back("C");
                    keyLabels.push_back("Compile Progress");
                }

                for (unsigned c=0; c<keys.size(); ++c) {
                    auto measure0 = styler.MeasureKeyIndicator(keyLabels[c], keys[c]);

                    auto frame = outerKeyHelpRect.AllocateFullWidth(measure0._height);
                    if (frame.Width() < measure0._minWidth) continue;
                    frame._topLeft[0] = frame._bottomRight[0] - std::min(frame.Width(), measure0._width);

                    auto d = styler.MeasureKeyIndicator_Precalculate(frame.Width(), frame.Height(), keyLabels[c], keys[c]);
                    styler.KeyIndicator(drawContext, frame, d.get());
                }
            }
        } else {
            assert(_style == Style::NonInteractive);

            // We should always display active compiles. Since we're in non-interactive mode, we have to show the screen directly, rather than 
            // just showing a key icon
            if (_loadingContext && !_loadingContext->IsIdle()) {
                Overlays::OperationContextDisplay op { _loadingContext };
                op.Render(context, layout, interactables, interfaceState);
            }
        }

        if (!_errorMsg.empty()) {
            DrawText{}
                .Alignment(RenderOverlays::TextAlignment::Center)
                .Color(0xffffbfbf)
                .Font(*res->_errorReportingFont)
                .Draw(context, {outerRect._topLeft, outerRect._bottomRight}, _errorMsg);
        }
    }

    auto    FrameRigDisplay::ProcessInput(InterfaceState& interfaceState, const InputSnapshot& input) -> ProcessInputResult
    {
        if (_style == Style::NonInteractive)
            return ProcessInputResult::Passthrough;

        auto topMost = interfaceState.TopMostHotArea();
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
            const auto& screens = ds->GetWidgets();
            if (ds) {
                if (std::find_if(screens.cbegin(), screens.cend(),
                    [&](const DebugScreensSystem::WidgetAndName& w) { return w._hashCode == topMost._id; }) != screens.cend()) {
                    if (input.IsRelease_LButton() && ds->SwitchToScreen(0, topMost._id)) {
                        _subMenuOpen = 0;
                    }
                    return ProcessInputResult::Consumed;
                }
            }
        } else if (input.IsPress("h"_key)) {
            if (auto ds = _debugSystem.lock())
                if (!ds->CurrentScreen(0)) {
                    ds->SwitchToScreen(0, "[Console] Key Binding Help");
                    return ProcessInputResult::Consumed;
                }
        } else if (input.IsPress("c"_key) && _loadingContext && !_loadingContext->IsIdle()) {
            if (auto ds = _debugSystem.lock())
                if (!ds->CurrentScreen(0)) {
                    ds->SwitchToScreen(0, "Loading Context");
                    return ProcessInputResult::Consumed;
                }
        }

        return ProcessInputResult::Passthrough;
    }

    FrameRigDisplay::FrameRigDisplay(
        std::shared_ptr<DebugScreensSystem> debugSystem,
        std::shared_ptr<Assets::OperationContext> loadingContext,
        const AccumulatedAllocations::Snapshot& prevFrameAllocationCount, const FrameRateRecorder& frameRate)
    {
        _frameRate = &frameRate;
        _prevFrameAllocationCount = &prevFrameAllocationCount;
        _debugSystem = std::move(debugSystem);
        _loadingContext = std::move(loadingContext);
        _subMenuOpen = 0;
    }

    FrameRigDisplay::~FrameRigDisplay()
    {}

    std::shared_ptr<IFrameRigDisplay> FrameRig::CreateDisplay(std::shared_ptr<DebugScreensSystem> debugSystem, std::shared_ptr<Assets::OperationContext> loadingContext)
    {
        if (!_pimpl->_frameRigDisplay)
            _pimpl->_frameRigDisplay = std::make_shared<FrameRigDisplay>(std::move(debugSystem), std::move(loadingContext), _pimpl->_prevFrameAllocationCount, _pimpl->_frameRate);
        return _pimpl->_frameRigDisplay;
    }

}

