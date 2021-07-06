// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "CommonResources.h"
#include "CommonBindings.h"
#include "../IDevice.h"
#include "../Metal/Metal.h"
#include "../../Utility/MemoryUtils.h"

#if GFXAPI_TARGET == GFXAPI_DX11
    #include "TechniqueUtils.h" // just for sizeof(LocalTransformConstants)
    #include "../Metal/ObjectFactory.h"
#endif

namespace RenderCore { namespace Techniques
{
    DepthStencilDesc CommonResourceBox::s_dsReadWrite {};
    DepthStencilDesc CommonResourceBox::s_dsReadOnly { CompareOp::LessEqual, false };
    DepthStencilDesc CommonResourceBox::s_dsDisable { CompareOp::Always, false };
    DepthStencilDesc CommonResourceBox::s_dsReadWriteWriteStencil { CompareOp::LessEqual, true, true, 0xff, 0xff, StencilDesc::AlwaysWrite, StencilDesc::AlwaysWrite };
    DepthStencilDesc CommonResourceBox::s_dsWriteOnly { CompareOp::Always, true };
    DepthStencilDesc CommonResourceBox::s_dsReadWriteLessThan { CompareOp::Less };

    AttachmentBlendDesc CommonResourceBox::s_abStraightAlpha { true, Blend::SrcAlpha, Blend::InvSrcAlpha, BlendOp::Add };
    AttachmentBlendDesc CommonResourceBox::s_abAlphaPremultiplied { true, Blend::One, Blend::InvSrcAlpha, BlendOp::Add };
    AttachmentBlendDesc CommonResourceBox::s_abOneSrcAlpha { true, Blend::One, Blend::SrcAlpha, BlendOp::Add };
    AttachmentBlendDesc CommonResourceBox::s_abAdditive { true, Blend::One, Blend::One, BlendOp::Add };
    AttachmentBlendDesc CommonResourceBox::s_abOpaque { };

    RasterizationDesc CommonResourceBox::s_rsDefault { CullMode::Back };
    RasterizationDesc CommonResourceBox::s_rsCullDisable { CullMode::None };
    RasterizationDesc CommonResourceBox::s_rsCullReverse { CullMode::Back, FaceWinding::CW };

    CommonResourceBox::CommonResourceBox(IDevice& device)
    {
        using namespace RenderCore::Metal;
#if GFXAPI_TARGET == GFXAPI_DX11
        _dssReadWrite = DepthStencilState();
        _dssReadOnly = DepthStencilState(true, false);
        _dssDisable = DepthStencilState(false, false);
        _dssReadWriteWriteStencil = DepthStencilState(true, true, 0xff, 0xff, StencilMode::AlwaysWrite, StencilMode::AlwaysWrite);
        _dssWriteOnly = DepthStencilState(true, true, CompareOp::Always);

        _blendStraightAlpha = BlendState(BlendOp::Add, Blend::SrcAlpha, Blend::InvSrcAlpha);
        _blendAlphaPremultiplied = BlendState(BlendOp::Add, Blend::One, Blend::InvSrcAlpha);
        _blendOneSrcAlpha = BlendState(BlendOp::Add, Blend::One, Blend::SrcAlpha);
        _blendAdditive = BlendState(BlendOp::Add, Blend::One, Blend::One);
        _blendOpaque = BlendOp::NoBlending;

        _defaultRasterizer = CullMode::Back;
        _cullDisable = CullMode::None;
        _cullReverse = RasterizerState(CullMode::Back, false);

        _localTransformBuffer = MakeConstantBuffer(GetObjectFactory(), sizeof(LocalTransformConstants));
#endif

        _linearClampSampler = device.CreateSampler(SamplerDesc{FilterMode::Trilinear, AddressMode::Clamp, AddressMode::Clamp});
        _linearWrapSampler = device.CreateSampler(SamplerDesc{FilterMode::Trilinear, AddressMode::Wrap, AddressMode::Wrap});
        _pointClampSampler = device.CreateSampler(SamplerDesc{FilterMode::Point, AddressMode::Clamp, AddressMode::Clamp});
        _anisotropicWrapSampler = device.CreateSampler(SamplerDesc{FilterMode::Anisotropic, AddressMode::Wrap, AddressMode::Wrap});
        _unnormalizedBilinearClampSampler = device.CreateSampler(SamplerDesc{FilterMode::Bilinear, AddressMode::Wrap, AddressMode::Wrap, CompareOp::Never, SamplerDescFlags::UnnormalizedCoordinates});
        _defaultSampler = _linearWrapSampler;
    }

    CommonResourceBox::~CommonResourceBox()
    {}

    namespace AttachmentSemantics
    {
        const char* TryDehash(uint64_t hashValue)
        {
            switch (hashValue) {
            case MultisampleDepth: return "MultisampleDepth";
            case GBufferDiffuse: return "GBufferDiffuse";
            case GBufferNormal: return "GBufferNormal";
            case GBufferParameter: return "GBufferParameter";
            case GBufferVelocity: return "GBufferVelocity";
            case ColorLDR: return "ColorLDR";
            case ColorHDR: return "ColorHDR";
            case Depth: return "Depth";
            case ShadowDepthMap: return "ShadowDepthMap";
            default: return nullptr;
            }
        }
    }

    namespace CommonSemantics
    {
        std::pair<const char*, unsigned> TryDehash(uint64_t hashValue)
        {
            if ((hashValue - POSITION) < 16) return std::make_pair("POSITION", unsigned(hashValue - POSITION));
            if ((hashValue - PIXELPOSITION) < 16) return std::make_pair("PIXELPOSITION", unsigned(hashValue - PIXELPOSITION));
            else if ((hashValue - TEXCOORD) < 16) return std::make_pair("TEXCOORD", unsigned(hashValue - TEXCOORD));
            else if ((hashValue - COLOR) < 16) return std::make_pair("COLOR", unsigned(hashValue - COLOR));
            else if ((hashValue - NORMAL) < 16) return std::make_pair("NORMAL", unsigned(hashValue - NORMAL));
            else if ((hashValue - TEXTANGENT) < 16) return std::make_pair("TEXTANGENT", unsigned(hashValue - TEXTANGENT));
            else if ((hashValue - TEXBITANGENT) < 16) return std::make_pair("TEXBITANGENT", unsigned(hashValue - TEXBITANGENT));
            else if ((hashValue - BONEINDICES) < 16) return std::make_pair("BONEINDICES", unsigned(hashValue - BONEINDICES));
            else if ((hashValue - BONEWEIGHTS) < 16) return std::make_pair("BONEWEIGHTS", unsigned(hashValue - BONEWEIGHTS));
            else if ((hashValue - PER_VERTEX_AO) < 16) return std::make_pair("PER_VERTEX_AO", unsigned(hashValue - PER_VERTEX_AO));
            else if ((hashValue - RADIUS) < 16) return std::make_pair("RADIUS", unsigned(hashValue - RADIUS));
            else return std::make_pair(nullptr, ~0u);
        }
    }
}}
