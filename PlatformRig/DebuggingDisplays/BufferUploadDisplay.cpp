// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "BufferUploadDisplay.h"
#include "../../BufferUploads/Metrics.h"
#include "../../BufferUploads/BatchedResources.h"
#include "../../RenderOverlays/CommonWidgets.h"
#include "../../RenderOverlays/Font.h"
#include "../../ConsoleRig/ResourceBox.h"
#include "../../Assets/Continuation.h"
#include "../../OSServices/TimeUtils.h"
#include "../../Utility/MemoryUtils.h"
#include "../../Utility/PtrUtils.h"
#include "../../Utility/StringFormat.h"
#include "../../Utility/StringUtils.h"
#include "../../Utility/StreamUtils.h"
#include "../../Utility/IteratorUtils.h"
#include "../../Utility/BitUtils.h"
#include <assert.h>

#pragma warning(disable:4127)       // warning C4127: conditional expression is constant

namespace PlatformRig { namespace Overlays
{
    static void DrawButton(IOverlayContext& context, const char name[], const Rect&buttonRect, Interactables&interactables, InterfaceState& interfaceState)
    {
        InteractableId id = InteractableId_Make(name);
        RenderOverlays::CommonWidgets::Draw{context, interactables, interfaceState}.ButtonBasic(buttonRect, id, name);
        interactables.Register({buttonRect, id});
    }

    BufferUploadDisplay::GPUMetrics::GPUMetrics()
    {
        _slidingAverageCostMS = 0.f;
        _slidingAverageBytesPerSecond = 0;
    }

    BufferUploadDisplay::FrameRecord::FrameRecord()
    {
        _frameId = 0x0;
        _gpuCost = 0.f;
        _commandListStart = _commandListEnd = ~unsigned(0x0);
    }

    BufferUploadDisplay::BufferUploadDisplay(BufferUploads::IManager* manager)
    : _manager(manager)
    {
        XlZeroMemory(_accumulatedCreateCount);
        XlZeroMemory(_accumulatedCreateBytes);
        XlZeroMemory(_accumulatedUploadCount);
        XlZeroMemory(_accumulatedUploadBytes);
        _graphsMode = 0;
        _mostRecentGPUFrequency = 0;
        _lastUploadBeginTime = 0;
        _mostRecentGPUCost = 0.f;
        _mostRecentGPUFrameId = 0;
        _lockedFrameId = ~unsigned(0x0);

        auto timerFrequency = OSServices::GetPerformanceCounterFrequency();
        _reciprocalTimerFrequency = 1./double(timerFrequency);

        assert(s_gpuListenerDisplay==0);
        s_gpuListenerDisplay = this;
    }

    BufferUploadDisplay::~BufferUploadDisplay()
    {
        assert(s_gpuListenerDisplay==this);
        s_gpuListenerDisplay = NULL;
    }

    static const char* AsString(BufferUploads::UploadDataType value)
    {
        switch (value) {
        case BufferUploads::UploadDataType::Texture:            return "Texture";
        case BufferUploads::UploadDataType::GeometryBuffer:     return "Geo";
        case BufferUploads::UploadDataType::UniformBuffer:      return "Uniforms";
        default: return "<<unknown>>";
        }
    };

    static const char* TypeString(const RenderCore::ResourceDesc& desc)
    {
        using namespace BufferUploads;
        if (desc._type == RenderCore::ResourceDesc::Type::Texture) {
            const TextureDesc& tDesc = desc._textureDesc;
            switch (tDesc._dimensionality) {
            case TextureDesc::Dimensionality::T1D: return "Tex1D";
            case TextureDesc::Dimensionality::T2D: return "Tex2D";
            case TextureDesc::Dimensionality::T3D: return "Tex3D";
            case TextureDesc::Dimensionality::CubeMap: return "TexCube";
            default: break;
            }
        } else if (desc._type == RenderCore::ResourceDesc::Type::LinearBuffer) {
            if (desc._bindFlags & BindFlag::VertexBuffer) return "VB";
            else if (desc._bindFlags & BindFlag::IndexBuffer) return "IB";
            else if (desc._bindFlags & BindFlag::ConstantBuffer) return "CB";
            else if (desc._bindFlags & BindFlag::UnorderedAccess) return "UOB";
        }
        return "Unknown";
    }

    static std::string BuildDescription(const RenderCore::ResourceDesc& desc)
    {
        using namespace BufferUploads;
        char buffer[2048];
        if (desc._type == RenderCore::ResourceDesc::Type::Texture) {
            const TextureDesc& tDesc = desc._textureDesc;
            xl_snprintf(buffer, dimof(buffer), "(%4ix%4i) mips:(%i), array:(%i)", 
                tDesc._width, tDesc._height, tDesc._mipCount, tDesc._arrayCount);
        } else if (desc._type == RenderCore::ResourceDesc::Type::LinearBuffer) {
            xl_snprintf(buffer, dimof(buffer), "%6.2fkb", 
                desc._linearBufferDesc._sizeInBytes/1024.f);
        } else {
            buffer[0] = '\0';
        }
        return std::string(buffer);
    }

    static unsigned& GetFrameID() 
    { 
        static unsigned s_frameId = 0;
        return s_frameId; 
    }

    namespace GraphTabs
    {
        enum Enum
        {
            Uploads,
            CreatesMB, CreatesCount, DeviceCreatesCount, StagingBufferAllocated,
            FramePriorityStall,     // FillValuesBuffer requires this and the above be in this order
            Latency, PendingBuffers, CommandListCount,
            GPUCost, GPUBytesPerSecond, AveGPUCost,
            ThreadActivity, BatchedCopy,
            Statistics, RecentRetirements,
            StagingMaxNextBlock, StagingAwaitingDevice
        };
        static const char* Names[] = {
            "Uploads (MB)", "Creates (MB)", "Creates (count)", "Device creates (count)", "Stage Buffer Allocated (MB)", "Frame Priority Stalls", "Latency (s)", "Pending Buffers (MB)", "Command List Count", "GPU Cost", "GPU bytes/second", "Ave GPU cost", "Thread Activity %", "Batched copy", "Statistics", "Recent Retirements", "Stage Max Next Block (MB)", "Stage Awaiting Device (MB)"
        };

        std::pair<const char*, std::vector<Enum>> Groups[] = 
        {
            std::pair<const char*, std::vector<Enum>>("Uploads",    { Uploads, StagingBufferAllocated, StagingMaxNextBlock, StagingAwaitingDevice }),
            std::pair<const char*, std::vector<Enum>>("Creations",  { CreatesMB, CreatesCount, DeviceCreatesCount }),
            std::pair<const char*, std::vector<Enum>>("GPU",        { GPUCost, GPUBytesPerSecond, AveGPUCost }),
            std::pair<const char*, std::vector<Enum>>("Threading",  { Latency, PendingBuffers, CommandListCount, ThreadActivity, BatchedCopy, FramePriorityStall }),
            std::pair<const char*, std::vector<Enum>>("Extra",      { Statistics, RecentRetirements }),
        };
    }

    static const unsigned s_MaxGraphSegments = 256;

    class BUFontBox
    {
    public:
        std::shared_ptr<RenderOverlays::Font> _font;
		std::shared_ptr<RenderOverlays::Font> _smallFont;
        std::shared_ptr<RenderOverlays::Font> _graphBorderFont;

        BUFontBox(std::shared_ptr<RenderOverlays::Font> font, std::shared_ptr<RenderOverlays::Font> smallFont, std::shared_ptr<RenderOverlays::Font> graphBorderFont)
        : _font(std::move(font)), _smallFont(std::move(smallFont)), _graphBorderFont(std::move(graphBorderFont)) {}

        static void ConstructToPromise(std::promise<std::shared_ptr<BUFontBox>>&& promise)
        {
            ::Assets::WhenAll(
                RenderOverlays::MakeFont("OrbitronBlack", 18),
                RenderOverlays::MakeFont("Vera", 16),
                RenderOverlays::MakeFont("Petra", 16)).ThenConstructToPromise(std::move(promise));
        }
    };

    static void DrawTopLeftRight(IOverlayContext& context, const Rect& rect, const ColorB& col)
    {
        Float3 coords[] = {
            AsPixelCoords(rect._topLeft), AsPixelCoords(Coord2(rect._topLeft[0], rect._bottomRight[1])),
            AsPixelCoords(rect._topLeft), AsPixelCoords(Coord2(rect._bottomRight[0], rect._topLeft[1])),
            AsPixelCoords(Coord2(rect._bottomRight[0], rect._topLeft[1])), AsPixelCoords(rect._bottomRight)
        };
        ColorB cols[] = { col, col, col, col, col, col };
        context.DrawLines(ProjectionMode::P2D, coords, dimof(coords), cols);
    }

