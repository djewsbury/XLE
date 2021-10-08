// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "CommonResources.h"
#include "CommonBindings.h"
#include "../IDevice.h"
#include "../IThreadContext.h"
#include "../Metal/Metal.h"
#include "../Metal/Resource.h"          // for Metal::CompleteInitialization
#include "../Metal/DeviceContext.h"     // for Metal::CompleteInitialization
#include "../../Utility/MemoryUtils.h"

#if GFXAPI_TARGET == GFXAPI_DX11
    #include "TechniqueUtils.h" // just for sizeof(LocalTransformConstants)
    #include "../Metal/ObjectFactory.h"
#endif

namespace RenderCore { namespace Techniques
{
    DepthStencilDesc CommonResourceBox::s_dsReadWrite { CompareOp::GreaterEqual };
    DepthStencilDesc CommonResourceBox::s_dsReadOnly { CompareOp::GreaterEqual, false };
    DepthStencilDesc CommonResourceBox::s_dsDisable { CompareOp::Always, false };
    DepthStencilDesc CommonResourceBox::s_dsReadWriteWriteStencil { CompareOp::GreaterEqual, true, true, 0xff, 0xff, StencilDesc::AlwaysWrite, StencilDesc::AlwaysWrite };
    DepthStencilDesc CommonResourceBox::s_dsWriteOnly { CompareOp::Always, true };
    DepthStencilDesc CommonResourceBox::s_dsReadWriteCloserThan { CompareOp::Greater }; // (ie, when reversed Z is the default, greater is closer)

    AttachmentBlendDesc CommonResourceBox::s_abStraightAlpha { true, Blend::SrcAlpha, Blend::InvSrcAlpha, BlendOp::Add };
    AttachmentBlendDesc CommonResourceBox::s_abAlphaPremultiplied { true, Blend::One, Blend::InvSrcAlpha, BlendOp::Add };
    AttachmentBlendDesc CommonResourceBox::s_abOneSrcAlpha { true, Blend::One, Blend::SrcAlpha, BlendOp::Add };
    AttachmentBlendDesc CommonResourceBox::s_abAdditive { true, Blend::One, Blend::One, BlendOp::Add };
    AttachmentBlendDesc CommonResourceBox::s_abOpaque { };

    RasterizationDesc CommonResourceBox::s_rsDefault { CullMode::Back };
    RasterizationDesc CommonResourceBox::s_rsCullDisable { CullMode::None };
    RasterizationDesc CommonResourceBox::s_rsCullReverse { CullMode::Back, FaceWinding::CW };

    static uint64_t s_nextCommonResourceBoxGuid = 1;

    static std::shared_ptr<IResource> CreateBlackResource(IDevice& device, const ResourceDesc& resDesc)
    {
        if (resDesc._type == ResourceDesc::Type::Texture) {
            return device.CreateResource(resDesc);
        } else {
            std::vector<uint8_t> blank(ByteCount(resDesc), 0);
            return device.CreateResource(resDesc, SubResourceInitData{MakeIteratorRange(blank)});
        }
    }

    static std::shared_ptr<IResource> CreateWhiteResource(IDevice& device, const ResourceDesc& resDesc)
    {
        if (resDesc._type == ResourceDesc::Type::Texture) {
            return device.CreateResource(resDesc);
        } else {
            std::vector<uint8_t> blank(ByteCount(resDesc), 0xff);
            return device.CreateResource(resDesc, SubResourceInitData{MakeIteratorRange(blank)});
        }
    }

    CommonResourceBox::CommonResourceBox(IDevice& device)
    : _samplerPool(device)
    , _guid(s_nextCommonResourceBoxGuid++)
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
        _unnormalizedBilinearClampSampler = device.CreateSampler(SamplerDesc{FilterMode::Bilinear, AddressMode::Clamp, AddressMode::Clamp, AddressMode::Clamp, CompareOp::Never, SamplerDescFlags::UnnormalizedCoordinates});
        _defaultSampler = _linearWrapSampler;

        _black2DSRV = CreateBlackResource(device, CreateDesc(BindFlag::ShaderResource|BindFlag::TransferDst, 0, GPUAccess::Read, TextureDesc::Plain2D(32, 32, Format::R8_UNORM), "black2d"))->CreateTextureView();
        _black2DArraySRV = CreateBlackResource(device, CreateDesc(BindFlag::ShaderResource|BindFlag::TransferDst, 0, GPUAccess::Read, TextureDesc::Plain2D(32, 32, Format::R8_UNORM, 1, 1), "black2darray"))->CreateTextureView();
        _black3DSRV = CreateBlackResource(device, CreateDesc(BindFlag::ShaderResource|BindFlag::TransferDst, 0, GPUAccess::Read, TextureDesc::Plain3D(8, 8, 8, Format::R8_UNORM), "black3d"))->CreateTextureView();
        _blackCubeSRV = CreateBlackResource(device, CreateDesc(BindFlag::ShaderResource|BindFlag::TransferDst, 0, GPUAccess::Read, TextureDesc::PlainCube(32, 32, Format::R8_UNORM), "blackCube"))->CreateTextureView();
        _blackCubeArraySRV = CreateBlackResource(device, CreateDesc(BindFlag::ShaderResource|BindFlag::TransferDst, 0, GPUAccess::Read, TextureDesc::PlainCube(32, 32, Format::R8_UNORM, 1, 6), "blackCubeArray"))->CreateTextureView();
        _blackCB = CreateBlackResource(device, CreateDesc(BindFlag::ConstantBuffer, 0, GPUAccess::Read, LinearBufferDesc{256}, "blackbuffer"));
        _blackBufferUAV = CreateBlackResource(device, CreateDesc(BindFlag::UnorderedAccess, 0, GPUAccess::Read, LinearBufferDesc{256, 16}, "blackbufferuav"))->CreateBufferView(BindFlag::UnorderedAccess);

