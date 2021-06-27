// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "DeviceContext.h"
#include "State.h"
#include "Shader.h"
#include "InputLayout.h"
#include "FrameBuffer.h"
#include "Format.h"
#include "BasicLabelWithNSError.h"
#include "../IDeviceAppleMetal.h"
#include "../../IThreadContext.h"
#include "../../Types.h"
#include "../../FrameBufferDesc.h"
#include "../../../OSServices/Log.h"
#include "../../../OSServices/LogUtil.h"
#include "../../../Utility/OCUtils.h"
#include "../../../Utility/MemoryUtils.h"
#include <assert.h>
#include <map>
#include <utility>
#include <memory>
#include <typeinfo>
#include "IncludeAppleMetal.h"

namespace RenderCore { namespace Metal_AppleMetal
{
    static const Resource& AsResource(const IResource* rp)
    {
        static Resource dummy;
        auto* res = (Resource*)const_cast<IResource*>(rp)->QueryInterface(typeid(Resource).hash_code());
        if (res)
            return *res;
        return dummy;
    }

    MTLPrimitiveType AsMTLenum(Topology topology)
    {
        switch (topology) {
            case Topology::PointList: return MTLPrimitiveTypePoint;
            case Topology::LineList: return MTLPrimitiveTypeLine;
            case Topology::LineStrip: return MTLPrimitiveTypeLineStrip;
            case Topology::TriangleList: return MTLPrimitiveTypeTriangle;
            case Topology::TriangleStrip: return MTLPrimitiveTypeTriangleStrip;
            default: assert(0); return MTLPrimitiveTypeTriangle;
        }
    }

    MTLCullMode AsMTLenum(CullMode cullMode)
    {
        switch (cullMode) {
            case CullMode::Front: return MTLCullModeFront;
            case CullMode::Back: return MTLCullModeBack;
            case CullMode::None: return MTLCullModeNone;
        }
    }

    MTLWinding AsMTLenum(FaceWinding faceWinding)
    {
        switch (faceWinding) {
            case FaceWinding::CCW: return MTLWindingCounterClockwise;
            case FaceWinding::CW: return MTLWindingClockwise;
            default: assert(0); return MTLWindingCounterClockwise;
        }
    }

    static MTLIndexType AsMTLIndexType(Format idxFormat)
    {
        switch (idxFormat) {
            case Format::R16_UINT: return MTLIndexTypeUInt16;
            case Format::R32_UINT: return MTLIndexTypeUInt32;
            default: assert(0); return MTLIndexTypeUInt16;
        }
    }

    static MTLCompareFunction AsMTLCompareFunction(CompareOp op)
    {
        switch (op) {
            case CompareOp::Never: return MTLCompareFunctionNever;
            case CompareOp::Less: return MTLCompareFunctionLess;
            case CompareOp::Equal: return MTLCompareFunctionEqual;
            case CompareOp::LessEqual: return MTLCompareFunctionLessEqual;
            case CompareOp::Greater: return MTLCompareFunctionGreater;
            case CompareOp::NotEqual: return MTLCompareFunctionNotEqual;
            case CompareOp::GreaterEqual: return MTLCompareFunctionGreaterEqual;
            case CompareOp::Always: return MTLCompareFunctionAlways;
            default: assert(0); return MTLCompareFunctionAlways;
        }
    }

    static MTLStencilOperation AsMTLStencilOperation(StencilOp op)
    {
        switch (op) {
            case StencilOp::Keep: return MTLStencilOperationKeep; // same as StencilOp::DontWrite
            case StencilOp::Zero: return MTLStencilOperationZero;
            case StencilOp::Replace: return MTLStencilOperationReplace;
            case StencilOp::IncreaseSat: return MTLStencilOperationIncrementClamp;
            case StencilOp::DecreaseSat: return MTLStencilOperationDecrementClamp;
            case StencilOp::Invert: return MTLStencilOperationInvert;
            case StencilOp::Increase: return MTLStencilOperationIncrementWrap;
            case StencilOp::Decrease: return MTLStencilOperationDecrementWrap;
            default: assert(0); return MTLStencilOperationKeep;
        }
    }

    static MTLBlendFactor AsMTLBlendFactor(Blend blend)
    {
        switch (blend) {
            case Blend::Zero: return MTLBlendFactorZero;
            case Blend::One: return MTLBlendFactorOne;
            case Blend::SrcColor: return MTLBlendFactorSourceColor;
            case Blend::InvSrcColor: return MTLBlendFactorOneMinusSourceColor;
            case Blend::DestColor: return MTLBlendFactorDestinationColor;
            case Blend::InvDestColor: return MTLBlendFactorOneMinusDestinationColor;
            case Blend::SrcAlpha: return MTLBlendFactorSourceAlpha;
            case Blend::InvSrcAlpha: return MTLBlendFactorOneMinusSourceAlpha;
            case Blend::DestAlpha: return MTLBlendFactorDestinationAlpha;
            case Blend::InvDestAlpha: return MTLBlendFactorOneMinusDestinationAlpha;
            default: assert(0); return MTLBlendFactorOne;
        }
    }

    static MTLBlendOperation AsMTLBlendOperation(BlendOp op)
    {
        switch (op) {
            case BlendOp::Add: return MTLBlendOperationAdd;
            case BlendOp::Subtract: return MTLBlendOperationSubtract;
            case BlendOp::RevSubtract: return MTLBlendOperationReverseSubtract;
            case BlendOp::Min: return MTLBlendOperationMin;
            case BlendOp::Max: return MTLBlendOperationMax;
            default: assert(0); return MTLBlendOperationAdd;
        }
    }

    static MTLViewport AsMTLViewport(const ViewportDesc& viewport, float renderTargetWidth, float renderTargetHeight)
    {
        MTLViewport vp;
        vp.originX = viewport._x;
        vp.originY = viewport._y;
        if (!viewport._originIsUpperLeft) {
            // Metal window coordinate space has origin in upper-left, so we must account for that in the viewport
            vp.originY = renderTargetHeight - viewport._y - viewport._height;
        }
        vp.width = viewport._width;
        vp.height = viewport._height;
        vp.znear = viewport._minDepth;
        vp.zfar = viewport._maxDepth;
        return vp;
    }

    static MTLScissorRect AsMTLScissorRect(const ScissorRect& scissorRect, float renderTargetWidth, float renderTargetHeight)
    {
        int x = scissorRect._x;
        int y = scissorRect._y;
        int width = scissorRect._width;
        int height = scissorRect._height;

        // Metal window coordinate space has origin in upper-left, so we must account for that in the scissor rect
        if (!scissorRect._originIsUpperLeft) {
            y = renderTargetHeight - y - height;
        }

        // Ensure scissor rect lies entirely within render target bounds.
        if (x < 0) {
            width += x;
            x = 0;
        } else if (x > renderTargetWidth) {
            width = 0;
            x = renderTargetWidth;
        }

        if (y < 0) {
            height += y;
            y = 0;
        } else if (y > renderTargetHeight) {
            height = 0;
            y = renderTargetHeight;
        }

        // Clamp size to valid window coordinates
        width = std::max(0, std::min(width, (int)renderTargetWidth - x));
        height = std::max(0, std::min(height, (int)renderTargetHeight - y));

        // Should never be negative numbers at this point, as that will produce wrap-around when casting to unsigned.
        assert(x >= 0 && y >= 0 && width >= 0 && height >= 0);
        return MTLScissorRect { (unsigned)x, (unsigned)y, (unsigned)width, (unsigned)height };
    }

    static void CheckCommandBufferError(id<MTLCommandBuffer> buffer)
    {
        if (buffer.error) {
            Log(Warning) << "================> " << buffer.error << std::endl;
        }
    }

    GraphicsPipeline::GraphicsPipeline(
        OCPtr<NSObject<MTLRenderPipelineState>> underlying,
        OCPtr<MTLRenderPipelineReflection> reflection,
        OCPtr<NSObject<MTLDepthStencilState>> depthStencilState,
        unsigned primitiveType,
        unsigned cullMode,
        unsigned faceWinding,
        uint64_t interfaceBindingGUID)
    : _underlying(std::move(underlying))
    , _reflection(std::move(reflection))
    , _depthStencilState(std::move(depthStencilState))
    , _primitiveType(std::move(primitiveType))
    , _cullMode(std::move(cullMode))
    , _faceWinding(std::move(faceWinding))
    , _interfaceBindingGUID(std::move(interfaceBindingGUID))
    {
    }