    static void DrawBottomLeftRight(IOverlayContext& context, const Rect& rect, const ColorB& col)
    {
        Float3 coords[] = {
            AsPixelCoords(rect._topLeft), AsPixelCoords(Coord2(rect._topLeft[0], rect._bottomRight[1])),
            AsPixelCoords(Coord2(rect._topLeft[0], rect._bottomRight[1])), AsPixelCoords(rect._bottomRight),
            AsPixelCoords(Coord2(rect._bottomRight[0], rect._topLeft[1])), AsPixelCoords(rect._bottomRight)
        };
        ColorB cols[] = { col, col, col, col, col, col };
        context.DrawLines(ProjectionMode::P2D, coords, dimof(coords), cols);
    }

    void    BufferUploadDisplay::DrawMenuBar(IOverlayContext& context, Layout& layout, Interactables& interactables, InterfaceState& interfaceState)
    {
        static const ColorB edge(60, 60, 60, 0xcf);
        static const ColorB middle(32, 32, 32, 0xcf);
        static const ColorB mouseOver(20, 20, 20, 0xff);
        static const ColorB text(220, 220, 220, 0xff);
        static const ColorB smallText(170, 170, 170, 0xff);
        auto fullSize = layout.GetMaximumSize();

        auto* fonts = ConsoleRig::TryActualizeCachedBox<BUFontBox>();
        if (!fonts) return;

            // bkgrnd
        FillRectangle(
            context, 
            Rect(fullSize._topLeft, Coord2(fullSize._bottomRight[0], fullSize._topLeft[1]+layout._paddingInternalBorder)),
            edge);
        FillRectangle(
            context, 
            Rect(Coord2(fullSize._topLeft[0], fullSize._bottomRight[1]-layout._paddingInternalBorder), fullSize._bottomRight),
            edge);
        FillRectangle(
            context, 
            Rect(Coord2(fullSize._topLeft[0], fullSize._topLeft[1]+layout._paddingInternalBorder), Coord2(fullSize._bottomRight[0], fullSize._bottomRight[1]-layout._paddingInternalBorder)),
            middle);

        layout.AllocateFullHeight(75);

        const std::vector<GraphTabs::Enum>* dropDown = nullptr;
        Rect dropDownRect;
        static const unsigned dropDownInternalBorder = 10;

        for (const auto& g:GraphTabs::Groups) {
            auto rect = layout.AllocateFullHeight(150);

            auto hash = Hash64(g.first);
            if (interfaceState.HasMouseOver(hash)) {
                FillRectangle(context, rect, mouseOver);
                DrawTopLeftRight(context, rect, ColorB::White);
                dropDown = &g.second;
                
                unsigned count = (unsigned)g.second.size();
                Coord2 dropDownSize(
                    300,
                    count * 20 + (count-1) * layout._paddingBetweenAllocations + 2 * dropDownInternalBorder);
                dropDownRect._topLeft = Coord2(rect._topLeft[0], rect._bottomRight[1]);
                dropDownRect._bottomRight = dropDownRect._topLeft + dropDownSize;
                interactables.Register({dropDownRect, hash});
            }

            DrawText()
                .Font(*fonts->_font)
                .Flags(DrawTextFlags::Shadow)
                .Alignment(TextAlignment::Center)
                .Color(text)
                .Draw(context, rect, g.first);

            interactables.Register({rect, hash});
        }

        if (dropDown) {
            FillRectangle(context, dropDownRect, mouseOver);
            DrawBottomLeftRight(context, dropDownRect, ColorB::White);

            Layout dd(dropDownRect);
            dd._paddingInternalBorder = dropDownInternalBorder;
            for (unsigned c=0; c<dropDown->size(); ++c) {
                auto rect = dd.AllocateFullWidth(20);
                auto col = smallText;

                const auto* name = GraphTabs::Names[unsigned(dropDown->begin()[c])];
                auto hash = Hash64(name);
                if (interfaceState.HasMouseOver(hash))
                    col = ColorB::White;

                DrawText()
                    .Font(*fonts->_font)
                    .Flags(DrawTextFlags::Shadow)
                    .Alignment(TextAlignment::Left)
                    .Color(col)
                    .Draw(context, rect, name);

                if ((c+1) != dropDown->size())
                    context.DrawLine(ProjectionMode::P2D,
                        AsPixelCoords(Coord2(rect._topLeft[0], rect._bottomRight[1])), col,
                        AsPixelCoords(rect._bottomRight), col);

                interactables.Register({rect, hash});
            }
        }
    }

    static const ColorB     GraphLabel(255, 255, 255, 128);
    static const ColorB     GraphBorder(64, 128, 64, 196);
    static const ColorB     GraphText(64, 128, 64, 196);
    static const ColorB     GraphBkColor(16, 16, 16, 210);

    static Rect DrawUploadsGraph(
        IOverlayContext& context, const Rect& controlRect,
        RenderOverlays::DebuggingDisplay::GraphSeries<float> topSeries,
        RenderOverlays::DebuggingDisplay::GraphSeries<float> bottomSeries,
        StringSection<> topSeriesName, StringSection<> bottomSeriesName,
        unsigned horizontalAllocation)
    {
        auto* fonts = ConsoleRig::TryActualizeCachedBox<BUFontBox>();
        if (!fonts) return {Zero<Coord2>(), Zero<Coord2>()};

        assert(topSeries._minValue == bottomSeries._minValue);

        auto border = fonts->_graphBorderFont->GetFontProperties()._lineHeight;
        const auto innerChartSpacing = 10;
        const auto infoBoxWidth = 50;

        int chartTopY = controlRect._topLeft[1] + Coord(border) + innerChartSpacing;
        int chartBottomY = controlRect._bottomRight[1] - Coord(border) - innerChartSpacing;
        if (chartBottomY <= chartTopY) return {Zero<Coord2>(), Zero<Coord2>()};
        int chartMiddle = (chartTopY + chartBottomY) / 2;

        Rect topChartRect {
            Coord2{controlRect._topLeft[0] + Coord(border), chartTopY },
            Coord2{controlRect._bottomRight[0] - Coord(border) - infoBoxWidth - 5u, chartMiddle }
        };

        // Try to align the width of the chart so that each horizontal element gets an equal number of pixels
        topChartRect._bottomRight[0] = topChartRect._topLeft[0] + int(topChartRect.Width()/horizontalAllocation) * horizontalAllocation;

        Rect bottomChartRect {
            Coord2{controlRect._topLeft[0] + Coord(border), chartBottomY },
            Coord2{topChartRect._bottomRight[0], chartMiddle }
        };

        FillRectangle(context, controlRect, GraphBkColor);

        // draw the charts themselves
        DrawBarChartContents(context, topChartRect, topSeries, horizontalAllocation);
        DrawBarChartContents(context, bottomChartRect, bottomSeries, horizontalAllocation);

        // draw the info bar on the right, showing the chart dimensions
        Rect infoArea {
            Coord2{topChartRect._bottomRight[0] + 5u, controlRect._topLeft[1] + Coord(border) },
            Coord2{controlRect._bottomRight[0] - Coord(border), controlRect._bottomRight[1] - Coord(border) }
        };
        char buffer[256];
        DrawText().Font(*fonts->_smallFont).Alignment(TextAlignment::TopRight).Color(GraphLabel).Draw(
            context, infoArea, (StringMeldInPlace(buffer) << "(" << topSeriesName << ") " << topSeries._maxValue).AsStringSection());
        DrawText().Font(*fonts->_smallFont).Alignment(TextAlignment::Right).Color(GraphLabel).Draw(
            context, infoArea, (StringMeldInPlace(buffer) << topSeries._minValue).AsStringSection());
        DrawText().Font(*fonts->_smallFont).Alignment(TextAlignment::BottomRight).Color(GraphLabel).Draw(
            context, infoArea, (StringMeldInPlace(buffer) << "(" << bottomSeriesName << ") " << bottomSeries._maxValue).AsStringSection());

        // draw the outline
        OutlineRoundedRectangle(
            context,
            { controlRect._topLeft + Coord2(border/2, border/2), controlRect._bottomRight - Coord2(border/2, border/2) },
            GraphBorder,
            2.f, 1.f/32.f);
        
        // highlight max values
        {
            // top series
            unsigned valueLeft = (unsigned)std::max(int(topSeries._values.size() - horizontalAllocation), 0);
            int A = topSeries._peakIndex - valueLeft;
            if (A > 0 && A < horizontalAllocation) {
                Coord px = topChartRect._topLeft[0] + topChartRect.Width() * A / float(horizontalAllocation);
                Coord px2 = px + topChartRect.Width() / horizontalAllocation;
                px -= 3; px2 += 2;
                Coord py = LinearInterpolate(topChartRect._bottomRight[1], topChartRect._topLeft[1], (topSeries._values[topSeries._peakIndex] - topSeries._minValue) / float(topSeries._maxValue - topSeries._minValue));
                Float3 lines[] { 
                    AsPixelCoords(Coord2{px, py}), AsPixelCoords(Coord2{px2, py}),
                    AsPixelCoords(Coord2{(px+px2)/2, py}), AsPixelCoords(Coord2{(px+px2)/2, controlRect._topLeft[1] + border})
                };
                context.DrawLines(ProjectionMode::P2D, lines, dimof(lines), GraphBorder);

                auto section = (StringMeldInPlace(buffer) << topSeries._values[topSeries._peakIndex]).AsStringSection();
                auto width = StringWidth(*fonts->_smallFont, section);
                Rect bubble{Coord2{(px+px2-width)/2 - 3, controlRect._topLeft[1]}, Coord2{(px+px2+width)/2 + 3, controlRect._topLeft[1]+border + 3}};
                if (bubble._topLeft[0] < controlRect._topLeft[0]) { auto shift = controlRect._topLeft[0] - bubble._topLeft[0]; bubble._topLeft[0] += shift; bubble._bottomRight[0] += shift; }
                if (bubble._bottomRight[0] > controlRect._bottomRight[0]) { auto shift = bubble._bottomRight[0] - controlRect._bottomRight[0]; bubble._topLeft[0] -= shift; bubble._bottomRight[0] -= shift; }
                FillAndOutlineRoundedRectangle(context, bubble, RenderOverlays::ColorB::Black, GraphBorder);
                DrawText().Font(*fonts->_smallFont).Alignment(TextAlignment::Center).Color(GraphText).Draw(context, bubble, section);
            }
        }
        {
            // bottom series
            unsigned valueLeft = (unsigned)std::max(int(bottomSeries._values.size() - horizontalAllocation), 0);
            int A = bottomSeries._peakIndex - valueLeft;
            if (A > 0 && A < horizontalAllocation) {
                Coord px = bottomChartRect._topLeft[0] + bottomChartRect.Width() * A / float(horizontalAllocation);
                Coord px2 = px + bottomChartRect.Width() / horizontalAllocation;
                px -= 3; px2 += 2;
                Coord py = LinearInterpolate(bottomChartRect._bottomRight[1], bottomChartRect._topLeft[1], (bottomSeries._values[bottomSeries._peakIndex] - bottomSeries._minValue) / float(bottomSeries._maxValue - bottomSeries._minValue));
                Float3 lines[] { 
                    AsPixelCoords(Coord2{px, py}), AsPixelCoords(Coord2{px2, py}),
                    AsPixelCoords(Coord2{(px+px2)/2, py}), AsPixelCoords(Coord2{(px+px2)/2, controlRect._bottomRight[1] - border})
                };
                context.DrawLines(ProjectionMode::P2D, lines, dimof(lines), GraphBorder);

                auto section = (StringMeldInPlace(buffer) << bottomSeries._values[bottomSeries._peakIndex]).AsStringSection();
                auto width = StringWidth(*fonts->_smallFont, section);
                Rect bubble{Coord2{(px+px2-width)/2 - 3, controlRect._bottomRight[1]-border-3}, Coord2{(px+px2+width)/2 + 3, controlRect._bottomRight[1]}};
                if (bubble._topLeft[0] < controlRect._topLeft[0]) { auto shift = controlRect._topLeft[0] - bubble._topLeft[0]; bubble._topLeft[0] += shift; bubble._bottomRight[0] += shift; }
                if (bubble._bottomRight[0] > controlRect._bottomRight[0]) { auto shift = bubble._bottomRight[0] - controlRect._bottomRight[0]; bubble._topLeft[0] -= shift; bubble._bottomRight[0] -= shift; }
                FillAndOutlineRoundedRectangle(context, bubble, RenderOverlays::ColorB::Black, GraphBorder);
                DrawText().Font(*fonts->_smallFont).Alignment(TextAlignment::Center).Color(GraphText).Draw(context, bubble, section);
            }
        }

        return { topChartRect._topLeft, Coord2{bottomChartRect._bottomRight[0], bottomChartRect._topLeft[1]} };
    }

