// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../StateDesc.h"
#include "../IDevice_Forward.h"
#include "../Metal/Metal.h"

#if GFXAPI_TARGET == GFXAPI_DX11
    #include "../Metal/State.h"
#endif

namespace RenderCore { namespace Techniques 
{
    class CommonResourceBox
    {
    public:
#if GFXAPI_TARGET == GFXAPI_DX11
        Metal::DepthStencilState _dssReadWrite;
        Metal::DepthStencilState _dssReadOnly;
        Metal::DepthStencilState _dssDisable;
        Metal::DepthStencilState _dssReadWriteWriteStencil;
        Metal::DepthStencilState _dssWriteOnly;

        Metal::BlendState _blendStraightAlpha;
        Metal::BlendState _blendAlphaPremultiplied;
        Metal::BlendState _blendOpaque;
        Metal::BlendState _blendOneSrcAlpha;
        Metal::BlendState _blendAdditive;

        Metal::RasterizerState _defaultRasterizer;
        Metal::RasterizerState _cullDisable;
        Metal::RasterizerState _cullReverse;
        
        Metal::ConstantBuffer _localTransformBuffer;
#endif

        std::shared_ptr<ISampler> _defaultSampler;
        std::shared_ptr<ISampler> _linearWrapSampler;
        std::shared_ptr<ISampler> _linearClampSampler;
        std::shared_ptr<ISampler> _anisotropicWrapSampler;
        std::shared_ptr<ISampler> _pointClampSampler;

		///////////////////////////////////////

		static DepthStencilDesc s_dsReadWrite;
        static DepthStencilDesc s_dsReadOnly;
        static DepthStencilDesc s_dsDisable;
        static DepthStencilDesc s_dsReadWriteWriteStencil;
        static DepthStencilDesc s_dsWriteOnly;
        static DepthStencilDesc s_dsReadWriteLessThan;

		static AttachmentBlendDesc s_abStraightAlpha;
		static AttachmentBlendDesc s_abAlphaPremultiplied;
		static AttachmentBlendDesc s_abOpaque;
		static AttachmentBlendDesc s_abOneSrcAlpha;
		static AttachmentBlendDesc s_abAdditive;

        static RasterizationDesc s_rsDefault;
        static RasterizationDesc s_rsCullDisable;
        static RasterizationDesc s_rsCullReverse;

        CommonResourceBox(IDevice&);
        ~CommonResourceBox();

    private:
        CommonResourceBox(CommonResourceBox&);
        CommonResourceBox& operator=(const CommonResourceBox&);
    };

}}