        _white2DSRV = CreateWhiteResource(device, CreateDesc(BindFlag::ShaderResource|BindFlag::TransferDst, 0, GPUAccess::Read, TextureDesc::Plain2D(32, 32, Format::R8_UNORM), "white2d"))->CreateTextureView();
        _white2DArraySRV = CreateWhiteResource(device, CreateDesc(BindFlag::ShaderResource|BindFlag::TransferDst, 0, GPUAccess::Read, TextureDesc::Plain2D(32, 32, Format::R8_UNORM, 1, 1), "white2darray"))->CreateTextureView();
        _white3DSRV = CreateWhiteResource(device, CreateDesc(BindFlag::ShaderResource|BindFlag::TransferDst, 0, GPUAccess::Read, TextureDesc::Plain3D(8, 8, 8, Format::R8_UNORM), "white3d"))->CreateTextureView();
        _whiteCubeSRV = CreateWhiteResource(device, CreateDesc(BindFlag::ShaderResource|BindFlag::TransferDst, 0, GPUAccess::Read, TextureDesc::PlainCube(32, 32, Format::R8_UNORM), "whiteCube"))->CreateTextureView();
        _whiteCubeArraySRV = CreateWhiteResource(device, CreateDesc(BindFlag::ShaderResource|BindFlag::TransferDst, 0, GPUAccess::Read, TextureDesc::PlainCube(32, 32, Format::R8_UNORM, 1, 6), "whiteCubeArray"))->CreateTextureView();

        _pendingCompleteInitialization = true;
    }

    CommonResourceBox::~CommonResourceBox()
    {}

    void CommonResourceBox::CompleteInitialization(IThreadContext& threadContext)
    {
        if (!_pendingCompleteInitialization) return;
        IResource* blackTextures[] {
            _black2DSRV->GetResource().get(),
            _black2DArraySRV->GetResource().get(),
            _black3DSRV->GetResource().get(),
            _blackCubeSRV->GetResource().get(),
            _blackCubeArraySRV->GetResource().get()
        };
        IResource* whiteTextures[] {
            _white2DSRV->GetResource().get(),
            _white2DArraySRV->GetResource().get(),
            _white3DSRV->GetResource().get(),
            _whiteCubeSRV->GetResource().get(),
            _whiteCubeArraySRV->GetResource().get()
        };
        auto& metalContext = *Metal::DeviceContext::Get(threadContext);
        Metal::CompleteInitialization(metalContext, MakeIteratorRange(blackTextures));
        Metal::CompleteInitialization(metalContext, MakeIteratorRange(whiteTextures));
        
        // We also have to clear out data for the textures (since these can't be initialized
        // in the construction operation)
        // We might be able to do this with just a clear call on some APIs; but let's do it
        // it hard way, anyway
        size_t largest = 0;
        for (const auto& res:blackTextures)
            largest = std::max(largest, (size_t)ByteCount(res->GetDesc()));
        for (const auto& res:whiteTextures)
            largest = std::max(largest, (size_t)ByteCount(res->GetDesc()));

        {
            auto staging = CreateBlackResource(*threadContext.GetDevice(), CreateDesc(BindFlag::TransferSrc, 0, 0, LinearBufferDesc{(unsigned)largest}, "staging"));
            auto encoder = metalContext.BeginBlitEncoder();
            for (const auto& res:blackTextures)
                encoder.Copy(*res, *staging);
        }
        {
            auto staging = CreateWhiteResource(*threadContext.GetDevice(), CreateDesc(BindFlag::TransferSrc, 0, 0, LinearBufferDesc{(unsigned)largest}, "staging"));
            auto encoder = metalContext.BeginBlitEncoder();
            for (const auto& res:whiteTextures)
                encoder.Copy(*res, *staging);
        }
        _pendingCompleteInitialization = false;
    }

    namespace AttachmentSemantics
    {
        const char* TryDehash(uint64_t hashValue)
        {
            switch (hashValue) {
            case MultisampleDepth: return "MultisampleDepth";
            case GBufferDiffuse: return "GBufferDiffuse";
            case GBufferNormal: return "GBufferNormal";
            case GBufferParameter: return "GBufferParameter";
            case GBufferMotion: return "GBufferMotion";
            case ColorLDR: return "ColorLDR";
            case ColorHDR: return "ColorHDR";
            case Depth: return "Depth";
            case ShadowDepthMap: return "ShadowDepthMap";
            case HierarchicalDepths: return "HierarchicalDepths";
            case TiledLightBitField: return "TiledLightBitField";
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