    static void HighlightChartPoint(IOverlayContext& context, const Rect& area, float value, float minValue, float maxValue)
    {
        auto* fonts = ConsoleRig::TryActualizeCachedBox<BUFontBox>();
        if (!fonts) return;

        Coord2 px = {
            (area._topLeft[0] + area._bottomRight[0]) / 2,
            LinearInterpolate(area._bottomRight[1], area._topLeft[1], (value-minValue)/(maxValue-minValue))
        };
        if (area._bottomRight[1] > area._topLeft[1]) px[1] -= 10;
        else if (area._bottomRight[1] > area._topLeft[1]) px[1] += 10;

        char buffer[256];
        auto section = (StringMeldInPlace(buffer) << value).AsStringSection();
        DrawText().Font(*fonts->_smallFont).Alignment(TextAlignment::Center).Color(GraphText).Draw(context, {px, px}, section);
    }

    size_t  BufferUploadDisplay::FillValuesBuffer(unsigned graphType, unsigned uploadType, float valuesBuffer[], size_t valuesMaxCount)
    {
        using namespace BufferUploads;
        size_t valuesCount = 0;
        for (std::deque<FrameRecord>::const_reverse_iterator i =_frames.rbegin(); i!=_frames.rend(); ++i) {
            if (valuesCount>=valuesMaxCount) {
                break;
            }
            ++valuesCount;
            using namespace GraphTabs;
            float& value = valuesBuffer[valuesMaxCount-valuesCount];
            value = 0.f;

                // Calculate the requested value ... 

            if (graphType == Latency) { // latency (ms)
                TimeMarker transactionLatencySum = 0;
                unsigned transactionLatencyCount = 0;
                for (unsigned cl=i->_commandListStart; cl<i->_commandListEnd; ++cl) {
                    BufferUploads::CommandListMetrics& commandList = _recentHistory[cl];
                    for (unsigned i2=0; i2<commandList.RetirementCount(); ++i2) {
                        const AssemblyLineRetirement& retirement = commandList.Retirement(i2);
                        transactionLatencySum += retirement._retirementTime - retirement._requestTime;
                        ++transactionLatencyCount;
                    }
                }

                float averageTransactionLatency = transactionLatencyCount?float(double(transactionLatencySum/TimeMarker(transactionLatencyCount)) * _reciprocalTimerFrequency):0.f;
                value = averageTransactionLatency;
            } else if (graphType == PendingBuffers) { // pending buffers
                if (i->_commandListStart!=i->_commandListEnd) {
                    value = _recentHistory[i->_commandListEnd-1]._assemblyLineMetrics._queuedBytes[uploadType] / (1024.f*1024.f);
                }
            } else if ((graphType >= Uploads && graphType <= FramePriorityStall) || graphType == StagingMaxNextBlock || graphType == StagingAwaitingDevice) {
                for (unsigned cl=i->_commandListStart; cl<i->_commandListEnd; ++cl) {
                    BufferUploads::CommandListMetrics& commandList = _recentHistory[cl];
                    if (_graphsMode == Uploads) { // bytes uploaded
                        value += (commandList._bytesUploaded[uploadType]) / (1024.f*1024.f);
                    } else if (_graphsMode == CreatesMB) { // creations (bytes)
                        value += commandList._bytesCreated[uploadType] / (1024.f*1024.f);
                    } else if (_graphsMode == CreatesCount) { // creations (count)
                        value += commandList._countCreations[uploadType];
                    } else if (_graphsMode == DeviceCreatesCount) {
                        value += commandList._countDeviceCreations[uploadType];
                    } else if (_graphsMode == StagingBufferAllocated) {
                        value += commandList._stagingBytesAllocated[uploadType] / (1024.f*1024.f);
                    } else if (_graphsMode == FramePriorityStall) {
                        value += float(commandList._framePriorityStallTime * _reciprocalTimerFrequency * 1000.f);
                    } else if (_graphsMode == StagingMaxNextBlock) {
                        value += commandList._assemblyLineMetrics._stagingPageMetrics._maxNextBlockBytes / (1024.f*1024.f);
                    } else if (_graphsMode == StagingAwaitingDevice) {
                        value += commandList._assemblyLineMetrics._stagingPageMetrics._bytesAwaitingDevice / (1024.f*1024.f);
                    }
                }
            } else if (_graphsMode == CommandListCount) {
                value = float(i->_commandListEnd-i->_commandListStart);
            } else if (_graphsMode == GPUCost) {
                value = i->_gpuCost;
            } else if (_graphsMode == GPUBytesPerSecond) {
                value = i->_gpuMetrics._slidingAverageBytesPerSecond / (1024.f * 1024.f);
            } else if (_graphsMode == AveGPUCost) {
                value = i->_gpuMetrics._slidingAverageCostMS;
            } else if (_graphsMode == ThreadActivity) {
                TimeMarker processingTimeSum = 0, waitTimeSum = 0;
                for (unsigned cl=i->_commandListStart; cl<i->_commandListEnd; ++cl) {
                    BufferUploads::CommandListMetrics& commandList = _recentHistory[cl];
                    processingTimeSum += commandList._processingEnd - commandList._processingStart;
                    waitTimeSum += commandList._waitTime;
                }
                value = (float(processingTimeSum))?(100.f * (1.0f-(waitTimeSum/float(processingTimeSum)))):0.f;
            } else if (_graphsMode == BatchedCopy) {
                value = 0;
                /*for (unsigned cl=i->_commandListStart; cl<i->_commandListEnd; ++cl) {
                    BufferUploads::CommandListMetrics& commandList = _recentHistory[cl];
                    value += commandList._batchedCopyBytes;
                }*/
            }
        }
        return valuesCount;
    }