    GraphicsPipeline::~GraphicsPipeline() = default;

    class GraphicsPipelineBuilder::Pimpl
    {
    public:
        OCPtr<MTLRenderPipelineDescriptor> _pipelineDescriptor; // For the current draw
        AttachmentBlendDesc _attachmentBlendDesc;
        DepthStencilDesc _activeDepthStencilDesc;
        OCPtr<MTLVertexDescriptor> _vertexDescriptor;
        unsigned _cullMode = 0;
        unsigned _faceWinding = 0;

        uint32_t _shaderGuid = 0;
        uint64_t _rpHash = 0;
        uint64_t _inputLayoutGuid = 0;
        uint64_t _absHash = 0;

        std::map<uint64_t, std::shared_ptr<GraphicsPipeline>> _prebuiltPipelines;

        #if defined(_DEBUG)
            std::string _shaderSourceIdentifiers;
        #endif
    };

    void GraphicsPipelineBuilder::Bind(const ShaderProgram& shaderProgram)
    {
        assert(_pimpl->_pipelineDescriptor);
        [_pimpl->_pipelineDescriptor setVertexFunction:shaderProgram._vf];
        [_pimpl->_pipelineDescriptor setFragmentFunction:shaderProgram._ff];
        _pimpl->_pipelineDescriptor.get().rasterizationEnabled = shaderProgram._ff != nullptr;
        _pimpl->_shaderGuid = shaderProgram.GetGUID();
        _dirty = true;

        #if defined(_DEBUG)
            _pimpl->_shaderSourceIdentifiers = shaderProgram.SourceIdentifiers();
        #endif
    }

    void GraphicsPipelineBuilder::Bind(IteratorRange<const AttachmentBlendDesc*> blendStates)
    {
        assert(blendStates.size() == 1);
        _pimpl->_attachmentBlendDesc = blendStates[0];
        _pimpl->_absHash = _pimpl->_attachmentBlendDesc.Hash();
        _dirty = true;
    }

    void GraphicsPipelineBuilder::SetRenderPassConfiguration(const FrameBufferDesc& fbDesc, unsigned subPass)
    {
        assert(subPass < fbDesc.GetSubpasses().size());
        assert(_pimpl->_pipelineDescriptor);

        auto& subpass = fbDesc.GetSubpasses()[subPass];

        // Derive the sample count directly from the framebuffer properties & the subpass.
        // TODO -- we should also enable specifying the sample count via a MSAA sampling state structure

        unsigned msaaAttachments = 0, singleSampleAttachments = 0;
        for (const auto&a:subpass.GetOutputs()) {
            if (a._window._flags & TextureViewDesc::Flags::ForceSingleSample) {
                ++singleSampleAttachments;
            } else {
                auto& attach = fbDesc.GetAttachments()[a._resourceName];
                if (attach._flags & AttachmentDesc::Flags::Multisampled) {
                    ++msaaAttachments;
                } else {
                    ++singleSampleAttachments;
                }
            }
        }

        unsigned sampleCount = 1;
        if (msaaAttachments == 0) {
            // no msaa attachments,
        } else if (fbDesc.GetProperties()._samples._sampleCount > 1) {
            if (singleSampleAttachments > 0) {
                Log(Warning) << "Subpass has a mixture of MSAA and non-MSAA attachments. MSAA can't be enabled, so falling back to single sample mode" << std::endl;
            } else {
                sampleCount = fbDesc.GetProperties()._samples._sampleCount;
            }
        }

        if ([_pimpl->_pipelineDescriptor.get() respondsToSelector:@selector(setRasterSampleCount:)]) {
            if (sampleCount != _pimpl->_pipelineDescriptor.get().rasterSampleCount) {
                _pimpl->_pipelineDescriptor.get().rasterSampleCount = sampleCount;
                _dirty = true;
            }
        } else {
            // Some drivers don't appear to have the "rasterSampleCount". It appears to be IOS 11+ only.
            // Falling back to the older name "sampleCount" -- documentation in the header suggests
            // they are the same thing
            if (sampleCount != _pimpl->_pipelineDescriptor.get().sampleCount) {
                _pimpl->_pipelineDescriptor.get().sampleCount = sampleCount;
                _dirty = true;
            }
        }

        uint64_t rpHash = sampleCount;

        // Figure out the pixel formats for each of the attachments (including depth/stencil)
        const unsigned maxColorAttachments = 8u;
        for (unsigned i=0; i<maxColorAttachments; ++i) {
            if (i < subpass.GetOutputs().size()) {
                assert(subpass.GetOutputs()[i]._resourceName <= fbDesc.GetAttachments().size());
                const auto& window = subpass.GetOutputs()[i]._window;
                const auto& attachment = fbDesc.GetAttachments()[subpass.GetOutputs()[i]._resourceName];
                auto finalFormat = ResolveFormat(attachment._format, window._format, BindFlag::RenderTarget);
                auto mtlFormat = AsMTLPixelFormat(finalFormat);
                _pimpl->_pipelineDescriptor.get().colorAttachments[i].pixelFormat = mtlFormat;
                rpHash = HashCombine(mtlFormat, rpHash);
            } else {
                _pimpl->_pipelineDescriptor.get().colorAttachments[i].pixelFormat = MTLPixelFormatInvalid;
            }
        }

        _pimpl->_pipelineDescriptor.get().depthAttachmentPixelFormat = MTLPixelFormatInvalid;
        _pimpl->_pipelineDescriptor.get().stencilAttachmentPixelFormat = MTLPixelFormatInvalid;

        if (subpass.GetDepthStencil()._resourceName != SubpassDesc::Unused._resourceName) {
            assert(subpass.GetDepthStencil()._resourceName <= fbDesc.GetAttachments().size());
            const auto& window = subpass.GetDepthStencil()._window;
            const auto& attachment = fbDesc.GetAttachments()[subpass.GetDepthStencil()._resourceName];
            auto finalFormat = ResolveFormat(attachment._format, window._format, BindFlag::DepthStencil);

            auto components = GetComponents(finalFormat);
            auto mtlFormat = AsMTLPixelFormat(finalFormat);
            if (components == FormatComponents::Depth) {
                _pimpl->_pipelineDescriptor.get().depthAttachmentPixelFormat = mtlFormat;
            } else if (components == FormatComponents::Stencil) {
                _pimpl->_pipelineDescriptor.get().stencilAttachmentPixelFormat = mtlFormat;
            } else if (components == FormatComponents::DepthStencil) {
                _pimpl->_pipelineDescriptor.get().depthAttachmentPixelFormat = mtlFormat;
                _pimpl->_pipelineDescriptor.get().stencilAttachmentPixelFormat = mtlFormat;
            } else {
                assert(0);      // format doesn't appear to have either depth or stencil components
            }

            rpHash = HashCombine(mtlFormat, rpHash);
        }

        _dirty = true;
        _pimpl->_rpHash = rpHash;
    }

    uint64_t GraphicsPipelineBuilder::GetRenderPassConfigurationHash() const
    {
        return _pimpl->_rpHash;
    }

