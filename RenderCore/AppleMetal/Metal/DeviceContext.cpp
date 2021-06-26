// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "DeviceContext.h"
#include "State.h"
#include "Shader.h"
#include "InputLayout.h"
#include "Buffer.h"
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

    class GraphicsPipelineBuilder::Pimpl
    {
    public:
        OCPtr<MTLRenderPipelineDescriptor> _pipelineDescriptor; // For the current draw
        AttachmentBlendDesc _attachmentBlendDesc;
        MTLPrimitiveType _activePrimitiveType;
        DepthStencilDesc _activeDepthStencilDesc;
        OCPtr<MTLVertexDescriptor> _vertexDescriptor;

        uint32_t _shaderGuid = 0;
        uint64_t _rpHash = 0;
        uint64_t _inputLayoutGuid = 0;
        uint64_t _absHash = 0, _dssHash = 0;

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
        _pimpl->_activePrimitiveType = AsMTLenum(topology);
    }

    void GraphicsPipelineBuilder::Bind(const DepthStencilDesc& desc)
    {
        // Enabling depth write but disabling depth test doesn't really make sense,
        // and has different behavior among graphics APIs.
        assert(desc._depthTest != CompareOp::Always || !desc._depthWrite);

        _pimpl->_activeDepthStencilDesc = desc;
        _pimpl->_dssHash = _pimpl->_activeDepthStencilDesc.Hash();
    }

    OCPtr<NSObject<MTLDepthStencilState>> GraphicsPipelineBuilder::CreateDepthStencilState(ObjectFactory& factory)
    {
        auto depthAttachmentPixelFormat = [_pimpl->_pipelineDescriptor depthAttachmentPixelFormat];
        // METAL_TODO: Should this account for things like MTLPixelFormatStencil8 (which have stencil but no depth) or MTLPixelFormatX24_Stencil8 (that have 24 bits set aside for, but not used for, depth)? I've never seen us use those formats with a depth buffer (unlike Invalid), but that's no guarantee that we can't ever do so.
        bool hasDepthAttachment = depthAttachmentPixelFormat != MTLPixelFormatInvalid;

        auto& desc = _pimpl->_activeDepthStencilDesc;
        // METAL_TODO: Is this the right place to check this, or should we have discovered this earlier, while creating _activeDepthStencilDesc?
        if (!hasDepthAttachment) {
            if (desc._depthWrite) {
                NSLog(@"CreateDepthStencilState: _depthWrite true when depthAttachmentPixelFormat is %d", (int)depthAttachmentPixelFormat);
                desc._depthWrite = false;
            }
            if (desc._depthTest > CompareOp::Never && desc._depthTest < CompareOp::Always) {
                NSLog(@"CreateDepthStencilState: _depthTest %d when depthAttachmentPixelFormat is %d", (int)desc._depthTest, (int)depthAttachmentPixelFormat);
                desc._depthTest = CompareOp::Always;
            }
        }

        OCPtr<MTLDepthStencilDescriptor> mtlDesc = moveptr([[MTLDepthStencilDescriptor alloc] init]);
        mtlDesc.get().depthCompareFunction = AsMTLCompareFunction(desc._depthTest);
        mtlDesc.get().depthWriteEnabled = desc._depthWrite;

        OCPtr<MTLStencilDescriptor> frontStencilDesc = moveptr([[MTLStencilDescriptor alloc] init]);
        frontStencilDesc.get().stencilCompareFunction = AsMTLCompareFunction(desc._frontFaceStencil._comparisonOp);
        frontStencilDesc.get().stencilFailureOperation = AsMTLStencilOperation(desc._frontFaceStencil._failOp);
        frontStencilDesc.get().depthFailureOperation = AsMTLStencilOperation(desc._frontFaceStencil._depthFailOp);
        frontStencilDesc.get().depthStencilPassOperation = AsMTLStencilOperation(desc._frontFaceStencil._passOp);
        frontStencilDesc.get().readMask = desc._stencilReadMask;
        frontStencilDesc.get().writeMask = desc._stencilWriteMask;
        mtlDesc.get().frontFaceStencil = frontStencilDesc;

        OCPtr<MTLStencilDescriptor> backStencilDesc = moveptr([[MTLStencilDescriptor alloc] init]);
        backStencilDesc.get().stencilCompareFunction = AsMTLCompareFunction(desc._backFaceStencil._comparisonOp);
        backStencilDesc.get().stencilFailureOperation = AsMTLStencilOperation(desc._backFaceStencil._failOp);
        backStencilDesc.get().depthFailureOperation = AsMTLStencilOperation(desc._backFaceStencil._depthFailOp);
        backStencilDesc.get().depthStencilPassOperation = AsMTLStencilOperation(desc._backFaceStencil._passOp);
        backStencilDesc.get().readMask = desc._stencilReadMask;
        backStencilDesc.get().writeMask = desc._stencilWriteMask;
        mtlDesc.get().backFaceStencil = backStencilDesc;

        return factory.CreateDepthStencilState(mtlDesc.get());
    }

    void GraphicsPipelineBuilder::Bind(const RasterizationDesc& desc)
    {
        _cullMode = (unsigned)AsMTLenum(desc._cullMode);
        _faceWinding = (unsigned)AsMTLenum(desc._frontFaceWinding);
    }

    const std::shared_ptr<GraphicsPipeline>& GraphicsPipelineBuilder::CreatePipeline(ObjectFactory& factory)
    {
        unsigned cullMode = _cullMode;
        unsigned faceWinding = _faceWinding;
        
        auto hash = HashCombine(_pimpl->_shaderGuid, _pimpl->_rpHash);
        hash = HashCombine(_pimpl->_absHash, hash);
        hash = HashCombine(_pimpl->_dssHash, hash);
        hash = HashCombine(_pimpl->_inputLayoutGuid, hash);
        hash = HashCombine(cullMode |
                            (faceWinding << 2) |
                            (_pimpl->_activePrimitiveType << 3), hash);

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

        auto dss = CreateDepthStencilState(factory);
        auto result  = std::make_shared<GraphicsPipeline>(
            std::move(renderPipelineState._renderPipelineState),
            std::move(renderPipelineState._reflection),
            std::move(dss),
            (unsigned)_pimpl->_activePrimitiveType,
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
        _pimpl->_activePrimitiveType = MTLPrimitiveTypeTriangle;
        _dirty = true;
    }

    GraphicsPipelineBuilder::~GraphicsPipelineBuilder()
    {
    }

////////////////////////////////////////////////////////////////////////////////////////////////////

    class DeviceContext::Pimpl
    {
    public:
        // This should always exist. In a device context for an immediate
        // thread context, we'll be given a command buffer at startup, and
        // each time we release one (in Present or CommitCommands) we get
        // a new one instantly. And the only other way to create a device
        // context is with a command buffer that you had lying around.
        IdPtr _commandBuffer; // For the duration of the frame

        // Only one encoder (of either type) can exist, not both. Within a
        // render pass, each subpass corresponds with one render encoder.
        // Outside of render passes, encoders should only be created, used,
        // and immediately destroyed, e.g., in a On... callback.
        IdPtr _commandEncoder; // For the current subpass
        IdPtr _blitCommandEncoder;

        bool _inRenderPass;

        float _renderTargetWidth;
        float _renderTargetHeight;

        std::weak_ptr<IDevice> _device;

        class QueuedUniformSet
        {
        public:
            std::shared_ptr<UnboundInterface> _unboundInterf;
            unsigned _streamIdx;

            std::vector<ConstantBufferView> _constantBuffers;
            std::vector<const ShaderResourceView*> _resources;
            std::vector<const SamplerState*> _samplers;
        };
        std::vector<QueuedUniformSet> _queuedUniformSets;

        CapturedStates _capturedStates;

        IdPtr _activeIndexBuffer; // MTLBuffer

        MTLIndexType _indexType;
        unsigned _indexFormatBytes;
        unsigned _indexBufferOffsetBytes;

        OCPtr<MTLRenderPipelineReflection> _graphicsPipelineReflection;
        uint64_t _boundVSArgs = 0ull, _boundPSArgs = 0ull;

        const GraphicsPipeline* _boundGraphicsPipeline = nullptr;

        NSThread* _boundThread = nullptr;

        std::vector<std::function<void(void)>> _onEndRenderPassFunctions;
        std::vector<std::function<void(void)>> _onEndEncodingFunctions;
        std::vector<std::function<void(void)>> _onDestroyEncoderFunctions;

        unsigned offsetToStartIndex(unsigned startIndex) {
            return startIndex * _indexFormatBytes + _indexBufferOffsetBytes;
        }
    };

    void DeviceContext::Bind(const IndexBufferView& IB)
    {
        assert(_pimpl->_boundThread == [NSThread currentThread]);
        auto resource = AsResource(IB._resource);
        id<MTLBuffer> buffer = resource.GetBuffer();
        if (!buffer)
            Throw(::Exceptions::BasicLabel("Attempting to bind index buffer view with invalid resource"));
        _pimpl->_activeIndexBuffer = buffer;
        _pimpl->_indexType = AsMTLIndexType(IB._indexFormat);
        _pimpl->_indexFormatBytes = BitsPerPixel(IB._indexFormat) / 8;
        _pimpl->_indexBufferOffsetBytes = IB._offset;
    }

    void DeviceContext::BindVS(id<MTLBuffer> buffer, unsigned offset, unsigned bufferIndex)
    {
        assert(_pimpl->_boundThread == [NSThread currentThread]);
        assert(_pimpl->_commandEncoder);
        [_pimpl->_commandEncoder setVertexBuffer:buffer offset:offset atIndex:bufferIndex];
    }

    void DeviceContext::Bind(IteratorRange<const ViewportDesc*> viewports, IteratorRange<const ScissorRect*> scissorRects)
    {
        assert(_pimpl->_boundThread == [NSThread currentThread]);
        assert(_pimpl->_commandEncoder);

        assert(viewports.size() == scissorRects.size() || scissorRects.size() == 0);
        // For now, we only support one viewport and scissor rect; in the future, we could support more
        assert(viewports.size() == 1);

        if (_pimpl->_commandEncoder) {
            const auto& viewport = viewports[0];
            [_pimpl->_commandEncoder setViewport:AsMTLViewport(viewport, _pimpl->_renderTargetWidth, _pimpl->_renderTargetHeight)];
            if (scissorRects.size()) {
                const auto& scissorRect = scissorRects[0];
                if (scissorRect._width == 0 || scissorRect._height == 0) {
                    Throw(::Exceptions::BasicLabel("Scissor rect width (%d) and height (%d) must be non-zero", scissorRect._width, scissorRect._height));
                }
                MTLScissorRect s = AsMTLScissorRect(scissorRect, _pimpl->_renderTargetWidth, _pimpl->_renderTargetHeight);

                // The size of s will be zero if the input ScissorRect contained no valid on-screen area.
                if (s.width == 0 || s.height == 0) {
                    return;
                }
                [_pimpl->_commandEncoder setScissorRect:s];
            } else {
                // If a scissor rect is not specified, use the full size of the render target
                [_pimpl->_commandEncoder setScissorRect:MTLScissorRect{0, 0, (NSUInteger)_pimpl->_renderTargetWidth, (NSUInteger)_pimpl->_renderTargetHeight}];
            }
        }
    }

    void DeviceContext::FinalizePipeline()
    {
        assert(_pimpl->_boundThread == [NSThread currentThread]);

        if (GraphicsPipelineBuilder::IsPipelineStale() || !_pimpl->_graphicsPipelineReflection) {
            auto& pipelineState = *GraphicsPipelineBuilder::CreatePipeline(GetObjectFactory());

            [_pimpl->_commandEncoder setRenderPipelineState:pipelineState._underlying];

            [_pimpl->_commandEncoder setCullMode:(MTLCullMode)pipelineState._cullMode];
            [_pimpl->_commandEncoder setFrontFacingWinding:(MTLWinding)pipelineState._faceWinding];
            [_pimpl->_commandEncoder setDepthStencilState:pipelineState._depthStencilState];
            [_pimpl->_commandEncoder setStencilReferenceValue:pipelineState._stencilReferenceValue];

            _pimpl->_graphicsPipelineReflection = pipelineState._reflection;
            _pimpl->_boundVSArgs = 0;
            _pimpl->_boundPSArgs = 0;
        }

        uint64_t boundVSArgs = 0, boundPSArgs = 0;
        for (const auto&qus:_pimpl->_queuedUniformSets) {
            UniformsStream stream {
                MakeIteratorRange(qus._constantBuffers),
                MakeIteratorRange(qus._resources).Cast<const void*const*>(),
                MakeIteratorRange(qus._samplers).Cast<const void*const*>()
            };
            auto bound = BoundUniforms::Apply_UnboundInterfacePath(*this, _pimpl->_graphicsPipelineReflection.get(), *qus._unboundInterf, qus._streamIdx, stream);
            assert((boundVSArgs & bound._vsArguments) == 0);
            assert((boundPSArgs & bound._psArguments) == 0);
            boundVSArgs |= bound._vsArguments;
            boundPSArgs |= bound._psArguments;
        }
        _pimpl->_boundVSArgs |= boundVSArgs;
        _pimpl->_boundPSArgs |= boundPSArgs;
        _pimpl->_boundGraphicsPipeline = nullptr;
        _pimpl->_queuedUniformSets.clear();

        // Bind standins for anything that have never been bound to anything correctly
        BoundUniforms::Apply_Standins(*this, _pimpl->_graphicsPipelineReflection.get(), ~_pimpl->_boundVSArgs, ~_pimpl->_boundPSArgs);
    }

    void DeviceContext::QueueUniformSet(
        const std::shared_ptr<UnboundInterface>& unboundInterf,
        unsigned streamIdx,
        const UniformsStream& stream)
    {
        assert(_pimpl->_boundThread == [NSThread currentThread]);

        Pimpl::QueuedUniformSet qus;
        qus._unboundInterf = unboundInterf;
        qus._streamIdx = streamIdx;
        qus._constantBuffers = std::vector<ConstantBufferView>{stream._constantBuffers.begin(), stream._constantBuffers.end()};
        qus._resources = std::vector<const ShaderResourceView*>{(const ShaderResourceView*const*)stream._resources.begin(), (const ShaderResourceView*const*)stream._resources.end()};
        qus._samplers = std::vector<const SamplerState*>{(const SamplerState*const*)stream._samplers.begin(), (const SamplerState*const*)stream._samplers.end()};

        for (auto& q:_pimpl->_queuedUniformSets)
            if (q._streamIdx == streamIdx) {
                q = std::move(qus);
                return;
            }
        _pimpl->_queuedUniformSets.emplace_back(std::move(qus));
    }

    void DeviceContext::Draw(unsigned vertexCount, unsigned startVertexLocation)
    {
        assert(_pimpl->_boundThread == [NSThread currentThread]);
        assert(_pimpl->_commandEncoder);

        FinalizePipeline();
        [_pimpl->_commandEncoder drawPrimitives:GraphicsPipelineBuilder::_pimpl->_activePrimitiveType
                                    vertexStart:startVertexLocation
                                    vertexCount:vertexCount];
    }

    void DeviceContext::DrawIndexed(unsigned indexCount, unsigned startIndexLocation, unsigned baseVertexLocation)
    {
        assert(baseVertexLocation==0);
        assert(_pimpl->_boundThread == [NSThread currentThread]);
        assert(_pimpl->_commandEncoder);

        FinalizePipeline();
        [_pimpl->_commandEncoder drawIndexedPrimitives:GraphicsPipelineBuilder::_pimpl->_activePrimitiveType
                                            indexCount:indexCount
                                             indexType:_pimpl->_indexType
                                           indexBuffer:_pimpl->_activeIndexBuffer
                                     indexBufferOffset:_pimpl->offsetToStartIndex(startIndexLocation)];
    }

    void DeviceContext::DrawInstances(unsigned vertexCount, unsigned instanceCount, unsigned startVertexLocation)
    {
        assert(_pimpl->_boundThread == [NSThread currentThread]);
        assert(_pimpl->_commandEncoder);

        FinalizePipeline();
        [_pimpl->_commandEncoder drawPrimitives:GraphicsPipelineBuilder::_pimpl->_activePrimitiveType
                                    vertexStart:startVertexLocation
                                    vertexCount:vertexCount
                                  instanceCount:instanceCount];
    }

    void DeviceContext::DrawIndexedInstances(unsigned indexCount, unsigned instanceCount, unsigned startIndexLocation, unsigned baseVertexLocation)
    {
        assert(_pimpl->_boundThread == [NSThread currentThread]);
        assert(baseVertexLocation==0);

        FinalizePipeline();
        assert(_pimpl->_commandEncoder);

        [_pimpl->_commandEncoder drawIndexedPrimitives:GraphicsPipelineBuilder::_pimpl->_activePrimitiveType
                                            indexCount:indexCount
                                             indexType:_pimpl->_indexType
                                           indexBuffer:_pimpl->_activeIndexBuffer
                                     indexBufferOffset:_pimpl->offsetToStartIndex(startIndexLocation)
                                         instanceCount:instanceCount];
    }

    void    DeviceContext::Draw(
        const GraphicsPipeline& pipeline,
        unsigned vertexCount, unsigned startVertexLocation)
    {
        assert(_pimpl->_boundThread == [NSThread currentThread]);
        assert(_pimpl->_commandEncoder);
        if (_pimpl->_boundGraphicsPipeline != &pipeline) {
            [_pimpl->_commandEncoder setRenderPipelineState:pipeline._underlying];
            [_pimpl->_commandEncoder setCullMode:(MTLCullMode)pipeline._cullMode];
            [_pimpl->_commandEncoder setFrontFacingWinding:(MTLWinding)pipeline._faceWinding];
            [_pimpl->_commandEncoder setDepthStencilState:pipeline._depthStencilState];
            [_pimpl->_commandEncoder setStencilReferenceValue:pipeline._stencilReferenceValue];

            _pimpl->_graphicsPipelineReflection = nullptr;
            _pimpl->_boundVSArgs = 0;
            _pimpl->_boundPSArgs = 0;
            _pimpl->_boundGraphicsPipeline = &pipeline;
            _pimpl->_queuedUniformSets.clear();
        }

        [_pimpl->_commandEncoder drawPrimitives:(MTLPrimitiveType)pipeline._primitiveType
                                    vertexStart:startVertexLocation
                                    vertexCount:vertexCount];
    }

    void    DeviceContext::DrawIndexed(
        const GraphicsPipeline& pipeline,
        unsigned indexCount, unsigned startIndexLocation)
    {
        assert(_pimpl->_boundThread == [NSThread currentThread]);
        assert(_pimpl->_commandEncoder);
        if (_pimpl->_boundGraphicsPipeline != &pipeline) {
            [_pimpl->_commandEncoder setRenderPipelineState:pipeline._underlying];
            [_pimpl->_commandEncoder setCullMode:(MTLCullMode)pipeline._cullMode];
            [_pimpl->_commandEncoder setFrontFacingWinding:(MTLWinding)pipeline._faceWinding];
            [_pimpl->_commandEncoder setDepthStencilState:pipeline._depthStencilState];
            [_pimpl->_commandEncoder setStencilReferenceValue:pipeline._stencilReferenceValue];

            _pimpl->_graphicsPipelineReflection = nullptr;
            _pimpl->_boundVSArgs = 0;
            _pimpl->_boundPSArgs = 0;
            _pimpl->_boundGraphicsPipeline = &pipeline;
            _pimpl->_queuedUniformSets.clear();
        }

        [_pimpl->_commandEncoder drawIndexedPrimitives:(MTLPrimitiveType)pipeline._primitiveType
                                            indexCount:indexCount
                                             indexType:_pimpl->_indexType
                                           indexBuffer:_pimpl->_activeIndexBuffer
                                     indexBufferOffset:_pimpl->offsetToStartIndex(startIndexLocation)];
    }

    void    DeviceContext::DrawInstances(
        const GraphicsPipeline& pipeline,
        unsigned vertexCount, unsigned instanceCount, unsigned startVertexLocation)
    {
        assert(_pimpl->_boundThread == [NSThread currentThread]);
        assert(_pimpl->_commandEncoder);
        if (_pimpl->_boundGraphicsPipeline != &pipeline) {
            [_pimpl->_commandEncoder setRenderPipelineState:pipeline._underlying];
            [_pimpl->_commandEncoder setCullMode:(MTLCullMode)pipeline._cullMode];
            [_pimpl->_commandEncoder setFrontFacingWinding:(MTLWinding)pipeline._faceWinding];
            [_pimpl->_commandEncoder setDepthStencilState:pipeline._depthStencilState];
            [_pimpl->_commandEncoder setStencilReferenceValue:pipeline._stencilReferenceValue];

            _pimpl->_graphicsPipelineReflection = nullptr;
            _pimpl->_boundVSArgs = 0;
            _pimpl->_boundPSArgs = 0;
            _pimpl->_boundGraphicsPipeline = &pipeline;
            _pimpl->_queuedUniformSets.clear();
        }

        [_pimpl->_commandEncoder drawPrimitives:(MTLPrimitiveType)pipeline._primitiveType
                                    vertexStart:startVertexLocation
                                    vertexCount:vertexCount
                                    instanceCount:instanceCount];
    }

    void    DeviceContext::DrawIndexedInstances(
        const GraphicsPipeline& pipeline,
        unsigned indexCount, unsigned instanceCount, unsigned startIndexLocation)
    {
        assert(_pimpl->_boundThread == [NSThread currentThread]);
        assert(_pimpl->_commandEncoder);
        if (_pimpl->_boundGraphicsPipeline != &pipeline) {
            [_pimpl->_commandEncoder setRenderPipelineState:pipeline._underlying];
            [_pimpl->_commandEncoder setCullMode:(MTLCullMode)pipeline._cullMode];
            [_pimpl->_commandEncoder setFrontFacingWinding:(MTLWinding)pipeline._faceWinding];
            [_pimpl->_commandEncoder setDepthStencilState:pipeline._depthStencilState];
            [_pimpl->_commandEncoder setStencilReferenceValue:pipeline._stencilReferenceValue];

            _pimpl->_graphicsPipelineReflection = nullptr;
            _pimpl->_boundVSArgs = 0;
            _pimpl->_boundPSArgs = 0;
            _pimpl->_boundGraphicsPipeline = &pipeline;
            _pimpl->_queuedUniformSets.clear();
        }

        [_pimpl->_commandEncoder drawIndexedPrimitives:(MTLPrimitiveType)pipeline._primitiveType
                                            indexCount:indexCount
                                             indexType:_pimpl->_indexType
                                           indexBuffer:_pimpl->_activeIndexBuffer
                                     indexBufferOffset:_pimpl->offsetToStartIndex(startIndexLocation)
                                         instanceCount:instanceCount];
    }

    void DeviceContext::BeginRenderPass()
    {
        assert(_pimpl->_boundThread == [NSThread currentThread]);
        assert(!_pimpl->_inRenderPass);
        _pimpl->_inRenderPass = true;
    }

    void DeviceContext::EndRenderPass()
    {
        assert(_pimpl->_boundThread == [NSThread currentThread]);
        assert(_pimpl->_inRenderPass);
        _pimpl->_inRenderPass = false;
        for (auto fn: _pimpl->_onEndRenderPassFunctions) { fn(); }
        _pimpl->_onEndRenderPassFunctions.clear();
    }

    bool DeviceContext::InRenderPass()
    {
        return _pimpl->_inRenderPass;
    }

    void DeviceContext::OnEndRenderPass(std::function<void ()> fn)
    {
        assert(_pimpl->_boundThread == [NSThread currentThread]);
        if (!_pimpl->_inRenderPass) {
            _pimpl->_onEndRenderPassFunctions.push_back(fn);
        } else {
            fn();
        }
    }

    void            DeviceContext::CreateRenderCommandEncoder(MTLRenderPassDescriptor* renderPassDescriptor)
    {
        assert(_pimpl->_boundThread == [NSThread currentThread]);
        CheckCommandBufferError(_pimpl->_commandBuffer);

        assert(!_pimpl->_commandEncoder);
        assert(!_pimpl->_blitCommandEncoder);

        float width = 0.f;
        float height = 0.f;
        if (renderPassDescriptor.colorAttachments[0].texture) {
            width = renderPassDescriptor.colorAttachments[0].texture.width;
            height = renderPassDescriptor.colorAttachments[0].texture.height;
        } else if (renderPassDescriptor.depthAttachment.texture) {
            width = renderPassDescriptor.depthAttachment.texture.width;
            height = renderPassDescriptor.depthAttachment.texture.height;
        } else if (renderPassDescriptor.stencilAttachment.texture) {
            width = renderPassDescriptor.stencilAttachment.texture.width;
            height = renderPassDescriptor.stencilAttachment.texture.height;
        }
        _pimpl->_renderTargetWidth = width;
        _pimpl->_renderTargetHeight = height;
        _pimpl->_commandEncoder = [_pimpl->_commandBuffer renderCommandEncoderWithDescriptor:renderPassDescriptor];
        assert(_pimpl->_commandEncoder);

        _pimpl->_boundVSArgs = 0;
        _pimpl->_boundPSArgs = 0;
        _pimpl->_graphicsPipelineReflection = nullptr;
        _pimpl->_queuedUniformSets.clear();
    }

    void            DeviceContext::CreateBlitCommandEncoder()
    {
        CheckCommandBufferError(_pimpl->_commandBuffer);
        assert(!_pimpl->_commandEncoder);
        assert(!_pimpl->_blitCommandEncoder);
        _pimpl->_blitCommandEncoder = [_pimpl->_commandBuffer blitCommandEncoder];
        assert(_pimpl->_blitCommandEncoder);
    }

    void            DeviceContext::EndEncoding()
    {
        assert(_pimpl->_boundThread == [NSThread currentThread]);
        assert(_pimpl->_commandEncoder || _pimpl->_blitCommandEncoder);

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

    void            DeviceContext::DestroyRenderCommandEncoder()
    {
        assert(_pimpl->_boundThread == [NSThread currentThread]);
        assert(_pimpl->_commandEncoder);
        _pimpl->_commandEncoder = nullptr;
        _pimpl->_renderTargetWidth = 0.f;
        _pimpl->_renderTargetHeight = 0.f;

        for (auto fn: _pimpl->_onDestroyEncoderFunctions) { fn(); }
        _pimpl->_onDestroyEncoderFunctions.clear();
    }

    void            DeviceContext::DestroyBlitCommandEncoder()
    {
        assert(_pimpl->_boundThread == [NSThread currentThread]);
        assert(_pimpl->_blitCommandEncoder);
        _pimpl->_blitCommandEncoder = nullptr;

        for (auto fn: _pimpl->_onDestroyEncoderFunctions) { fn(); }
        _pimpl->_onDestroyEncoderFunctions.clear();
    }

    bool DeviceContext::HasEncoder()
    {
        return HasRenderCommandEncoder() || HasBlitCommandEncoder();
    }

    bool DeviceContext::HasRenderCommandEncoder()
    {
        return _pimpl->_commandEncoder;
    }

    bool DeviceContext::HasBlitCommandEncoder()
    {
        return _pimpl->_blitCommandEncoder;
    }

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

    void            DeviceContext::HoldCommandBuffer(id<MTLCommandBuffer> commandBuffer)
    {
        /* Hold for the duration of the frame */
        assert(_pimpl->_boundThread == [NSThread currentThread]);
        assert(!_pimpl->_commandBuffer);
        _pimpl->_commandBuffer = commandBuffer;

        CheckCommandBufferError(_pimpl->_commandBuffer);
    }

    void            DeviceContext::ReleaseCommandBuffer()
    {
        assert(_pimpl->_boundThread == [NSThread currentThread]);
        CheckCommandBufferError(_pimpl->_commandBuffer);

        /* The command encoder should have been released when the subpass was finished,
         * now we release the command buffer */
        assert(!_pimpl->_commandEncoder && !_pimpl->_blitCommandEncoder);
        assert(_pimpl->_commandBuffer);
        _pimpl->_commandBuffer = nullptr;
    }

    id<MTLCommandBuffer>            DeviceContext::RetrieveCommandBuffer()
    {
        assert(_pimpl->_boundThread == [NSThread currentThread]);
        return _pimpl->_commandBuffer;
    }

    void            DeviceContext::PushDebugGroup(const char annotationName[])
    {
        assert(_pimpl->_boundThread == [NSThread currentThread]);
        // assert(_pimpl->_commandEncoder);
        [_pimpl->_commandEncoder pushDebugGroup:[NSString stringWithCString:annotationName encoding:NSUTF8StringEncoding]];
    }

    void            DeviceContext::PopDebugGroup()
    {
        assert(_pimpl->_boundThread == [NSThread currentThread]);
        // assert(_pimpl->_commandEncoder);
        [_pimpl->_commandEncoder popDebugGroup];
    }

    CapturedStates* DeviceContext::GetCapturedStates() { return &_pimpl->_capturedStates; }
    void        DeviceContext::BeginStateCapture(CapturedStates& capturedStates) {}
    void        DeviceContext::EndStateCapture() {}

    std::shared_ptr<IDevice> DeviceContext::GetDevice()
    {
        return _pimpl->_device.lock();
    }

    DeviceContext::DeviceContext(std::shared_ptr<IDevice> device)
    {
        _pimpl = std::make_unique<Pimpl>();
        _pimpl->_indexType = MTLIndexTypeUInt16;
        _pimpl->_indexFormatBytes = 2; // two bytes for MTLIndexTypeUInt16
        _pimpl->_indexBufferOffsetBytes = 0;
        _pimpl->_inRenderPass = false;
        _pimpl->_renderTargetWidth = 0.f;
        _pimpl->_renderTargetHeight = 0.f;
        _pimpl->_boundThread = [NSThread currentThread];
        _pimpl->_device = device;
    }

    DeviceContext::~DeviceContext()
    {
    }

    void                            DeviceContext::BeginCommandList()
    {   
    }

    std::shared_ptr<CommandList>         DeviceContext::ResolveCommandList()
    {
        return std::shared_ptr<CommandList>();
    }

    void                            DeviceContext::CommitCommandList(CommandList& commandList)
    {
    }

    const std::shared_ptr<DeviceContext>& DeviceContext::Get(IThreadContext& threadContext)
    {
        static std::shared_ptr<DeviceContext> dummy;
        auto* tc = (IThreadContextAppleMetal*)threadContext.QueryInterface(typeid(IThreadContextAppleMetal).hash_code());
        if (tc) return tc->GetDeviceContext();
        return dummy;
    }
}}