    void    BufferUploadDisplay::DrawDoubleGraph(
        IOverlayContext& context, Interactables& interactables, InterfaceState& interfaceState,
        const Rect& rect, unsigned topGraphSlotIdx, unsigned bottomGraphSlotIdx,
        StringSection<> topGraphName, unsigned topGraphType, unsigned topUploadType,
        StringSection<> bottomGraphName, unsigned bottomGraphType, unsigned bottomUploadType)
    {
        if (std::max(topGraphSlotIdx, bottomGraphSlotIdx) >= _graphSlots.size())
            _graphSlots.resize(std::max(topGraphSlotIdx, bottomGraphSlotIdx)+1);

        float valuesBuffer[s_MaxGraphSegments];
        float valuesBuffer2[s_MaxGraphSegments];
        XlZeroMemory(valuesBuffer);
        XlZeroMemory(valuesBuffer2);

        size_t valuesCount = FillValuesBuffer(topGraphType, topUploadType, valuesBuffer, dimof(valuesBuffer));
        auto& topGraphSlot = _graphSlots[topGraphSlotIdx];
        GraphSeries<float> topSeries{
            MakeIteratorRange(&valuesBuffer[dimof(valuesBuffer)-valuesCount], &valuesBuffer[dimof(valuesBuffer)]),
            topGraphSlot._minHistory, topGraphSlot._maxHistory };

        size_t valuesCount2 = FillValuesBuffer(bottomGraphType, bottomUploadType, valuesBuffer2, dimof(valuesBuffer2));
        auto& bottomGraphSlot = _graphSlots[bottomGraphSlotIdx];
        GraphSeries<float> bottomSeries{
            MakeIteratorRange(&valuesBuffer2[dimof(valuesBuffer2)-valuesCount2], &valuesBuffer2[dimof(valuesBuffer2)]),
            bottomGraphSlot._minHistory, bottomGraphSlot._maxHistory };

        bottomSeries._minValue = topSeries._minValue = std::min(topSeries._minValue, bottomSeries._minValue);
        auto chartArea = DrawUploadsGraph(
            context, rect,
            topSeries, bottomSeries,
            topGraphName, bottomGraphName,
            s_MaxGraphSegments);

        /////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
            //  extra graph functionality....

        {
            const InteractableId framePicker = InteractableId_Make("FramePicker");
            size_t iterator = 0;
            unsigned frameLeft = (unsigned)std::max(int(_frames.size() - s_MaxGraphSegments), 0);
            for (auto i =_frames.begin()+frameLeft; i!=_frames.end(); ++i) {
                Rect graphPart{
                    Coord2{LinearInterpolate(chartArea._topLeft[0], chartArea._bottomRight[0], iterator/float(s_MaxGraphSegments)), chartArea._topLeft[1]},
                    Coord2{LinearInterpolate(chartArea._topLeft[0], chartArea._bottomRight[0], (iterator+1)/float(s_MaxGraphSegments)), chartArea._bottomRight[1]}};
                InteractableId id = framePicker + std::distance(_frames.begin(), i);
                if (i->_frameId == _lockedFrameId) {
                    FillRectangle(context, graphPart, ColorB(0x3f7f3f7fu));
                    HighlightChartPoint(
                        context, Rect { graphPart._topLeft, {graphPart._bottomRight[0], (chartArea._topLeft[1] + chartArea._bottomRight[1])/2} },
                        topSeries._values[iterator], topSeries._minValue, topSeries._maxValue);
                    
                    HighlightChartPoint(
                        context, Rect { {graphPart._topLeft[0], graphPart._bottomRight[1]}, {graphPart._bottomRight[0], (chartArea._topLeft[1] + chartArea._bottomRight[1])/2} },
                        bottomSeries._values[iterator], bottomSeries._minValue, bottomSeries._maxValue);
                } else if (interfaceState.HasMouseOver(id)) {
                    FillRectangle(context, graphPart, ColorB(0x3f7f7f7fu));
                }
                interactables.Register({graphPart, id});
                ++iterator;
            }
        }
    }

    void    BufferUploadDisplay::DrawDisplay(IOverlayContext& context, Layout& layout, Interactables& interactables, InterfaceState& interfaceState)
    {
        using namespace BufferUploads;
        static unsigned GraphHeight = 196;

        switch (_graphsMode) {
        case GraphTabs::Uploads:
        case GraphTabs::CreatesMB:
        case GraphTabs::CreatesCount:
        case GraphTabs::DeviceCreatesCount:
        case GraphTabs::PendingBuffers:
        case GraphTabs::StagingBufferAllocated:
            DrawDoubleGraph(
                context, interactables, interfaceState,
                layout.AllocateFullWidth(GraphHeight),
                0, 1,
                "Textures", _graphsMode, (unsigned)UploadDataType::Texture,
                "Textures", _graphsMode, (unsigned)UploadDataType::Texture);

            DrawDoubleGraph(
                context, interactables, interfaceState,
                layout.AllocateFullWidth(GraphHeight),
                2, 3,
                "Geometry", _graphsMode, (unsigned)UploadDataType::GeometryBuffer,
                "Uniforms", _graphsMode, (unsigned)UploadDataType::UniformBuffer);
            break;
        
        default:
            DrawDoubleGraph(
                context, interactables, interfaceState,
                layout.AllocateFullWidth(GraphHeight),
                0, 1,
                GraphTabs::Names[_graphsMode], _graphsMode, (unsigned)UploadDataType::Texture,
                GraphTabs::Names[_graphsMode], _graphsMode, (unsigned)UploadDataType::Texture);
            break;
        };
    }