    void GraphicsPipelineBuilder::SetRenderPassConfiguration(MTLRenderPassDescriptor* renderPassDescriptor, unsigned sampleCount)
    {
        sampleCount = std::max(sampleCount, 1u);
        if ([_pimpl->_pipelineDescriptor.get() respondsToSelector:@selector(setRasterSampleCount:)]) {
            if (sampleCount != _pimpl->_pipelineDescriptor.get().rasterSampleCount) {
                _pimpl->_pipelineDescriptor.get().rasterSampleCount = sampleCount;
                _dirty = true;
            }
        } else {
            // Some drivers don't appear to have the "rasterSampleCount". It appears to be IOS 11+ only.
            // Falling back to the older name "sampleCount" -- documentation in the header suggests
            // they are the same thing
            if (sampleCount != _pimpl->_pipelineDescriptor.get().sampleCount) {
                _pimpl->_pipelineDescriptor.get().sampleCount = sampleCount;
                _dirty = true;
            }
        }

        uint64_t rpHash = sampleCount;

        const unsigned maxColorAttachments = 8u;
        for (unsigned i=0; i<maxColorAttachments; ++i) {
            MTLRenderPassColorAttachmentDescriptor* renderPassColorAttachmentDesc = renderPassDescriptor.colorAttachments[i];
            if (renderPassColorAttachmentDesc.texture) {
                _pimpl->_pipelineDescriptor.get().colorAttachments[i].pixelFormat = renderPassColorAttachmentDesc.texture.pixelFormat;
                rpHash = HashCombine(renderPassColorAttachmentDesc.texture.pixelFormat, rpHash);
            } else {
                _pimpl->_pipelineDescriptor.get().colorAttachments[i].pixelFormat = MTLPixelFormatInvalid;
            }
        }

        _pimpl->_pipelineDescriptor.get().depthAttachmentPixelFormat = MTLPixelFormatInvalid;
        _pimpl->_pipelineDescriptor.get().stencilAttachmentPixelFormat = MTLPixelFormatInvalid;

        if (renderPassDescriptor.depthAttachment.texture) {
            _pimpl->_pipelineDescriptor.get().depthAttachmentPixelFormat = renderPassDescriptor.depthAttachment.texture.pixelFormat;
        }
        if (renderPassDescriptor.stencilAttachment.texture) {
            _pimpl->_pipelineDescriptor.get().stencilAttachmentPixelFormat = renderPassDescriptor.stencilAttachment.texture.pixelFormat;
        } else if (renderPassDescriptor.depthAttachment.texture) {
            // If the depth texture is a depth/stencil format, we must ensure that both the stencil and depth fields agree
            auto depthFormat = renderPassDescriptor.depthAttachment.texture.pixelFormat;
            if (depthFormat == MTLPixelFormatDepth32Float_Stencil8 ||  depthFormat == MTLPixelFormatX32_Stencil8
                #if PLATFORMOS_TARGET == PLATFORMOS_OSX
                    || depthFormat == MTLPixelFormatDepth24Unorm_Stencil8 || depthFormat == MTLPixelFormatX24_Stencil8
                #endif
                ) {
                _pimpl->_pipelineDescriptor.get().stencilAttachmentPixelFormat = depthFormat;
            }
        }

        if (renderPassDescriptor.depthAttachment.texture) {
            rpHash = HashCombine(renderPassDescriptor.depthAttachment.texture.pixelFormat, rpHash);
        } else if (renderPassDescriptor.stencilAttachment.texture)
            rpHash = HashCombine(renderPassDescriptor.stencilAttachment.texture.pixelFormat, rpHash);

        _dirty = true;
        _pimpl->_rpHash = rpHash;
    }

    void GraphicsPipelineBuilder::Bind(const BoundInputLayout& inputLayout, Topology topology)
    {
        // KenD -- the vertex descriptor isn't necessary if the vertex function does not have an input argument declared [[stage_in]] */
        assert(_pimpl->_pipelineDescriptor);
        auto* descriptor = inputLayout.GetVertexDescriptor();
        if (descriptor != _pimpl->_vertexDescriptor.get()) {
            _pimpl->_vertexDescriptor = descriptor;
            _pimpl->_inputLayoutGuid = inputLayout.GetGUID();
            _dirty = true;
        }
        _activePrimitiveType = AsMTLenum(topology);
    }

    void GraphicsPipelineBuilder::Bind(const DepthStencilDesc& desc)
    {
        // Enabling depth write but disabling depth test doesn't really make sense,
        // and has different behavior among graphics APIs.
        assert(desc._depthTest != CompareOp::Always || !desc._depthWrite);

        _pimpl->_activeDepthStencilDesc = desc;
    }

    OCPtr<NSObject<MTLDepthStencilState>> GraphicsPipelineBuilder::CreateDepthStencilState(ObjectFactory& factory)
    {
        OCPtr<MTLDepthStencilDescriptor> mtlDesc = moveptr([[MTLDepthStencilDescriptor alloc] init]);
        auto& dsDesc = _pimpl->_activeDepthStencilDesc;
        if ([_pimpl->_pipelineDescriptor depthAttachmentPixelFormat] != MTLPixelFormatInvalid) {
            mtlDesc.get().depthCompareFunction = AsMTLCompareFunction(dsDesc._depthTest);
            mtlDesc.get().depthWriteEnabled = dsDesc._depthWrite;
        } else {
            mtlDesc.get().depthCompareFunction = AsMTLCompareFunction(CompareOp::Always);
            mtlDesc.get().depthWriteEnabled = false;
        }

        if ([_pimpl->_pipelineDescriptor stencilAttachmentPixelFormat] != MTLPixelFormatInvalid
            && dsDesc._stencilEnable) {
            OCPtr<MTLStencilDescriptor> frontStencilDesc = moveptr([[MTLStencilDescriptor alloc] init]);
            frontStencilDesc.get().stencilCompareFunction = AsMTLCompareFunction(dsDesc._frontFaceStencil._comparisonOp);
            frontStencilDesc.get().stencilFailureOperation = AsMTLStencilOperation(dsDesc._frontFaceStencil._failOp);
            frontStencilDesc.get().depthFailureOperation = AsMTLStencilOperation(dsDesc._frontFaceStencil._depthFailOp);
            frontStencilDesc.get().depthStencilPassOperation = AsMTLStencilOperation(dsDesc._frontFaceStencil._passOp);
            frontStencilDesc.get().readMask = dsDesc._stencilReadMask;
            frontStencilDesc.get().writeMask = dsDesc._stencilWriteMask;
            mtlDesc.get().frontFaceStencil = frontStencilDesc;

            OCPtr<MTLStencilDescriptor> backStencilDesc = moveptr([[MTLStencilDescriptor alloc] init]);
            backStencilDesc.get().stencilCompareFunction = AsMTLCompareFunction(dsDesc._backFaceStencil._comparisonOp);
            backStencilDesc.get().stencilFailureOperation = AsMTLStencilOperation(dsDesc._backFaceStencil._failOp);
            backStencilDesc.get().depthFailureOperation = AsMTLStencilOperation(dsDesc._backFaceStencil._depthFailOp);
            backStencilDesc.get().depthStencilPassOperation = AsMTLStencilOperation(dsDesc._backFaceStencil._passOp);
            backStencilDesc.get().readMask = dsDesc._stencilReadMask;
            backStencilDesc.get().writeMask = dsDesc._stencilWriteMask;
            mtlDesc.get().backFaceStencil = backStencilDesc;
        }

        return factory.CreateDepthStencilState(mtlDesc.get());
    }

    void GraphicsPipelineBuilder::Bind(const RasterizationDesc& desc)
    {
        _pimpl->_cullMode = (unsigned)AsMTLenum(desc._cullMode);
        _pimpl->_faceWinding = (unsigned)AsMTLenum(desc._frontFaceWinding);
    }

