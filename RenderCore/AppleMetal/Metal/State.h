// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../../Types.h"
#include "../../../Core/Exceptions.h"

namespace RenderCore { namespace Metal_AppleMetal
{
    class DeviceContext;

////////////////////////////////////////////////////////////////////////////////////////////////////

    /// Equivalent to MTLStencilDescriptor or D3D12_DEPTH_STENCILOP_DESC or VkStencilOpState
    /// Note that OpenGLES2 & Vulkan allow for separate readmask/writemask/reference values per
    /// face, but DX & Metal do not.
    class StencilDesc
    {
    public:
        StencilOp       _passOp = StencilOp::Keep;        ///< pass stencil & depth tests
        StencilOp       _failOp = StencilOp::Keep;        ///< fail stencil test
        StencilOp       _depthFailOp = StencilOp::Keep;   ///< pass stencil but fail depth tests
        CompareOp       _comparisonOp = CompareOp::Always;
    };

    /// Equivalent to MTLDepthStencilDescriptor or D3D12_DEPTH_STENCIL_DESC or VkPipelineDepthStencilStateCreateInfo
    class DepthStencilDesc
    {
    public:
        CompareOp       _depthTest = CompareOp::LessEqual;
        bool            _depthWrite = true;
        bool            _stencilEnable = false;
        uint8_t         _stencilReadMask = 0x0;
        uint8_t         _stencilWriteMask = 0x0;
        uint8_t         _stencilReference = 0x0;
        StencilDesc     _frontFaceStencil;
        StencilDesc     _backFaceStencil;
    };

    /// Similar to VkPipelineRasterizationStateCreateInfo or D3D12_RASTERIZER_DESC
    /// (Metal just has separate function calls)
    class RasterizationDesc
    {
    public:
        CullMode        _cullMode = CullMode::Back;
        FaceWinding     _frontFaceWinding = FaceWinding::CCW;
    };

    /// Similar to ?
    class SamplerStateDesc
    {
    public:
        RenderCore::FilterMode _filter = RenderCore::FilterMode::Trilinear;
        RenderCore::AddressMode _addressU = RenderCore::AddressMode::Wrap;
        RenderCore::AddressMode _addressV = RenderCore::AddressMode::Wrap;
        RenderCore::CompareOp _comparison = RenderCore::CompareOp::Never;
    };

////////////////////////////////////////////////////////////////////////////////////////////////////

    class SamplerState
    {
    public:
        SamplerState(   FilterMode filter = FilterMode::Trilinear,
                        AddressMode addressU = AddressMode::Wrap,
                        AddressMode addressV = AddressMode::Wrap,
                        AddressMode addressW = AddressMode::Wrap,
                        CompareOp comparison = CompareOp::Never);

        typedef SamplerState UnderlyingType;
        UnderlyingType GetUnderlying() const never_throws { return *this; }
    };

    class BlendState
    {
    public:
        BlendState();
        void Apply() const never_throws;
    };

////////////////////////////////////////////////////////////////////////////////////////////////////

    class ViewportDesc
    {
    public:
            // (naming convention as per D3D11_VIEWPORT)
        float TopLeftX, TopLeftY;
        float Width, Height;
        float MinDepth, MaxDepth;

        ViewportDesc(DeviceContext&);
        ViewportDesc(float topLeftX=0.f, float topLeftY=0.f, float width=0.f, float height=0.f, float minDepth=0.f, float maxDepth=1.f)
        : TopLeftX(topLeftX), TopLeftY(topLeftY)
        , Width(width), Height(height)
        , MinDepth(minDepth), MaxDepth(maxDepth)
        {}
    };
}}