    void    BufferUploadDisplay::DrawStatistics(
        IOverlayContext& context, Layout& layout, 
        Interactables& interactables, InterfaceState& interfaceState,
        const BufferUploads::CommandListMetrics& mostRecentResults)
    {
        using namespace BufferUploads;

        GPUMetrics gpuMetrics = CalculateGPUMetrics();

            //////
        TimeMarker transactionLatencySum = 0, commandListLatencySum = 0;
        unsigned transactionLatencyCount = 0, commandListLatencyCount = 0;
        for (auto i =_recentHistory.rbegin();i!=_recentHistory.rend(); ++i) {
            for (unsigned i2=0; i2<i->RetirementCount(); ++i2) {
                const AssemblyLineRetirement& retire = i->Retirement(i2);
                transactionLatencySum += retire._retirementTime - retire._requestTime;
                ++transactionLatencyCount;
            }
            commandListLatencySum += i->_commitTime - i->_resolveTime;
            ++commandListLatencyCount;
        }

        TimeMarker processingTimeSum = 0, waitTimeSum = 0;
        unsigned wakeCountSum = 0;
            
        size_t lastValidFrameIndex = _frames.size()-1;
        for (std::deque<FrameRecord>::reverse_iterator i=_frames.rbegin(); i!=_frames.rend(); ++i, --lastValidFrameIndex)
            if (i->_commandListStart != i->_commandListEnd)
                break;
        if (lastValidFrameIndex < _frames.size())
            for (unsigned cl=_frames[lastValidFrameIndex]._commandListStart; cl<_frames[lastValidFrameIndex]._commandListEnd; ++cl) {
                BufferUploads::CommandListMetrics& commandList = _recentHistory[cl];
                processingTimeSum += commandList._processingEnd - commandList._processingStart;
                waitTimeSum += commandList._waitTime;
                wakeCountSum += commandList._wakeCount;
            }

        float averageTransactionLatency = transactionLatencyCount?float(double(transactionLatencySum/TimeMarker(transactionLatencyCount)) * _reciprocalTimerFrequency):0.f;
        float averageCommandListLatency = commandListLatencyCount?float(double(commandListLatencySum/TimeMarker(commandListLatencyCount)) * _reciprocalTimerFrequency):0.f;

        const auto lineHeight = 20u;
        const ColorB headerColor = ColorB::Blue;
        std::pair<std::string, unsigned> headers0[] = { std::make_pair("Name", 300), std::make_pair("Value", 3000) };
        std::pair<std::string, unsigned> headers1[] = { std::make_pair("Name", 300), std::make_pair("Tex", 150), std::make_pair("Geo", 150), std::make_pair("Uniforms", 300) };
        char buffer[256];
            
        DrawTableHeaders(context, layout.AllocateFullWidth(lineHeight), MakeIteratorRange(headers0), headerColor, &interactables);
        DrawTableEntry(context, layout.AllocateFullWidth(lineHeight), MakeIteratorRange(headers0), 
            {   std::make_pair("Name", "Ave latency"), 
                std::make_pair("Value", XlDynFormatString("%6.2f ms", averageTransactionLatency * 1000.f)) });

        DrawTableEntry(context, layout.AllocateFullWidth(lineHeight), MakeIteratorRange(headers0), 
            {   std::make_pair("Name", "Command list latency"), 
                std::make_pair("Value", XlDynFormatString("%6.2f ms", averageCommandListLatency * 1000.f)) });

        DrawTableEntry(context, layout.AllocateFullWidth(lineHeight), MakeIteratorRange(headers0), 
            {   std::make_pair("Name", "GPU theoretical MB/second"), 
                std::make_pair("Value", XlDynFormatString("%6.2f MB/s", gpuMetrics._slidingAverageBytesPerSecond/float(1024.f*1024.f))) });

        DrawTableEntry(context, layout.AllocateFullWidth(lineHeight), MakeIteratorRange(headers0), 
            {   std::make_pair("Name", "GPU ave cost"), 
                std::make_pair("Value", XlDynFormatString("%6.2f ms", gpuMetrics._slidingAverageCostMS)) });

        DrawTableEntry(context, layout.AllocateFullWidth(lineHeight), MakeIteratorRange(headers0), 
            {   std::make_pair("Name", "Thread activity"), 
                std::make_pair("Value", XlDynFormatString("%6.3f%% (%i)", (float(processingTimeSum))?(100.f * (1.0f-(waitTimeSum/float(processingTimeSum)))):0.f, wakeCountSum)) });

        DrawTableEntry(context, layout.AllocateFullWidth(lineHeight), MakeIteratorRange(headers0), 
            {   std::make_pair("Name", "Prepare staging steps (peak)"), 
                std::make_pair("Value", XlDynFormatString("%i (%i)", mostRecentResults._assemblyLineMetrics._queuedPrepareStaging, mostRecentResults._assemblyLineMetrics._peakPrepareStaging)) });

        DrawTableEntry(context, layout.AllocateFullWidth(lineHeight), MakeIteratorRange(headers0), 
            {   std::make_pair("Name", "Transfer staging steps (peak)"),
                std::make_pair("Value", XlDynFormatString("%i (%i)", mostRecentResults._assemblyLineMetrics._queuedTransferStagingToFinal, mostRecentResults._assemblyLineMetrics._peakTransferStagingToFinal)) });

        DrawTableEntry(context, layout.AllocateFullWidth(lineHeight), MakeIteratorRange(headers0), 
            {   std::make_pair("Name", "Create from pkt steps (peak)"),
                std::make_pair("Value", XlDynFormatString("%i (%i)", mostRecentResults._assemblyLineMetrics._queuedCreateFromDataPacket, mostRecentResults._assemblyLineMetrics._peakCreateFromDataPacket)) });

        DrawTableEntry(context, layout.AllocateFullWidth(lineHeight), MakeIteratorRange(headers0), 
            {   std::make_pair("Name", "Transaction count"),
                std::make_pair("Value", XlDynFormatString("%i/%i", mostRecentResults._assemblyLineMetrics._transactionCount, mostRecentResults._assemblyLineMetrics._temporaryTransactionsAllocated)) });

        DrawTableEntry(context, layout.AllocateFullWidth(lineHeight), MakeIteratorRange(headers0), 
            {   std::make_pair("Name", "Staging allocated"),
                std::make_pair("Value", (StringMeldInPlace(buffer) << ByteCount{mostRecentResults._assemblyLineMetrics._stagingPageMetrics._bytesAllocated}).AsString()) });

        DrawTableEntry(context, layout.AllocateFullWidth(lineHeight), MakeIteratorRange(headers0), 
            {   std::make_pair("Name", "Staging max next block"),
                std::make_pair("Value", (StringMeldInPlace(buffer) << ByteCount{mostRecentResults._assemblyLineMetrics._stagingPageMetrics._maxNextBlockBytes}).AsString()) });

        DrawTableEntry(context, layout.AllocateFullWidth(lineHeight), MakeIteratorRange(headers0), 
            {   std::make_pair("Name", "Staging awaiting device"),
                std::make_pair("Value", (StringMeldInPlace(buffer) << ByteCount{mostRecentResults._assemblyLineMetrics._stagingPageMetrics._bytesAwaitingDevice}).AsString()) });

        DrawTableEntry(context, layout.AllocateFullWidth(lineHeight), MakeIteratorRange(headers0), 
            {   std::make_pair("Name", "Staging locked on ordering"),
                std::make_pair("Value", (StringMeldInPlace(buffer) << ByteCount{mostRecentResults._assemblyLineMetrics._stagingPageMetrics._bytesLockedDueToOrdering}).AsString()) });

        ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
        DrawTableHeaders(context, layout.AllocateFullWidth(lineHeight), MakeIteratorRange(headers1), headerColor, &interactables);
        DrawTableEntry(context, layout.AllocateFullWidth(lineHeight), MakeIteratorRange(headers1), 
            {   std::make_pair("Name", "Recent creates"), 
                std::make_pair("Tex", XlDynFormatString("%i", mostRecentResults._countCreations[(unsigned)UploadDataType::Texture])),
                std::make_pair("Geo", XlDynFormatString("%i", mostRecentResults._countCreations[(unsigned)UploadDataType::GeometryBuffer])),
                std::make_pair("Uniforms", XlDynFormatString("%i", mostRecentResults._countCreations[(unsigned)UploadDataType::UniformBuffer])) });

        DrawTableEntry(context, layout.AllocateFullWidth(lineHeight), MakeIteratorRange(headers1), 
            {   std::make_pair("Name", "Acc creates"), 
                std::make_pair("Tex", XlDynFormatString("%i", _accumulatedCreateCount[(unsigned)UploadDataType::Texture])),
                std::make_pair("Geo", XlDynFormatString("%i", _accumulatedCreateCount[(unsigned)UploadDataType::GeometryBuffer])),
                std::make_pair("Uniforms", XlDynFormatString("%i", _accumulatedCreateCount[(unsigned)UploadDataType::UniformBuffer])) });

        DrawTableEntry(context, layout.AllocateFullWidth(lineHeight), MakeIteratorRange(headers1), 
            {   std::make_pair("Name", "Acc creates (MB)"), 
                std::make_pair("Tex", (StringMeldInPlace(buffer) << ByteCount{_accumulatedCreateBytes[(unsigned)UploadDataType::Texture]}).AsString()),
                std::make_pair("Geo", (StringMeldInPlace(buffer) << ByteCount{_accumulatedCreateBytes[(unsigned)UploadDataType::GeometryBuffer]}).AsString()),
                std::make_pair("Uniforms", (StringMeldInPlace(buffer) << ByteCount{_accumulatedCreateBytes[(unsigned)UploadDataType::UniformBuffer]}).AsString()) });

        DrawTableEntry(context, layout.AllocateFullWidth(lineHeight), MakeIteratorRange(headers1), 
            {   std::make_pair("Name", "Acc uploads"), 
                std::make_pair("Tex", XlDynFormatString("%i", _accumulatedUploadCount[(unsigned)UploadDataType::Texture])),
                std::make_pair("Geo", XlDynFormatString("%i", _accumulatedUploadCount[(unsigned)UploadDataType::GeometryBuffer])),
                std::make_pair("Uniforms", XlDynFormatString("%i", _accumulatedUploadCount[(unsigned)UploadDataType::UniformBuffer])) });

        DrawTableEntry(context, layout.AllocateFullWidth(lineHeight), MakeIteratorRange(headers1), 
            {   std::make_pair("Name", "Acc uploads (MB)"), 
                std::make_pair("Tex", (StringMeldInPlace(buffer) << ByteCount{_accumulatedUploadBytes[(unsigned)UploadDataType::Texture]}).AsString()),
                std::make_pair("Geo", (StringMeldInPlace(buffer) << ByteCount{_accumulatedUploadBytes[(unsigned)UploadDataType::GeometryBuffer]}).AsString()),
                std::make_pair("Uniforms", (StringMeldInPlace(buffer) << ByteCount{_accumulatedUploadBytes[(unsigned)UploadDataType::UniformBuffer]}).AsString()) });
    }

    void    BufferUploadDisplay::DrawRecentRetirements(
        IOverlayContext& context, Layout& layout, 
        Interactables& interactables, InterfaceState& interfaceState)
    {
        const auto lineHeight = 20u;
        const ColorB headerColor = ColorB::Blue;
        std::pair<std::string, unsigned> headers[] = { std::make_pair("Name", 500), std::make_pair("Latency (ms)", 160), std::make_pair("Type", 80), std::make_pair("Description", 3000) };
            
        DrawTableHeaders(context, layout.AllocateFullWidth(lineHeight), MakeIteratorRange(headers), headerColor, &interactables);

        unsigned frameCounter = 0;
        for (auto i=_frames.rbegin(); i!=_frames.rend(); ++i, ++frameCounter) {
            if (_lockedFrameId != ~unsigned(0x0) && i->_frameId != _lockedFrameId) {
                continue;
            }
            for (int cl=int(i->_commandListEnd)-1; cl>=int(i->_commandListStart); --cl) {
                const auto& commandList = _recentHistory[cl];
                for (unsigned i2=0; i2<commandList.RetirementCount(); ++i2) {
                    Rect rect = layout.AllocateFullWidth(lineHeight);
                    if (!(IsGood(rect) && rect._bottomRight[1] < layout._maximumSize._bottomRight[1] && rect._topLeft[1] >= layout._maximumSize._topLeft[1]))
                        break;

                    const auto& retire = commandList.Retirement(i2);
                    DrawTableEntry(context, rect, MakeIteratorRange(headers), 
                        {   std::make_pair("Name", retire._desc._name), 
                            std::make_pair("Latency (ms)", XlDynFormatString("%6.2f", float(double(retire._retirementTime-retire._requestTime)*_reciprocalTimerFrequency*1000.))),
                            std::make_pair("Type", TypeString(retire._desc)),
                            std::make_pair("Description", BuildDescription(retire._desc)) });
                }
            }
        }
    }