    const std::shared_ptr<GraphicsPipeline>& GraphicsPipelineBuilder::CreatePipeline(ObjectFactory& factory)
    {
        unsigned cullMode = _pimpl->_cullMode;
        unsigned faceWinding = _pimpl->_faceWinding;

        auto dssHash = 0;
        if ([_pimpl->_pipelineDescriptor depthAttachmentPixelFormat] != MTLPixelFormatInvalid)
            dssHash |= _pimpl->_activeDepthStencilDesc.HashDepthAspect();
        if ([_pimpl->_pipelineDescriptor stencilAttachmentPixelFormat] != MTLPixelFormatInvalid)
            dssHash |= _pimpl->_activeDepthStencilDesc.HashStencilAspect();

        auto hash = HashCombine(_pimpl->_shaderGuid, _pimpl->_rpHash);
        hash = HashCombine(_pimpl->_absHash, hash);
        hash = dssHash ? HashCombine(dssHash, hash) : hash;
        hash = HashCombine(_pimpl->_inputLayoutGuid, hash);
        hash = HashCombine(cullMode |
                            (faceWinding << 2) |
                            (_activePrimitiveType << 3), hash);

        auto i = _pimpl->_prebuiltPipelines.find(hash);
        if (i!=_pimpl->_prebuiltPipelines.end())
            return i->second;

        MTLRenderPipelineColorAttachmentDescriptor* colAttachmentZero =
            _pimpl->_pipelineDescriptor.get().colorAttachments[0];
        if (colAttachmentZero.pixelFormat != MTLPixelFormatInvalid) {
            const auto& blendDesc = _pimpl->_attachmentBlendDesc;
            colAttachmentZero.blendingEnabled = blendDesc._blendEnable;

            if (blendDesc._colorBlendOp != BlendOp::NoBlending) {
                colAttachmentZero.rgbBlendOperation = AsMTLBlendOperation(blendDesc._colorBlendOp);
                colAttachmentZero.sourceRGBBlendFactor = AsMTLBlendFactor(blendDesc._srcColorBlendFactor);
                colAttachmentZero.destinationRGBBlendFactor = AsMTLBlendFactor(blendDesc._dstColorBlendFactor);
            } else {
                colAttachmentZero.rgbBlendOperation = MTLBlendOperationAdd;
                colAttachmentZero.sourceRGBBlendFactor = MTLBlendFactorOne;
                colAttachmentZero.destinationRGBBlendFactor = MTLBlendFactorZero;
            }

            if (blendDesc._colorBlendOp != BlendOp::NoBlending) {
                colAttachmentZero.alphaBlendOperation = AsMTLBlendOperation(blendDesc._alphaBlendOp);
                colAttachmentZero.sourceAlphaBlendFactor = AsMTLBlendFactor(blendDesc._srcAlphaBlendFactor);
                colAttachmentZero.destinationAlphaBlendFactor = AsMTLBlendFactor(blendDesc._dstAlphaBlendFactor);
            } else {
                colAttachmentZero.alphaBlendOperation = MTLBlendOperationAdd;
                colAttachmentZero.sourceAlphaBlendFactor = MTLBlendFactorOne;
                colAttachmentZero.destinationAlphaBlendFactor = MTLBlendFactorZero;
            }

            colAttachmentZero.writeMask =
                ((blendDesc._writeMask & ColorWriteMask::Red)    ? MTLColorWriteMaskRed   : 0) |
                ((blendDesc._writeMask & ColorWriteMask::Green)  ? MTLColorWriteMaskGreen : 0) |
                ((blendDesc._writeMask & ColorWriteMask::Blue)   ? MTLColorWriteMaskBlue  : 0) |
                ((blendDesc._writeMask & ColorWriteMask::Alpha)  ? MTLColorWriteMaskAlpha : 0);
        } else {
            colAttachmentZero.blendingEnabled = NO;
        }

        [_pimpl->_pipelineDescriptor setVertexDescriptor:_pimpl->_vertexDescriptor.get()];

        auto renderPipelineState = factory.CreateRenderPipelineState(_pimpl->_pipelineDescriptor.get(), true);

        // DavidJ -- note -- we keep the state _pimpl->_pipelineDescriptor from here.
        //      what happens if we continue to change it? It doesn't impact the compiled state we
        //      just made, right?

        _dirty = false;

        if (!renderPipelineState._renderPipelineState) {
            if (renderPipelineState._error) {
                Log(Error) << "Failed to create render pipeline state: " << renderPipelineState._error.get() << std::endl;
                Throw(BasicLabelWithNSError(renderPipelineState._error, "PipelineState failed with error: %s", renderPipelineState._error.get().description.UTF8String));
            } else {
                Throw(::Exceptions::BasicLabel("PipelineState failed with no error code msg"));
            }
        }

        auto result  = std::make_shared<GraphicsPipeline>(
            std::move(renderPipelineState._renderPipelineState),
            std::move(renderPipelineState._reflection),
            CreateDepthStencilState(factory),
            (unsigned)_activePrimitiveType,
            cullMode,
            faceWinding,
            hash);
        #if defined(_DEBUG)
            result->_shaderSourceIdentifiers = _pimpl->_shaderSourceIdentifiers;
        #endif

        i = _pimpl->_prebuiltPipelines.insert(std::make_pair(hash, result)).first;
        return i->second;
    }

    GraphicsPipelineBuilder::GraphicsPipelineBuilder()
    {
        _pimpl = std::make_unique<Pimpl>();
        _pimpl->_pipelineDescriptor = moveptr([[MTLRenderPipelineDescriptor alloc] init]);
        _activePrimitiveType = MTLPrimitiveTypeTriangle;
        _dirty = true;
    }

    GraphicsPipelineBuilder::~GraphicsPipelineBuilder() = default;
    GraphicsPipelineBuilder& GraphicsPipelineBuilder::operator=(GraphicsPipelineBuilder&&) = default;
    GraphicsPipelineBuilder::GraphicsPipelineBuilder(GraphicsPipelineBuilder&&) = default;

////////////////////////////////////////////////////////////////////////////////////////////////////

    class AppleMetalEncoderSharedState
    {
    public:
        // Only one encoder (of either type) can exist, not both. Within a
        // render pass, each subpass corresponds with one render encoder.
        // Outside of render passes, encoders should only be created, used,
        // and immediately destroyed, e.g., in a On... callback.
        IdPtr _commandEncoder; 
        IdPtr _blitCommandEncoder;

        float _renderTargetWidth = 0.f;
        float _renderTargetHeight = 0.f;

        NSThread* _boundThread = nullptr;

        struct QueuedUniformSet
        {
            std::shared_ptr<UnboundInterface> _unboundInterf;
            unsigned _groupIdx = ~0u;

            std::vector<std::vector<uint8_t>> _immediateDatas;
            std::vector<const IResourceView*> _resources;
            std::vector<const ISampler*> _samplers;
        };
        std::vector<QueuedUniformSet> _queuedUniformSets;
    };

    static unsigned OffsetToStartIndex(unsigned startIndex, unsigned indexFormatBytes, unsigned indexBufferOffsetBytes)
    {
        return startIndex * indexFormatBytes + indexBufferOffsetBytes;
    }

    void GraphicsEncoder::Bind(IteratorRange<const ViewportDesc*> viewports, IteratorRange<const ScissorRect*> scissorRects)
    {
        assert(_sharedState->_boundThread == [NSThread currentThread]);
        assert(_sharedState->_commandEncoder);

        assert(viewports.size() == scissorRects.size() || scissorRects.size() == 0);
        // For now, we only support one viewport and scissor rect; in the future, we could support more
        assert(viewports.size() == 1);

        const auto& viewport = viewports[0];
        [_sharedState->_commandEncoder setViewport:AsMTLViewport(viewport, _sharedState->_renderTargetWidth, _sharedState->_renderTargetHeight)];
        if (scissorRects.size()) {
            const auto& scissorRect = scissorRects[0];
            if (scissorRect._width == 0 || scissorRect._height == 0) {
                Throw(::Exceptions::BasicLabel("Scissor rect width (%d) and height (%d) must be non-zero", scissorRect._width, scissorRect._height));
            }
            MTLScissorRect s = AsMTLScissorRect(scissorRect, _sharedState->_renderTargetWidth, _sharedState->_renderTargetHeight);

            // The size of s will be zero if the input ScissorRect contained no valid on-screen area.
            if (s.width == 0 || s.height == 0) {
                return;
            }
            [_sharedState->_commandEncoder setScissorRect:s];
        } else {
            // If a scissor rect is not specified, use the full size of the render target
            [_sharedState->_commandEncoder setScissorRect:MTLScissorRect{0, 0, (NSUInteger)_sharedState->_renderTargetWidth, (NSUInteger)_sharedState->_renderTargetHeight}];
        }
    }

