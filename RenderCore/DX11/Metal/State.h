// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "DX11.h"
#include "../../Types.h"
#include "../../StateDesc.h"
#include "../../Math/Vector.h"
#include "../../../Utility/IntrusivePtr.h"

namespace RenderCore { namespace Metal_DX11
{
////////////////////////////////////////////////////////////////////////////////////////////////////

    class DeviceContext;

    /// <summary>States for sampling from textures</summary>
    /// 
    ///     Sets states that are used by the hardware when sampling from textures. Most useful
    ///     states are:
    ///         <list>
    ///              <item> filtering mode (FilterMode::Enum)
    ///                      Set to point / linear / trilinear / anisotrophic (etc)
    ///              <item> addressing mode (AddressMode::Enum)
    ///                      Wrapping / mirroring / clamping on texture edges
    ///         </list>
    ///
    ///     Currently the following D3D states are not exposed by this interface:
    ///         <list>
    ///             <item> MipLODBias
    ///             <item> MaxAnisotrophy
    ///             <item> ComparisonFunc
    ///             <item> BorderColor
    ///             <item> MinLOD
    ///             <item> MaxLOD
    ///         </list>
    ///
    ///     It's best to limit the total number of SamplerState objects used by
    ///     the system. In an ideal game, there should be D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT (16)
    ///     sampler states or less. Then, they could all be bound at the start of the frame, and
    ///     avoid any further state thrashing.
    /// 
    /// <exception cref="::RenderCore::Exceptions::AllocationFailure">
    ///     Failed to create underlying object. Could be caused by invalid input values, or a corrupt/lost device.
    /// </exception>
    class SamplerState
    {
    public:
        SamplerState(const SamplerStateDesc& desc) : SamplerState(desc._filter, desc._addressU, desc._addressV, AddressMode::Wrap, desc._comparison) {}
		SamplerState(   FilterMode filter = FilterMode::Trilinear,
                        AddressMode addressU = AddressMode::Wrap, 
                        AddressMode addressV = AddressMode::Wrap, 
                        AddressMode addressW = AddressMode::Wrap,
						CompareOp comparison = CompareOp::Never);
        ~SamplerState();

        SamplerState(SamplerState&& moveFrom) = default;
        SamplerState& operator=(SamplerState&& moveFrom) = default;
        SamplerState(const SamplerState& copyFrom) = default;
        SamplerState& operator=(const SamplerState& copyFrom) = default;

        typedef ID3D::SamplerState* UnderlyingType;
        UnderlyingType              GetUnderlying() const  { return _underlying.get(); }

    private:
        intrusive_ptr<ID3D::SamplerState>  _underlying;
    };

        ////////////////////////////////////////////////////////////////////////////////////////////////

    /// <summary>States used while rasterising triangles</summary>
    ///
    ///     These states are used during triangle setup for interpolator
    ///     initialising and culling.
    ///
    ///     Common states:
    ///         <list>
    ///              <item> cull mode (CullMode::Enum)
    ///                     this is the most commonly used state
    ///         </list>
    ///
    ///     D3D states not exposed by this interface:
    ///         <list>
    ///             <item> FillMode
    ///             <item> FrontCounterClockwise
    ///             <item> DepthBias
    ///             <item> DepthBiasClamp
    ///             <item> SlopeScaledDepthBias
    ///             <item> DepthClipEnable
    ///             <item> ScissorEnable
    ///             <item> MultisampleEnable        (defaults to true)
    ///             <item> AntialiasedLineEnable
    ///         </list>
    ///
    /// <exception cref="::RenderCore::Exceptions::AllocationFailure">
    ///     Failed to create underlying object. Could be caused by invalid input values, or a corrupt/lost device.
    /// </exception>
    class RasterizerState
    {
    public:
        RasterizerState(const RasterizationDesc& desc) : RasterizerState(desc._cullMode, desc._frontFaceWinding == FaceWinding::CCW, FillMode::Solid, (int)desc._depthBiasConstantFactor, desc._depthBiasClamp, desc._depthBiasSlopeFactor) {}
		RasterizerState(CullMode cullmode = CullMode::Back, bool frontCounterClockwise = true);
        RasterizerState(
            CullMode cullmode, bool frontCounterClockwise,
            FillMode fillmode,
            int depthBias, float depthBiasClamp, float slopeScaledBias);
        RasterizerState(DeviceContext&);
        ~RasterizerState();

        RasterizerState(RasterizerState&& moveFrom) = default;
        RasterizerState& operator=(RasterizerState&& moveFrom) = default;
        RasterizerState(const RasterizerState& copyFrom) = default;
        RasterizerState& operator=(const RasterizerState& copyFrom) = default;
        RasterizerState(intrusive_ptr<ID3D::RasterizerState>&& moveFrom);

        typedef ID3D::RasterizerState*  UnderlyingType;
        UnderlyingType                  GetUnderlying() const  { return _underlying.get(); }

        static RasterizerState Null();

    private:
        intrusive_ptr<ID3D::RasterizerState>  _underlying;
    };

        ////////////////////////////////////////////////////////////////////////////////////////////////