    void    BufferUploadDisplay::Render(IOverlayContext& context, Layout& layout, Interactables& interactables, InterfaceState& interfaceState)
    {
        using namespace BufferUploads;
        CommandListMetrics mostRecentResults;
        unsigned commandListCount = 0;

            //      Keep popping metrics from the upload manager until we stop getting valid ones            
        BufferUploads::IManager* manager = _manager;
        if (manager) {
            for (;;) {
                CommandListMetrics metrics = manager->PopMetrics();
                if (!metrics._commitTime) {
                    break;
                }
                mostRecentResults = metrics;
                _recentHistory.push_back(metrics);
                AddCommandListToFrame(metrics._frameId, unsigned(_recentHistory.size()-1));
                for (unsigned c=0; c<(unsigned)BufferUploads::UploadDataType::Max; ++c) {
                    _accumulatedCreateCount[c] += metrics._countCreations[c];
                    _accumulatedCreateBytes[c] += metrics._bytesCreated[c];
                    _accumulatedUploadCount[c] += metrics._countUploaded[c];
                    _accumulatedUploadBytes[c] += metrics._bytesUploaded[c];
                }
                ++commandListCount;
            }
        }

        if (!mostRecentResults._commitTime && _recentHistory.size()) {
            mostRecentResults = _recentHistory[_recentHistory.size()-1];
        }

        {
            ScopedLock(_gpuEventsBufferLock);
            ProcessGPUEvents_MT(AsPointer(_gpuEventsBuffer.begin()), AsPointer(_gpuEventsBuffer.end()));
            _gpuEventsBuffer.erase(_gpuEventsBuffer.begin(), _gpuEventsBuffer.end());
        }

            //      Present these frame by frame results visually.
            //      But also show information about the recent history (retired textures, etc)
        layout.AllocateFullWidthFraction(0.01f);
        Layout menuBar = layout.AllocateFullWidthFraction(.125f);
        Layout displayArea = layout.AllocateFullWidthFraction(1.f);

        if (_graphsMode == GraphTabs::Statistics) {
            DrawStatistics(context, displayArea, interactables, interfaceState, mostRecentResults);
        } else if (_graphsMode == GraphTabs::RecentRetirements) {
            DrawRecentRetirements(context, displayArea, interactables, interfaceState);
        } else {
            DrawDisplay(context, displayArea, interactables, interfaceState);
        }
        DrawMenuBar(context, menuBar, interactables, interfaceState);
    }

    auto    BufferUploadDisplay::ProcessInput(InterfaceState& interfaceState, const InputSnapshot& input) -> ProcessInputResult
    {
        if (auto topMostWidget = interfaceState.TopMostId()) {
            if (input.IsHeld_LButton()) {
                const InteractableId framePicker = InteractableId_Make("FramePicker");
                if (topMostWidget >= framePicker && topMostWidget < (framePicker+s_MaxGraphSegments)) {
                    unsigned graphIndex = unsigned(topMostWidget - framePicker);
                    _lockedFrameId = _frames[graphIndex]._frameId;
                    return ProcessInputResult::Consumed;
                }
            }

            if (input.IsRelease_LButton()) {
                for (unsigned c=0; c<dimof(GraphTabs::Names); ++c) {
                    if (topMostWidget == InteractableId_Make(GraphTabs::Names[c])) {
                        _graphsMode = c;
                        _graphSlots.clear();
                        return ProcessInputResult::Consumed;
                    }
                }
            }
        }
        return ProcessInputResult::Passthrough;
    }

    void    BufferUploadDisplay::AddCommandListToFrame(unsigned frameId, unsigned commandListIndex)
    {
        for (std::deque<FrameRecord>::reverse_iterator i=_frames.rbegin(); i!=_frames.rend(); ++i) {
            if (i->_frameId == frameId) {
                if (i->_commandListStart == ~unsigned(0x0)) {
                    i->_commandListStart = commandListIndex;
                    i->_commandListEnd = commandListIndex+1;
                } else {
                    assert(commandListIndex == i->_commandListEnd || commandListIndex == (i->_commandListEnd-1));
                    i->_commandListEnd = std::max(i->_commandListEnd, commandListIndex+1);
                }
                i->_gpuMetrics = CalculateGPUMetrics();
                return;
            } else if (i->_frameId < frameId) {
                    //      We went too far and didn't find this frame... We'll have to insert it in as a new frame.
                FrameRecord newFrame;
                newFrame._frameId = frameId;
                newFrame._commandListStart = commandListIndex;
                newFrame._commandListEnd = commandListIndex+1;
                std::deque<FrameRecord>::iterator newItem = _frames.insert(i.base(),newFrame);
                newItem->_gpuMetrics = CalculateGPUMetrics();
                return;
            }
        }

        FrameRecord newFrame;
        newFrame._frameId = frameId;
        newFrame._commandListStart = commandListIndex;
        newFrame._commandListEnd = commandListIndex+1;
        _frames.push_back(newFrame);
        _frames[_frames.size()-1]._gpuMetrics = CalculateGPUMetrics();
    }

    void    BufferUploadDisplay::AddGPUToCostToFrame(unsigned frameId, float gpuCost)
    {
        for (std::deque<FrameRecord>::reverse_iterator i=_frames.rbegin(); i!=_frames.rend(); ++i) {
            if (i->_frameId == frameId) {
                i->_gpuCost += gpuCost;
                i->_gpuMetrics = CalculateGPUMetrics();
                return;
            } else if (i->_frameId < frameId) {
                // we went too far and didn't find this frame... We'll have to insert it in as a new frame.
                FrameRecord newFrame;
                newFrame._frameId = frameId;
                newFrame._gpuCost = gpuCost;
                std::deque<FrameRecord>::iterator newItem = _frames.insert(i.base(),newFrame);
                newItem->_gpuMetrics = CalculateGPUMetrics();
                return;
            }
        }

        FrameRecord newFrame;
        newFrame._frameId = frameId;
        newFrame._gpuCost = gpuCost;
        _frames.push_back(newFrame);
        _frames[_frames.size()-1]._gpuMetrics = CalculateGPUMetrics();
    }

    BufferUploadDisplay::GPUMetrics   BufferUploadDisplay::CalculateGPUMetrics()
    {
                //////
                //      calculate the GPU upload speed... How much GPU time should we expect to consume per mb uploaded,
                //      based on a sliding average
        BufferUploadDisplay::GPUMetrics result;
        result._slidingAverageBytesPerSecond = 0;
        result._slidingAverageCostMS = 0.f;

        unsigned framesCountWithValidGPUCost = (unsigned)_frames.size();
        for (std::deque<FrameRecord>::reverse_iterator i=_frames.rbegin(); i!=_frames.rend(); ++i, --framesCountWithValidGPUCost) {
            if (i->_gpuCost != 0.f && i->_commandListStart != i->_commandListEnd) {
                break;
            }
        }

        const unsigned samples = std::min(unsigned(framesCountWithValidGPUCost), 256u);
        std::deque<FrameRecord>::const_iterator gI = _frames.begin() + (_frames.size()-samples);
        float totalGPUCost = 0.f; unsigned totalBytesUploaded = 0;
        for (unsigned c=0; c<samples; ++c, ++gI) {
            float gpuCost = gI->_gpuCost; unsigned bytesUploaded = 0;
            for (unsigned cl=gI->_commandListStart; cl<gI->_commandListEnd; ++cl) {
                BufferUploads::CommandListMetrics& commandList = _recentHistory[cl];
                for (unsigned c2=0; c2<(unsigned)BufferUploads::UploadDataType::Max; ++c2) {
                    bytesUploaded += commandList._bytesUploaded[c2];
                }
            }
            totalGPUCost += gpuCost;
            totalBytesUploaded += bytesUploaded;
        }
        if (totalGPUCost) {
            result._slidingAverageBytesPerSecond = unsigned(totalBytesUploaded / (totalGPUCost/1000.f));
        }
        if (samples) {
            result._slidingAverageCostMS = totalGPUCost / float(samples);
        }
        return result;
    }