    void GraphicsEncoder::Bind(IteratorRange<const VertexBufferView*> vbViews, const IndexBufferView& ibView)
    {
        assert(_sharedState->_boundThread == [NSThread currentThread]);
        assert(_sharedState->_commandEncoder);

        if (ibView._resource) {
            auto resource = AsResource(ibView._resource);
            id<MTLBuffer> buffer = resource.GetBuffer();
            if (!buffer)
                Throw(::Exceptions::BasicLabel("Attempting to bind index buffer view with invalid resource"));
            _activeIndexBuffer = buffer;
            _indexType = AsMTLIndexType(ibView._indexFormat);
            _indexFormatBytes = BitsPerPixel(ibView._indexFormat) / 8;
            _indexBufferOffsetBytes = ibView._offset;
        } else {
            _activeIndexBuffer = nullptr;
            _indexType = MTLIndexTypeUInt16;
            _indexFormatBytes = 2;
            _indexBufferOffsetBytes = 0;
        }

        for (unsigned vb=0; vb<vbViews.size(); ++vb)
            [_sharedState->_commandEncoder.get() setVertexBuffer:checked_cast<const Resource*>(vbViews[vb]._resource)->GetBuffer() offset:vbViews[vb]._offset atIndex:vb];
    }

    void GraphicsEncoder::SetStencilRef(unsigned frontFaceStencilRef, unsigned backFaceStencilRef)
    {
        [_sharedState->_commandEncoder setStencilFrontReferenceValue:frontFaceStencilRef
                                                  backReferenceValue:backFaceStencilRef];
    }

    void GraphicsEncoder::PushDebugGroup(const char annotationName[])
    {
        assert(_sharedState->_boundThread == [NSThread currentThread]);
        assert(_sharedState->_commandEncoder);
        [_sharedState->_commandEncoder.get() pushDebugGroup:[NSString stringWithCString:annotationName encoding:NSUTF8StringEncoding]];
    }

    void GraphicsEncoder::PopDebugGroup()
    {
        assert(_sharedState->_boundThread == [NSThread currentThread]);
        assert(_sharedState->_commandEncoder);
        [_sharedState->_commandEncoder.get() popDebugGroup];
    }

    id<MTLRenderCommandEncoder> GraphicsEncoder::GetUnderlying()
    { 
        return (id<MTLRenderCommandEncoder>)_sharedState->_commandEncoder.get();
    }

    void GraphicsEncoder::QueueUniformSet(
        const std::shared_ptr<UnboundInterface>& unboundInterf,
        unsigned groupIdx,
        const UniformsStream& stream)
    {
        assert(_sharedState->_boundThread == [NSThread currentThread]);

        AppleMetalEncoderSharedState::QueuedUniformSet qus;
        qus._unboundInterf = unboundInterf;
        qus._groupIdx = groupIdx;
        qus._resources = {stream._resourceViews.begin(), stream._resourceViews.end()};
        qus._samplers = {stream._samplers.begin(), stream._samplers.end()};
        qus._immediateDatas.reserve(stream._immediateData.size());
        for (const auto& c:stream._immediateData)
            qus._immediateDatas.emplace_back((const uint8_t*)c.begin(), (const uint8_t*)c.end());

        for (auto& q:_sharedState->_queuedUniformSets)
            if (q._groupIdx == groupIdx) {
                q = std::move(qus);
                return;
            }
        _sharedState->_queuedUniformSets.emplace_back(std::move(qus));
    }

    GraphicsEncoder::GraphicsEncoder(
        id<MTLCommandBuffer> cmdBuffer,
        MTLRenderPassDescriptor* renderPassDescriptor,
        std::shared_ptr<AppleMetalEncoderSharedState> sharedState,
        Type type)
    : _type(type)
    , _sharedState(std::move(sharedState))
    {
        assert(!_sharedState->_commandEncoder);
        assert(!_sharedState->_blitCommandEncoder);
        
        _sharedState->_commandEncoder = [cmdBuffer renderCommandEncoderWithDescriptor:renderPassDescriptor];
        assert(_sharedState->_commandEncoder);

        _indexType = MTLIndexTypeUInt16;
        _indexFormatBytes = 2; // two bytes for MTLIndexTypeUInt16
        _indexBufferOffsetBytes = 0;
    }

    GraphicsEncoder::~GraphicsEncoder()
    {
        if (_sharedState) {
            assert(_sharedState->_commandEncoder);
            [_sharedState->_commandEncoder endEncoding];
            _sharedState->_commandEncoder = nil;
        }
    }

    GraphicsEncoder::GraphicsEncoder()
    {
        _indexType = _indexFormatBytes = _indexBufferOffsetBytes = 0;
        _type = Type::Normal;
    }
    GraphicsEncoder::GraphicsEncoder(GraphicsEncoder&& moveFrom) = default;
	GraphicsEncoder& GraphicsEncoder::operator=(GraphicsEncoder&&) = default;

    void    GraphicsEncoder_Optimized::DrawIndexed(
        const GraphicsPipeline& pipeline,
        unsigned indexCount, unsigned startIndexLocation)
    {
        assert(_sharedState->_boundThread == [NSThread currentThread]);
        assert(_sharedState->_commandEncoder);
        assert(_sharedState->_queuedUniformSets.empty());
        if (_boundGraphicsPipeline != &pipeline) {
            [_sharedState->_commandEncoder setRenderPipelineState:pipeline._underlying];
            [_sharedState->_commandEncoder setCullMode:(MTLCullMode)pipeline._cullMode];
            [_sharedState->_commandEncoder setFrontFacingWinding:(MTLWinding)pipeline._faceWinding];
            [_sharedState->_commandEncoder setDepthStencilState:pipeline._depthStencilState];
            _boundGraphicsPipeline = &pipeline;
        }

        [_sharedState->_commandEncoder drawIndexedPrimitives:(MTLPrimitiveType)pipeline._primitiveType
                                                  indexCount:indexCount
                                                   indexType:(MTLIndexType)_indexType
                                                 indexBuffer:_activeIndexBuffer
                                           indexBufferOffset:OffsetToStartIndex(startIndexLocation, _indexFormatBytes, _indexBufferOffsetBytes)];
    }

    void    GraphicsEncoder_Optimized::DrawInstances(
        const GraphicsPipeline& pipeline,
        unsigned vertexCount, unsigned instanceCount, unsigned startVertexLocation)
    {
        assert(_sharedState->_boundThread == [NSThread currentThread]);
        assert(_sharedState->_commandEncoder);
        assert(_sharedState->_queuedUniformSets.empty());
        if (_boundGraphicsPipeline != &pipeline) {
            [_sharedState->_commandEncoder setRenderPipelineState:pipeline._underlying];
            [_sharedState->_commandEncoder setCullMode:(MTLCullMode)pipeline._cullMode];
            [_sharedState->_commandEncoder setFrontFacingWinding:(MTLWinding)pipeline._faceWinding];
            [_sharedState->_commandEncoder setDepthStencilState:pipeline._depthStencilState];
            _boundGraphicsPipeline = &pipeline;
        }

        [_sharedState->_commandEncoder drawPrimitives:(MTLPrimitiveType)pipeline._primitiveType
                                          vertexStart:startVertexLocation
                                          vertexCount:vertexCount
                                        instanceCount:instanceCount];
    }

    void    GraphicsEncoder_Optimized::DrawIndexedInstances(
        const GraphicsPipeline& pipeline,
        unsigned indexCount, unsigned instanceCount, unsigned startIndexLocation)
    {
        assert(_sharedState->_boundThread == [NSThread currentThread]);
        assert(_sharedState->_commandEncoder);
        assert(_sharedState->_queuedUniformSets.empty());
        if (_boundGraphicsPipeline != &pipeline) {
            [_sharedState->_commandEncoder setRenderPipelineState:pipeline._underlying];
            [_sharedState->_commandEncoder setCullMode:(MTLCullMode)pipeline._cullMode];
            [_sharedState->_commandEncoder setFrontFacingWinding:(MTLWinding)pipeline._faceWinding];
            [_sharedState->_commandEncoder setDepthStencilState:pipeline._depthStencilState];
            _boundGraphicsPipeline = &pipeline;
        }

        [_sharedState->_commandEncoder drawIndexedPrimitives:(MTLPrimitiveType)pipeline._primitiveType
                                                  indexCount:indexCount
                                                   indexType:(MTLIndexType)_indexType
                                                 indexBuffer:_activeIndexBuffer
                                           indexBufferOffset:OffsetToStartIndex(startIndexLocation, _indexFormatBytes, _indexBufferOffsetBytes)
                                               instanceCount:instanceCount];
    }