    /// <summary>States used while drawing pixels to the render targets</summary>
    ///
    ///     These states are used to blend the pixel shader output with
    ///     the existing colours in the render targets.
    ///
    ///     In D3D11, we can define separate blending modes for each render target
    ///     (and separate blending for colour and alpha parts).
    ///
    ///     However, most of the time we want to just use the same blend mode
    ///     for all render targets. So, the simple interface will smear the
    ///     given blend mode across all render target settings.
    ///
    ///     BlendState can be constructed with just "BlendOp::NoBlending" if you
    ///     want to disable all blending. For example:
    ///         <code>
    ///             context->Bind(BlendOp::NoBlending);
    ///         </code>
    ///
    ///     Common states:
    ///         <list>
    ///             <item> blending operation (BlendOp::Enum)
    ///             <item> blending parameter for source (Blend::Enum)
    ///             <item> blending parameter for destination (Blend::Enum)
    ///         </list>
    ///
    ///     D3D states not exposed by this interface:
    ///         <list>
    ///             <item> AlphaToCoverageEnable
    ///             <item> IndependentBlendEnable
    ///             <item> RenderTargetWriteMask
    ///         </list>
    ///
    /// <exception cref="::RenderCore::Exceptions::AllocationFailure">
    ///     Failed to create underlying object. Could be caused by invalid input values, or a corrupt/lost device.
    /// </exception>
    class BlendState
    {
    public:
		BlendState(const AttachmentBlendDesc& desc);
        BlendState( BlendOp blendingOperation = BlendOp::Add, 
                    Blend srcBlend = Blend::SrcAlpha,
                    Blend dstBlend = Blend::InvSrcAlpha);
        BlendState( BlendOp blendingOperation, 
                    Blend srcBlend,
                    Blend dstBlend,
                    BlendOp alphaBlendingOperation, 
                    Blend alphaSrcBlend,
                    Blend alphaDstBlend);
        ~BlendState();

        BlendState(BlendState&& moveFrom) = default;
        BlendState& operator=(BlendState&& moveFrom) = default;
        BlendState(const BlendState& copyFrom) = default;
        BlendState& operator=(const BlendState& copyFrom) = default;
        BlendState(intrusive_ptr<ID3D::BlendState>&& moveFrom);
        BlendState(DeviceContext& context);

        static BlendState OutputDisabled();
        static BlendState Null();

        typedef ID3D::BlendState*   UnderlyingType;
        UnderlyingType              GetUnderlying() const  { return _underlying.get(); }

    private:
        intrusive_ptr<ID3D::BlendState>  _underlying;
    };

        ////////////////////////////////////////////////////////////////////////////////////////////////

	// Deprecated -- for backwards compatibility only
	class XLE_DEPRECATED_ATTRIBUTE StencilMode : public StencilDesc
    {
    public:
        StencilMode(
			CompareOp comparison      = CompareOp::Always,
            StencilOp onPass          = StencilOp::Replace,
            StencilOp onStencilFail   = StencilOp::DontWrite,
            StencilOp onDepthFail     = StencilOp::DontWrite)
		{
			_passOp = onPass;
			_failOp = onStencilFail;
			_depthFailOp = onDepthFail;
			_comparisonOp = comparison;
		}

        static StencilMode NoEffect;
        static StencilMode AlwaysWrite;
    };

    /// <summary>States used reading and writing to the depth buffer</summary>
    ///
    ///     These states are used by the hardware to determine how pixels are
    ///     rejected by the depth buffer, and whether we write new depth values to
    ///     the depth buffer.
    ///
    ///     Common states:
    ///         <list>
    ///             <item> depth enable (enables/disables both reading to and writing from the depth buffer)
    ///             <item> write enable (enables/disables writing to the depth buffer)
    ///         </list>
    ///
    ///     Stencil states:
    ///         <list>
    ///             <item> For both front and back faces:
    //                  <list>
    ///                     <item> comparison mode
    ///                     <item> operation for stencil and depth pass
    ///                     <item> operation for stencil fail
    ///                     <item> operation for depth fail
    ///                 </list>
    ///         </list>
    ///
    ///     D3D states not exposed by this interface:
    ///         <list>
    ///             <item> DepthFunc (always D3D11_COMPARISON_LESS_EQUAL)
    ///             <item> StencilReadMask
    ///             <item> StencilWriteMask
    ///         </list>
    ///
    /// <exception cref="::RenderCore::Exceptions::AllocationFailure">
    ///     Failed to create underlying object. Could be caused by invalid input values, or a corrupt/lost device.
    /// </exception>
    class DepthStencilState
    {
    public:
		DepthStencilState(const DepthStencilDesc& desc);
        explicit DepthStencilState(bool enabled=true, bool writeEnabled=true, CompareOp comparison = CompareOp::LessEqual);
        DepthStencilState(
            bool depthTestEnabled, bool writeEnabled, 
            unsigned stencilReadMask, unsigned stencilWriteMask,
            const StencilDesc& frontFaceStencil = StencilMode::NoEffect,
            const StencilDesc& backFaceStencil = StencilMode::NoEffect);
        DepthStencilState(DeviceContext& context);
        DepthStencilState(const DepthStencilState&) = default;
        DepthStencilState& operator=(const DepthStencilState&) = default;
		DepthStencilState(DepthStencilState&&) = default;
        DepthStencilState& operator=(DepthStencilState&&) = default;
        ~DepthStencilState();

        typedef ID3D::DepthStencilState*    UnderlyingType;
        UnderlyingType                      GetUnderlying() const  { return _underlying.get(); }

    private:
        intrusive_ptr<ID3D::DepthStencilState>  _underlying;
    };

        ////////////////////////////////////////////////////////////////////////////////////////////////

	using ViewportDesc = RenderCore::ViewportDesc;
}}