    void    BufferUploadDisplay::ProcessGPUEvents(const void* eventsBufferStart, const void* eventsBufferEnd)
    {
        ScopedLock(_gpuEventsBufferLock);
        size_t oldSize = _gpuEventsBuffer.size();
        size_t eventsBufferSize = ptrdiff_t(eventsBufferEnd) - ptrdiff_t(eventsBufferStart);
        _gpuEventsBuffer.resize(_gpuEventsBuffer.size() + eventsBufferSize);
        memcpy(&_gpuEventsBuffer[oldSize], eventsBufferStart, eventsBufferSize);
    }

    void    BufferUploadDisplay::ProcessGPUEvents_MT(const void* eventsBufferStart, const void* eventsBufferEnd)
    {
        const void * evnt = eventsBufferStart;
        while (evnt < eventsBufferEnd) {
            uint32 eventType = (uint32)*((const size_t*)evnt); evnt = PtrAdd(evnt, sizeof(size_t));
            if (eventType == ~uint32(0x0)) {
                size_t frameId = *((const size_t*)evnt); evnt = PtrAdd(evnt, sizeof(size_t));
                GPUTime frequency = *((const uint64_t*)evnt); evnt = PtrAdd(evnt, sizeof(uint64_t));
                _mostRecentGPUFrequency = frequency;
                _mostRecentGPUFrameId = (unsigned)frameId;
            } else {
                const char* eventName = *((const char**)evnt); evnt = PtrAdd(evnt, sizeof(const char*));
                assert((size_t(evnt)%sizeof(uint64_t))==0);
                uint64_t timeValue = *((const uint64_t*)evnt); evnt = PtrAdd(evnt, sizeof(uint64_t));

                if (eventName && !XlCompareStringI(eventName, "GPU_UPLOAD")) {
                    if (eventType == 0) {
                        _lastUploadBeginTime = timeValue;
                    } else {
                        if (_lastUploadBeginTime) {
                            _mostRecentGPUCost = float(double(timeValue - _lastUploadBeginTime) / double(_mostRecentGPUFrequency) * 1000.);

                                //      write this result into the GPU time for any frames that need it...
                            AddGPUToCostToFrame(_mostRecentGPUFrameId, _mostRecentGPUCost);
                        }
                    }
                }
            }
        }
    }

    BufferUploadDisplay* BufferUploadDisplay::s_gpuListenerDisplay = 0;
    void BufferUploadDisplay::GPUEventListener(const void* eventsBufferStart, const void* eventsBufferEnd)
    {
        if (s_gpuListenerDisplay) {
            s_gpuListenerDisplay->ProcessGPUEvents(eventsBufferStart, eventsBufferEnd);
        }
    }


        ////////////////////////////////////////////////////////////////////

    ResourcePoolDisplay::ResourcePoolDisplay(BufferUploads::IManager* manager)
    {
        _manager = manager;
        _filter = 0;
        _detailsIndex = 0;
        _graphMin = _graphMax = 0.f;
    }

    ResourcePoolDisplay::~ResourcePoolDisplay()
    {
    }

    namespace ResourcePoolDisplayTabs
    {
        static const char* Names[] = { "Index Buffers", "Vertex Buffers", "Staging Textures" };
    }

    bool    ResourcePoolDisplay::Filter(const RenderCore::ResourceDesc& desc)
    {
        if (_filter == 0 && (desc._bindFlags&BufferUploads::BindFlag::IndexBuffer)) return true;
        if (_filter == 1 && (desc._bindFlags&BufferUploads::BindFlag::VertexBuffer)) return true;
        if (_filter == 2 && (desc._type == RenderCore::ResourceDesc::Type::Texture)) return true;
        return false;
    }

    static const InteractableId ResourcePoolDisplayGraph = InteractableId_Make("ResourcePoolDisplayGraph");

    void    ResourcePoolDisplay::Render(IOverlayContext& context, Layout& layout, Interactables&interactables, InterfaceState& interfaceState)
    {
        using namespace BufferUploads;
        IManager* manager = _manager;
        if (manager) {
            PoolSystemMetrics metrics = manager->CalculatePoolMetrics();

                /////////////////////////////////////////////////////////////////////////////
            const std::vector<PoolMetrics>& metricsVector = (_filter==2)?metrics._stagingPools:metrics._resourcePools;
            size_t maxSize = 0, count = 0;
            for (std::vector<PoolMetrics>::const_iterator i=metricsVector.begin();i!=metricsVector.end(); ++i) {
                if (Filter(i->_desc)) {
                    maxSize = std::max(maxSize, i->_peakSize);
                    ++count;
                }
            }

                /////////////////////////////////////////////////////////////////////////////
            layout.AllocateFullWidth(128);  // leave some space at the top
            Layout buttonsLayout(layout.AllocateFullWidth(32));
            for (unsigned c=0; c<dimof(ResourcePoolDisplayTabs::Names); ++c) {
                DrawButton(context, ResourcePoolDisplayTabs::Names[c], buttonsLayout.AllocateFullHeightFraction(1.f/float(dimof(ResourcePoolDisplayTabs::Names))), interactables, interfaceState);
            }

            if (count) {
                    /////////////////////////////////////////////////////////////////////////////
                Rect barChartRect = layout.AllocateFullWidth(400);
                Layout barsLayout(barChartRect);
                barsLayout._paddingBetweenAllocations = 4;
                const unsigned barWidth = (barChartRect.Width() - (count-1) * barsLayout._paddingBetweenAllocations - 2*barsLayout._paddingInternalBorder) / count;

                static ColorB rectColor(96, 192, 170, 128);
                static ColorB peakMarkerColor(192, 64, 64, 128);
                static ColorB textColour(192, 192, 192, 128);
            
                    /////////////////////////////////////////////////////////////////////////////
                unsigned c=0;
                const PoolMetrics* detailsMetrics = NULL;
                for (std::vector<PoolMetrics>::const_iterator i=metricsVector.begin(); i!=metricsVector.end(); ++i) {
                    if (Filter(i->_desc)) {
                        float A = i->_currentSize / float(maxSize);
                        float B = i->_peakSize / float(maxSize);
                        Rect fullRect = barsLayout.AllocateFullHeight(barWidth);
                        Rect colouredRect(Coord2(fullRect._topLeft[0], LinearInterpolate(fullRect._topLeft[1], fullRect._bottomRight[1], 1.f-A)), fullRect._bottomRight);
                        FillRectangle(context, colouredRect, rectColor);
                        FillRectangle(context, Rect(    Coord2(fullRect._topLeft[0], LinearInterpolate(fullRect._topLeft[1], fullRect._bottomRight[1], 1.f-B)),
                                                        Coord2(fullRect._bottomRight[0], LinearInterpolate(fullRect._topLeft[1], fullRect._bottomRight[1], 1.f-B)+2)), peakMarkerColor);

                        Rect textRect(colouredRect._topLeft, Coord2(colouredRect._bottomRight[0], colouredRect._topLeft[1]+10));
                        if (i->_peakSize) {
                            const auto& desc = i->_desc;
                            if (desc._type == RenderCore::ResourceDesc::Type::LinearBuffer) {
                                if (desc._bindFlags & BindFlag::IndexBuffer) {
                                    DrawText().Color(textColour).FormatAndDraw(context, textRect, "IB %6.2fk", desc._linearBufferDesc._sizeInBytes / 1024.f);
                                } else if (desc._bindFlags & BindFlag::VertexBuffer) {
                                    DrawText().Color(textColour).FormatAndDraw(context, textRect, "VB %6.2fk", desc._linearBufferDesc._sizeInBytes / 1024.f);
                                } else {
                                    DrawText().Color(textColour).FormatAndDraw(context, textRect, "B %6.2fk", desc._linearBufferDesc._sizeInBytes / 1024.f);
                                }
                            } else if (desc._type == RenderCore::ResourceDesc::Type::Texture) {
                                DrawText().Color(textColour).FormatAndDraw(context, textRect, "Tex %ix%i", desc._textureDesc._width, desc._textureDesc._height);
                            }
                            textRect._topLeft[1] += 16; textRect._bottomRight[1] += 16;
                            if (i->_currentSize) {
                                DrawText().Color(textColour).FormatAndDraw(context, textRect, "%i (%6.3fMB)", 
                                    i->_currentSize, (i->_currentSize * RenderCore::ByteCount(i->_desc)) / (1024.f*1024.f));
                            }
                        }

                        InteractableId id = ResourcePoolDisplayGraph+c;
                        if (_detailsIndex==c) {
                            detailsMetrics = &(*i);
                        }
                        interactables.Register({fullRect, id});
                        ++c;
                    }
                }

                if (detailsMetrics) {
                    _detailsHistory.push_back(*detailsMetrics);
                    Rect textRect = layout.AllocateFullWidth(32);
                    DrawText().Color(textColour).FormatAndDraw(context, textRect, "Real size: %6.2fMB, Created size: %6.2fMB, Padding overhead: %6.2fMB, Count: %i",
                        detailsMetrics->_totalRealSize/(1024.f*1024.f), detailsMetrics->_totalCreateSize/(1024.f*1024.f), (detailsMetrics->_totalCreateSize-detailsMetrics->_totalRealSize)/(1024.f*1024.f),
                        detailsMetrics->_totalCreateCount);

                    Rect historyRect = layout.AllocateFullWidth(200);
                    float historyValues[256];
                    unsigned historyCount = 0;
                    for (std::vector<PoolMetrics>::const_reverse_iterator i=_detailsHistory.rbegin(); i!=_detailsHistory.rend() && historyCount < dimof(historyValues); ++i, ++historyCount) {
                        historyValues[historyCount] = float(i->_recentReleaseCount);
                    }
                    DrawHistoryGraph(context, historyRect, historyValues, historyCount, dimof(historyValues), _graphMin, _graphMax);
                }
            }
        }
    }