    void    GraphicsEncoder_Optimized::Draw(
        const GraphicsPipeline& pipeline,
        unsigned vertexCount, unsigned startVertexLocation)
    {
        assert(_sharedState->_boundThread == [NSThread currentThread]);
        assert(_sharedState->_commandEncoder);
        if (_boundGraphicsPipeline != &pipeline) {
            [_sharedState->_commandEncoder setRenderPipelineState:pipeline._underlying];
            [_sharedState->_commandEncoder setCullMode:(MTLCullMode)pipeline._cullMode];
            [_sharedState->_commandEncoder setFrontFacingWinding:(MTLWinding)pipeline._faceWinding];
            [_sharedState->_commandEncoder setDepthStencilState:pipeline._depthStencilState];
            _boundGraphicsPipeline = &pipeline;
        }

        [_sharedState->_commandEncoder drawPrimitives:(MTLPrimitiveType)pipeline._primitiveType
                                          vertexStart:startVertexLocation
                                          vertexCount:vertexCount];
    }

    GraphicsEncoder_Optimized::GraphicsEncoder_Optimized(
        id<MTLCommandBuffer> cmdBuffer,
        MTLRenderPassDescriptor* renderPassDescriptor,
        std::shared_ptr<AppleMetalEncoderSharedState> sharedState,
        Type type)
    : GraphicsEncoder(cmdBuffer, renderPassDescriptor, sharedState, type)
    , _boundGraphicsPipeline(nullptr)
    {
    }

    GraphicsEncoder_Optimized::GraphicsEncoder_Optimized(GraphicsEncoder_Optimized&&) = default;
    GraphicsEncoder_Optimized& GraphicsEncoder_Optimized::operator=(GraphicsEncoder_Optimized&&) = default;
    GraphicsEncoder_Optimized::GraphicsEncoder_Optimized() = default;
    GraphicsEncoder_Optimized::~GraphicsEncoder_Optimized() = default;

    void GraphicsEncoder_ProgressivePipeline::FinalizePipeline()
    {
        assert(_sharedState->_boundThread == [NSThread currentThread]);
        assert(_sharedState->_commandEncoder);

        if (GraphicsPipelineBuilder::IsPipelineStale() || !_graphicsPipelineReflection) {
            auto& pipelineState = *GraphicsPipelineBuilder::CreatePipeline(GetObjectFactory());

            [_sharedState->_commandEncoder setRenderPipelineState:pipelineState._underlying];
            [_sharedState->_commandEncoder setCullMode:(MTLCullMode)pipelineState._cullMode];
            [_sharedState->_commandEncoder setFrontFacingWinding:(MTLWinding)pipelineState._faceWinding];
            [_sharedState->_commandEncoder setDepthStencilState:pipelineState._depthStencilState];

            _graphicsPipelineReflection = pipelineState._reflection;
            _boundVSArgs = 0;
            _boundPSArgs = 0;
        }

        uint64_t boundVSArgs = 0, boundPSArgs = 0;
        for (const auto&qus:_sharedState->_queuedUniformSets) {
            IteratorRange<const void*> immData[qus._immediateDatas.size()];
            for (unsigned c=0; c<qus._immediateDatas.size(); ++c)
                immData[c] = qus._immediateDatas[c];
            UniformsStream stream {
                MakeIteratorRange(qus._resources),
                MakeIteratorRange(immData, &immData[qus._immediateDatas.size()]),
                MakeIteratorRange(qus._samplers)
            };
            auto bound = BoundUniforms::Apply_UnboundInterfacePath(*this, _graphicsPipelineReflection.get(), *qus._unboundInterf, qus._groupIdx, stream);
            assert((boundVSArgs & bound._vsArguments) == 0);
            assert((boundPSArgs & bound._psArguments) == 0);
            boundVSArgs |= bound._vsArguments;
            boundPSArgs |= bound._psArguments;
        }
        _boundVSArgs |= boundVSArgs;
        _boundPSArgs |= boundPSArgs;
        _sharedState->_queuedUniformSets.clear();

        // Bind standins for anything that have never been bound to anything correctly
        BoundUniforms::Apply_Standins(*this, _graphicsPipelineReflection.get(), ~_boundVSArgs, ~_boundPSArgs);
    }

    void GraphicsEncoder_ProgressivePipeline::Draw(unsigned vertexCount, unsigned startVertexLocation)
    {
        assert(_sharedState->_boundThread == [NSThread currentThread]);
        assert(_sharedState->_commandEncoder);

        FinalizePipeline();
        [_sharedState->_commandEncoder drawPrimitives:(MTLPrimitiveType)GraphicsPipelineBuilder::_activePrimitiveType
                                          vertexStart:startVertexLocation
                                          vertexCount:vertexCount];
    }

    void GraphicsEncoder_ProgressivePipeline::DrawIndexed(unsigned indexCount, unsigned startIndexLocation)
    {
        assert(_sharedState->_boundThread == [NSThread currentThread]);
        assert(_sharedState->_commandEncoder);

        FinalizePipeline();
        [_sharedState->_commandEncoder drawIndexedPrimitives:(MTLPrimitiveType)GraphicsPipelineBuilder::_activePrimitiveType
                                                  indexCount:indexCount
                                                   indexType:(MTLIndexType)_indexType
                                                 indexBuffer:_activeIndexBuffer
                                           indexBufferOffset:OffsetToStartIndex(startIndexLocation, _indexFormatBytes, _indexBufferOffsetBytes)];
    }

    void GraphicsEncoder_ProgressivePipeline::DrawInstances(unsigned vertexCount, unsigned instanceCount, unsigned startVertexLocation)
    {
        assert(_sharedState->_boundThread == [NSThread currentThread]);
        assert(_sharedState->_commandEncoder);

        FinalizePipeline();
        [_sharedState->_commandEncoder drawPrimitives:(MTLPrimitiveType)GraphicsPipelineBuilder::_activePrimitiveType
                                          vertexStart:startVertexLocation
                                          vertexCount:vertexCount
                                        instanceCount:instanceCount];
    }

    void GraphicsEncoder_ProgressivePipeline::DrawIndexedInstances(unsigned indexCount, unsigned instanceCount, unsigned startIndexLocation)
    {
        assert(_sharedState->_boundThread == [NSThread currentThread]);
        assert(_sharedState->_commandEncoder);

        FinalizePipeline();
        [_sharedState->_commandEncoder drawIndexedPrimitives:(MTLPrimitiveType)GraphicsPipelineBuilder::_activePrimitiveType
                                                  indexCount:indexCount
                                                   indexType:(MTLIndexType)_indexType
                                                 indexBuffer:_activeIndexBuffer
                                           indexBufferOffset:OffsetToStartIndex(startIndexLocation, _indexFormatBytes, _indexBufferOffsetBytes)
                                               instanceCount:instanceCount];
    }

    GraphicsEncoder_ProgressivePipeline::GraphicsEncoder_ProgressivePipeline(
        id<MTLCommandBuffer> cmdBuffer,
        MTLRenderPassDescriptor* renderPassDescriptor,
        unsigned renderPassSampleCount,
        std::shared_ptr<AppleMetalEncoderSharedState> sharedState,
        Type type)
    : GraphicsEncoder(cmdBuffer, renderPassDescriptor, std::move(sharedState), type)
    {
        _graphicsPipelineReflection = nullptr;
        GraphicsPipelineBuilder::SetRenderPassConfiguration(renderPassDescriptor, renderPassSampleCount);
    }

    GraphicsEncoder_ProgressivePipeline::GraphicsEncoder_ProgressivePipeline(GraphicsEncoder_ProgressivePipeline&&) = default;
    GraphicsEncoder_ProgressivePipeline& GraphicsEncoder_ProgressivePipeline::operator=(GraphicsEncoder_ProgressivePipeline&&) = default;
    GraphicsEncoder_ProgressivePipeline::GraphicsEncoder_ProgressivePipeline() = default;
    GraphicsEncoder_ProgressivePipeline::~GraphicsEncoder_ProgressivePipeline() = default;

////////////////////////////////////////////////////////////////////////////////////////////////////

