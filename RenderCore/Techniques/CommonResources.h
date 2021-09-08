// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../StateDesc.h"
#include "../ResourceUtils.h"
#include "../IDevice_Forward.h"
#include "../Metal/Metal.h"

namespace RenderCore { namespace Techniques 
{
    class CommonResourceBox
    {
    public:
        std::shared_ptr<ISampler> _defaultSampler;
        std::shared_ptr<ISampler> _linearWrapSampler;
        std::shared_ptr<ISampler> _linearClampSampler;
        std::shared_ptr<ISampler> _anisotropicWrapSampler;
        std::shared_ptr<ISampler> _pointClampSampler;
        std::shared_ptr<ISampler> _unnormalizedBilinearClampSampler;
        SamplerPool _samplerPool;

		///////////////////////////////////////

		static DepthStencilDesc s_dsReadWrite;
        static DepthStencilDesc s_dsReadOnly;
        static DepthStencilDesc s_dsDisable;
        static DepthStencilDesc s_dsReadWriteWriteStencil;
        static DepthStencilDesc s_dsWriteOnly;
        static DepthStencilDesc s_dsReadWriteCloserThan;

		static AttachmentBlendDesc s_abStraightAlpha;
		static AttachmentBlendDesc s_abAlphaPremultiplied;
		static AttachmentBlendDesc s_abOpaque;
		static AttachmentBlendDesc s_abOneSrcAlpha;
		static AttachmentBlendDesc s_abAdditive;

        static RasterizationDesc s_rsDefault;
        static RasterizationDesc s_rsCullDisable;
        static RasterizationDesc s_rsCullReverse;

        uint64_t GetGUID() const { return _guid; }

        CommonResourceBox(IDevice&);
        ~CommonResourceBox();

    private:
        CommonResourceBox(CommonResourceBox&) = delete;
        CommonResourceBox& operator=(const CommonResourceBox&) = delete;
        uint64_t _guid = 0;
    };

}}