    auto    ResourcePoolDisplay::ProcessInput(InterfaceState& interfaceState, const InputSnapshot& input) -> ProcessInputResult
    {
        if (interfaceState.TopMostId()) {
            if (input.IsRelease_LButton()) {
                InteractableId topMostWidget = interfaceState.TopMostId();
                if (topMostWidget == InteractableId_Make(ResourcePoolDisplayTabs::Names[0])) {
                    _filter = 0;
                    return ProcessInputResult::Consumed;
                } else if (topMostWidget == InteractableId_Make(ResourcePoolDisplayTabs::Names[1])) {
                    _filter = 1;
                    return ProcessInputResult::Consumed;
                } else if (topMostWidget == InteractableId_Make(ResourcePoolDisplayTabs::Names[2])) {
                    _filter = 2;
                    return ProcessInputResult::Consumed;
                } else if (topMostWidget >= ResourcePoolDisplayGraph && topMostWidget < ResourcePoolDisplayGraph+100) {
                    _detailsIndex = unsigned(topMostWidget-ResourcePoolDisplayGraph);
                    _detailsHistory.clear();
                    return ProcessInputResult::Consumed;
                }
            }
        }
        return ProcessInputResult::Passthrough;
    }



        ////////////////////////////////////////////////////////////////////

    static const unsigned FramesOfWarmth = 60;

    void    BatchingDisplay::Render(IOverlayContext& context, Layout& layout, Interactables&interactables, InterfaceState& interfaceState)
    {
        using namespace BufferUploads;
        auto metrics = _batchedResources->CalculateMetrics();

        layout.AllocateFullWidth(32);  // leave some space at the top
        static ColorB textColour(192, 192, 192, 128);
        static ColorB unallocatedLineColour(192, 192, 192, 128);

        size_t allocatedSpace = 0, unallocatedSpace = 0;
        size_t largestFreeBlock = 0;
        size_t largestHeapSize = 0;
        size_t totalBlockCount = 0;
        for (std::vector<BatchedHeapMetrics>::const_iterator i=metrics._heaps.begin(); i!=metrics._heaps.end(); ++i) {
            allocatedSpace += i->_allocatedSpace;
            unallocatedSpace += i->_unallocatedSpace;
            largestFreeBlock = std::max(largestFreeBlock, i->_largestFreeBlock);
            largestHeapSize = std::max(largestHeapSize, i->_heapSize);
            totalBlockCount += i->_referencedCountedBlockCount;
        }

        {
            DrawText().Color(textColour).FormatAndDraw(context, layout.AllocateFullWidth(16), "Heap count: %i / Total allocated: %7.3fMb / Total unallocated: %7.3fMb",
                metrics._heaps.size(), allocatedSpace/(1024.f*1024.f), unallocatedSpace/(1024.f*1024.f));
            DrawText().Color(textColour).FormatAndDraw(context, layout.AllocateFullWidth(16), "Largest free block: %7.3fKb / Average unallocated: %7.3fKb",
                largestFreeBlock/1024.f, unallocatedSpace/(float(metrics._heaps.size())*1024.f));
            DrawText().Color(textColour).FormatAndDraw(context, layout.AllocateFullWidth(16), "Block count: %i / Ave block size: %7.3fKb",
                totalBlockCount, allocatedSpace/float(totalBlockCount*1024.f));
        }

        unsigned currentFrameId = GetFrameID();

        {
            const unsigned lineHeight = 4;
            Rect outsideRect = layout.AllocateFullWidth(Coord(metrics._heaps.size()*lineHeight + layout._paddingInternalBorder*2));
            Rect heapAllocationDisplay = Layout(outsideRect).AllocateFullWidthFraction(100.f);

            OutlineRectangle(context, outsideRect, 0xff000000);

            std::vector<Float3> lines;
            std::vector<ColorB> lineColors;
            lines.reserve(metrics._heaps.size()*lineHeight*2*10);
            lineColors.reserve(metrics._heaps.size()*lineHeight*10);

            float X = heapAllocationDisplay.Width() / float(largestHeapSize);
            unsigned y = heapAllocationDisplay._topLeft[1];

            for (auto i=metrics._heaps.begin(); i!=metrics._heaps.end(); ++i) {
                unsigned heapIndex = (unsigned)std::distance(metrics._heaps.begin(), i);

                unsigned lastStart = 0;
                const bool drawAllocated = true;
                for (std::vector<unsigned>::const_iterator i2=i->_markers.begin(); (i2+1)<i->_markers.end(); i2+=2) {
                    unsigned start, end;
                    if (drawAllocated) {
                        start = lastStart;
                        end = *i2;
                    } else {
                        start = *i2;
                        end = *(i2+1);
                    }
                    if (start != end) {
                        float warmth = CalculateWarmth(heapIndex, start, end, drawAllocated);
                        ColorB col = ColorB::FromNormalized(warmth, 0.f, 1.0f-warmth);
                        for (unsigned c=0; c<lineHeight; ++c) {
                            const Coord x = Coord(start*X + heapAllocationDisplay._topLeft[0]);
                            lines.push_back(AsPixelCoords(Coord2(x, y+c)));
                            lines.push_back(AsPixelCoords(Coord2(std::max(x+1, Coord(end*X + heapAllocationDisplay._topLeft[0])), y+c)));
                            lineColors.push_back(col);
                            lineColors.push_back(col);
                        }
                    }
                    lastStart = *(i2+1);
                }

                y += lineHeight;
            }

            if (!lines.empty()) {
                context.DrawLines(ProjectionMode::P2D, AsPointer(lines.begin()), (uint32)lines.size(), AsPointer(lineColors.begin()));
            }
        }

        _lastFrameMetrics = metrics;

            //      extinquish cooling spans
        for (std::vector<WarmSpan>::iterator i=_warmSpans.begin(); i!=_warmSpans.end();) {
            if (i->_frameStart <= (currentFrameId-FramesOfWarmth)) {
                i = _warmSpans.erase(i);
            } else {
                ++i;
            }
        }
    }

    float BatchingDisplay::CalculateWarmth(unsigned heapIndex, unsigned begin, unsigned end, bool allocatedMode)
    {
        const unsigned currentFrameId = GetFrameID();
        for (std::vector<WarmSpan>::const_iterator i=_warmSpans.begin(); i!=_warmSpans.end(); ++i) {
            if (i->_heapIndex == heapIndex && i->_begin == begin && i->_end == end) {
                return 1.f-std::min((currentFrameId-i->_frameStart)/float(FramesOfWarmth), 1.f);
            }
        }

        const bool thereLastFrame = FindSpan(heapIndex, begin, end, allocatedMode);
        if (!thereLastFrame) {
            WarmSpan warmSpan;
            warmSpan._heapIndex = heapIndex;
            warmSpan._begin = begin;
            warmSpan._end = end;
            warmSpan._frameStart = currentFrameId;
            _warmSpans.push_back(warmSpan);
            return 1.f;
        }

        return 0.f;
    }

    bool BatchingDisplay::FindSpan(unsigned heapIndex, unsigned begin, unsigned end, bool allocatedMode)
    {
        if (heapIndex >= _lastFrameMetrics._heaps.size()) {
            return false;
        }

        unsigned lastStart = 0;
        for (std::vector<unsigned>::const_iterator i2=_lastFrameMetrics._heaps[heapIndex]._markers.begin(); (i2+1)<_lastFrameMetrics._heaps[heapIndex]._markers.end(); i2+=2) {
            unsigned spanBegin, spanEnd;
            if (allocatedMode) {
                spanBegin = lastStart;
                spanEnd = *i2;
            } else {
                spanBegin = *i2;
                spanEnd = *(i2+1);
            }
            if (begin == spanBegin && end == spanEnd) {
                return true;
            }
            lastStart = *(i2+1);
        }
        return false;
    }

    auto BatchingDisplay::ProcessInput(InterfaceState& interfaceState, const InputSnapshot& input) -> ProcessInputResult
    {
        return ProcessInputResult::Passthrough;
    }

    BatchingDisplay::BatchingDisplay(std::shared_ptr<BufferUploads::BatchedResources> batchedResources)
    : _batchedResources(std::move(batchedResources)) {}
    BatchingDisplay::~BatchingDisplay() = default;
}}