    class DeviceContext::Pimpl
    {
    public:
        std::shared_ptr<AppleMetalEncoderSharedState> _sharedEncoderState;

        // This should always exist. In a device context for an immediate
        // thread context, we'll be given a command buffer at startup, and
        // each time we release one (in Present or CommitCommands) we get
        // a new one instantly. And the only other way to create a device
        // context is with a command buffer that you had lying around.
        IdPtr _commandBuffer; // For the duration of the frame

        OCPtr<MTLRenderPassDescriptor> _renderPassDescriptor;
        unsigned _renderPassSampleCount = 0;
        bool _inRenderPass = false;
        unsigned _nextSubpass = 0;
        std::vector<ClearValue> _renderPassClearValues;

        CapturedStates _capturedStates;

        // We reset some states on the first graphics encoder after beginning a render pass 
        bool _hasPendingResetStates = false;
        ViewportDesc _pendingDefaultViewport;

        // std::vector<std::function<void(void)>> _onEndRenderPassFunctions;
        // std::vector<std::function<void(void)>> _onEndEncodingFunctions;
        // std::vector<std::function<void(void)>> _onDestroyEncoderFunctions;
    };

    void DeviceContext::BeginRenderPass(
        FrameBuffer& frameBuffer,
        IteratorRange<const ClearValue*> clearValues)
    {
        assert(_pimpl->_sharedEncoderState->_boundThread == [NSThread currentThread]);
        assert(!_pimpl->_inRenderPass);
        assert(!_pimpl->_sharedEncoderState->_commandEncoder);
        assert(!_pimpl->_sharedEncoderState->_blitCommandEncoder);
        _pimpl->_inRenderPass = true;
        _pimpl->_nextSubpass = 0;
        _pimpl->_renderPassClearValues.clear();
        _pimpl->_renderPassClearValues.insert(_pimpl->_renderPassClearValues.end(), clearValues.begin(), clearValues.end());
        BeginNextSubpass(frameBuffer);

        _pimpl->_pendingDefaultViewport = frameBuffer.GetDefaultViewport();
        _pimpl->_hasPendingResetStates = true;
    }

    void DeviceContext::BeginNextSubpass(FrameBuffer& frameBuffer)
    {
        assert(_pimpl->_sharedEncoderState->_boundThread == [NSThread currentThread]);
        assert(_pimpl->_inRenderPass);
        assert(!_pimpl->_sharedEncoderState->_commandEncoder);
        assert(!_pimpl->_sharedEncoderState->_blitCommandEncoder);

        // Queue up the next render targets
        auto subpassIndex = _pimpl->_nextSubpass;
        if (subpassIndex < frameBuffer.GetSubpassCount()) {
            auto* descriptor = frameBuffer.GetDescriptor(subpassIndex);
            _pimpl->_renderPassDescriptor = descriptor;
            _pimpl->_renderPassSampleCount = frameBuffer.GetSampleCount(subpassIndex);

            #if 0
                /* Metal TODO -- this is a partial implementation of clear colors; it works for a single color attachment
                * and assumes that depth/stencil clear values are after color attachment clear values, if any */
                unsigned clearValueIterator = 0;
                if (descriptor.colorAttachments[0].texture && descriptor.colorAttachments[0].loadAction == MTLLoadActionClear) {
                    if (clearValueIterator < _pimpl->_renderPassClearValues.size()) {
                        auto* clear = _pimpl->_renderPassClearValues[clearValueIterator]._float;
                        descriptor.colorAttachments[0].clearColor = MTLClearColorMake(clear[0], clear[1], clear[2], clear[3]);
                    } else {
                        descriptor.colorAttachments[0].clearColor = MTLClearColorMake(0.0, 0.0, 0.0, 1.0);
                    }
                }
                if (descriptor.depthAttachment.texture && descriptor.depthAttachment.loadAction == MTLLoadActionClear) {
                    if (clearValueIterator < _pimpl->_renderPassClearValues.size()) {
                        descriptor.depthAttachment.clearDepth = _pimpl->_renderPassClearValues[clearValueIterator]._depthStencil._depth;
                    } else {
                        descriptor.depthAttachment.clearDepth = 1.0f;
                    }
                }
                if (descriptor.stencilAttachment.texture && descriptor.stencilAttachment.loadAction == MTLLoadActionClear) {
                    if (clearValueIterator < _pimpl->_renderPassClearValues.size()) {
                        descriptor.stencilAttachment.clearStencil = _pimpl->_renderPassClearValues[clearValueIterator]._depthStencil._stencil;
                    } else {
                        descriptor.stencilAttachment.clearStencil = 0;
                    }
                }
            #endif

            float width = 0.f;
            float height = 0.f;
            if (descriptor.colorAttachments[0].texture) {
                width = descriptor.colorAttachments[0].texture.width;
                height = descriptor.colorAttachments[0].texture.height;
            } else if (descriptor.depthAttachment.texture) {
                width = descriptor.depthAttachment.texture.width;
                height = descriptor.depthAttachment.texture.height;
            } else if (descriptor.stencilAttachment.texture) {
                width = descriptor.stencilAttachment.texture.width;
                height = descriptor.stencilAttachment.texture.height;
            }
            _pimpl->_sharedEncoderState->_renderTargetWidth = width;
            _pimpl->_sharedEncoderState->_renderTargetHeight = height;
        } else {
            _pimpl->_sharedEncoderState->_renderTargetWidth = 0.f;
            _pimpl->_sharedEncoderState->_renderTargetHeight = 0.f;
        }

        ++_pimpl->_nextSubpass;
    }

    void DeviceContext::EndRenderPass()
    {
        assert(_pimpl->_sharedEncoderState->_boundThread == [NSThread currentThread]);
        assert(_pimpl->_inRenderPass);
        assert(!_pimpl->_sharedEncoderState->_commandEncoder);
        assert(!_pimpl->_sharedEncoderState->_blitCommandEncoder);
        _pimpl->_inRenderPass = false;
        // for (auto fn: _pimpl->_onEndRenderPassFunctions) { fn(); }
        // _pimpl->_onEndRenderPassFunctions.clear();
    }

    bool DeviceContext::IsInRenderPass() const
    {
        return _pimpl->_inRenderPass;
    }

#if 0
    void DeviceContext::OnEndRenderPass(std::function<void ()> fn)
    {
        assert(_pimpl->_boundThread == [NSThread currentThread]);
        if (!_pimpl->_inRenderPass) {
            _pimpl->_onEndRenderPassFunctions.push_back(fn);
        } else {
            fn();
        }
    }
#endif

    GraphicsEncoder_Optimized DeviceContext::BeginGraphicsEncoder(std::shared_ptr<ICompiledPipelineLayout>)
    {
        assert(_pimpl->_sharedEncoderState->_boundThread == [NSThread currentThread]);
        CheckCommandBufferError(_pimpl->_commandBuffer);
        assert(_pimpl->_inRenderPass);
        assert(_pimpl->_renderPassDescriptor);
        _pimpl->_sharedEncoderState->_queuedUniformSets.clear();

        GraphicsEncoder_Optimized result {
            (id<MTLCommandBuffer>)_pimpl->_commandBuffer.get(),
            _pimpl->_renderPassDescriptor,
            _pimpl->_sharedEncoderState,
            GraphicsEncoder::Type::Normal };

        // We reset some states on the first encoder after beginning a render pass
        if (_pimpl->_hasPendingResetStates) {
            ViewportDesc viewports[1] = { _pimpl->_pendingDefaultViewport };
            ScissorRect scissorRects[1];
            scissorRects[0] = ScissorRect{0, 0, (unsigned)viewports[0]._width, (unsigned)viewports[0]._height};
            result.Bind(MakeIteratorRange(viewports), MakeIteratorRange(scissorRects));
            result.SetStencilRef(0,0);
            _pimpl->_hasPendingResetStates = false;
        }

        return result;
    }

    GraphicsEncoder_ProgressivePipeline DeviceContext::BeginGraphicsEncoder_ProgressivePipeline(std::shared_ptr<ICompiledPipelineLayout>)
    {
        assert(_pimpl->_sharedEncoderState->_boundThread == [NSThread currentThread]);
        CheckCommandBufferError(_pimpl->_commandBuffer);
        assert(_pimpl->_inRenderPass);
        assert(_pimpl->_renderPassDescriptor);

        GraphicsEncoder_ProgressivePipeline result {
            (id<MTLCommandBuffer>)_pimpl->_commandBuffer.get(),
            _pimpl->_renderPassDescriptor,
            _pimpl->_renderPassSampleCount,
            _pimpl->_sharedEncoderState,
            GraphicsEncoder::Type::Normal };

        // We reset some states on the first encoder after beginning a render pass
        if (_pimpl->_hasPendingResetStates) {
            ViewportDesc viewports[1] = { _pimpl->_pendingDefaultViewport };
            ScissorRect scissorRects[1];
            scissorRects[0] = ScissorRect{0, 0, (unsigned)viewports[0]._width, (unsigned)viewports[0]._height};
            result.Bind(MakeIteratorRange(viewports), MakeIteratorRange(scissorRects));
            result.SetStencilRef(0,0);
            _pimpl->_hasPendingResetStates = false;
        }

        return result;
    }


#if 0
    void DeviceContext::CreateBlitCommandEncoder()
    {
        CheckCommandBufferError(_pimpl->_commandBuffer);
        assert(!_pimpl->_commandEncoder);
        assert(!_pimpl->_blitCommandEncoder);
        _pimpl->_blitCommandEncoder = [_pimpl->_commandBuffer blitCommandEncoder];
        assert(_pimpl->_blitCommandEncoder);
    }

    void DeviceContext::EndEncoding()
    {
        assert(_pimpl->_sharedEncoderState->_boundThread == [NSThread currentThread]);
        assert(_pimpl->_sharedEncoderState->_commandEncoder || _pimpl->_sharedEncoderState->_blitCommandEncoder);

        if (_pimpl->_commandEncoder) {
            [_pimpl->_commandEncoder endEncoding];
        } else {
            [_pimpl->_blitCommandEncoder endEncoding];
        }

        _pimpl->_graphicsPipelineReflection = nullptr;
        _pimpl->_boundVSArgs = 0;
        _pimpl->_boundPSArgs = 0;
        _pimpl->_boundGraphicsPipeline = nullptr;
        _pimpl->_queuedUniformSets.clear();

        for (auto fn: _pimpl->_onEndEncodingFunctions) { fn(); }
        _pimpl->_onEndEncodingFunctions.clear();
    }
#endif

#if 0
    void            DeviceContext::OnEndEncoding(std::function<void(void)> fn)
    {
        assert(_pimpl->_boundThread == [NSThread currentThread]);
        if (_pimpl->_commandEncoder || _pimpl->_blitCommandEncoder) {
            _pimpl->_onEndEncodingFunctions.push_back(fn);
        } else {
            fn();
        }
    }

    void            DeviceContext::OnDestroyEncoder(std::function<void(void)> fn)
    {
        assert(_pimpl->_boundThread == [NSThread currentThread]);
        if (_pimpl->_commandEncoder || _pimpl->_blitCommandEncoder) {
            _pimpl->_onDestroyEncoderFunctions.push_back(fn);
        } else {
            fn();
        }
    }

    void DeviceContext::DestroyRenderCommandEncoder()
    {
        assert(_pimpl->_sharedEncoderState->_boundThread == [NSThread currentThread]);
        assert(_pimpl->_commandEncoder);
        _pimpl->_commandEncoder = nullptr;
        _pimpl->_renderTargetWidth = 0.f;
        _pimpl->_renderTargetHeight = 0.f;

        for (auto fn: _pimpl->_onDestroyEncoderFunctions) { fn(); }
        _pimpl->_onDestroyEncoderFunctions.clear();
    }

    void DeviceContext::DestroyBlitCommandEncoder()
    {
        assert(_pimpl->_sharedEncoderState->_boundThread == [NSThread currentThread]);
        assert(_pimpl->_blitCommandEncoder);
        _pimpl->_blitCommandEncoder = nullptr;

        for (auto fn: _pimpl->_onDestroyEncoderFunctions) { fn(); }
        _pimpl->_onDestroyEncoderFunctions.clear();
    }
#endif

    bool DeviceContext::HasEncoder()
    {
        return HasRenderCommandEncoder() || HasBlitCommandEncoder();
    }

    bool DeviceContext::HasRenderCommandEncoder()
    {
        return _pimpl->_sharedEncoderState->_commandEncoder;
    }

    bool DeviceContext::HasBlitCommandEncoder()
    {
        return _pimpl->_sharedEncoderState->_blitCommandEncoder;
    }

#if 0
    id<MTLRenderCommandEncoder> DeviceContext::GetCommandEncoder()
    {
        return GetRenderCommandEncoder();
    }

    id<MTLRenderCommandEncoder> DeviceContext::GetRenderCommandEncoder()
    {
        assert(_pimpl->_boundThread == [NSThread currentThread]);
        assert(_pimpl->_commandEncoder);
        return _pimpl->_commandEncoder;
    }

    id<MTLBlitCommandEncoder> DeviceContext::GetBlitCommandEncoder()
    {
        assert(_pimpl->_boundThread == [NSThread currentThread]);
        assert(_pimpl->_blitCommandEncoder);
        return _pimpl->_blitCommandEncoder;
    }
#endif

    void DeviceContext::HoldCommandBuffer(id<MTLCommandBuffer> commandBuffer)
    {
        /* Hold for the duration of the frame */
        assert(_pimpl->_sharedEncoderState->_boundThread == [NSThread currentThread]);
        assert(!_pimpl->_commandBuffer);
        _pimpl->_commandBuffer = commandBuffer;

        CheckCommandBufferError(_pimpl->_commandBuffer);
    }

    void DeviceContext::ReleaseCommandBuffer()
    {
        assert(_pimpl->_sharedEncoderState->_boundThread == [NSThread currentThread]);
        CheckCommandBufferError(_pimpl->_commandBuffer);

        /* The command encoder should have been released when the subpass was finished,
         * now we release the command buffer */
        assert(!_pimpl->_sharedEncoderState->_commandEncoder && !_pimpl->_sharedEncoderState->_blitCommandEncoder);
        assert(_pimpl->_commandBuffer);
        _pimpl->_commandBuffer = nullptr;
    }

    id<MTLCommandBuffer> DeviceContext::RetrieveCommandBuffer()
    {
        assert(_pimpl->_sharedEncoderState->_boundThread == [NSThread currentThread]);
        return _pimpl->_commandBuffer;
    }

    CapturedStates* DeviceContext::GetCapturedStates() { return &_pimpl->_capturedStates; }
    void        DeviceContext::BeginStateCapture(CapturedStates& capturedStates) {}
    void        DeviceContext::EndStateCapture() {}

    DeviceContext::DeviceContext(std::shared_ptr<IDevice> device)
    {
        _pimpl = std::make_unique<Pimpl>();
        _pimpl->_sharedEncoderState = std::make_shared<AppleMetalEncoderSharedState>();
        _pimpl->_inRenderPass = false;
        _pimpl->_sharedEncoderState->_renderTargetWidth = 0.f;
        _pimpl->_sharedEncoderState->_renderTargetHeight = 0.f;
        _pimpl->_sharedEncoderState->_boundThread = [NSThread currentThread];
    }

    DeviceContext::~DeviceContext()
    {
    }

    void DeviceContext::BeginCommandList()
    {   
        assert(0);
    }

    std::shared_ptr<CommandList> DeviceContext::ResolveCommandList()
    {
        assert(0);
        return std::shared_ptr<CommandList>();
    }

    void DeviceContext::CommitCommandList(CommandList& commandList)
    {
        assert(0);
    }

    const std::shared_ptr<DeviceContext>& DeviceContext::Get(IThreadContext& threadContext)
    {
        static std::shared_ptr<DeviceContext> dummy;
        auto* tc = (IThreadContextAppleMetal*)threadContext.QueryInterface(typeid(IThreadContextAppleMetal).hash_code());
        if (tc) return tc->GetDeviceContext();
        return dummy;
    }
}}
