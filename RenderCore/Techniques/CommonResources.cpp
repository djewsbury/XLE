// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "CommonResources.h"
#include "CommonBindings.h"
#include "../IDevice.h"
#include "../DeviceInitialization.h"
#include "../Metal/Metal.h"
#include "../Metal/Resource.h"          // for Metal::CompleteInitialization
#include "../Metal/DeviceContext.h"     // for Metal::CompleteInitialization
#include "../../Utility/MemoryUtils.h"

using namespace Utility::Literals;

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

    CommonResourceBox::CommonResourceBox(IDevice& device)
    : _samplerPool(device)
    , _guid(s_nextCommonResourceBoxGuid++)
    {
        using namespace RenderCore::Metal;
        _linearClampSampler = device.CreateSampler(SamplerDesc{FilterMode::Trilinear, AddressMode::Clamp, AddressMode::Clamp});
        _linearWrapSampler = device.CreateSampler(SamplerDesc{FilterMode::Trilinear, AddressMode::Wrap, AddressMode::Wrap});
        _pointClampSampler = device.CreateSampler(SamplerDesc{FilterMode::Point, AddressMode::Clamp, AddressMode::Clamp});
        _anisotropicWrapSampler = device.CreateSampler(SamplerDesc{FilterMode::Anisotropic, AddressMode::Wrap, AddressMode::Wrap});
        _unnormalizedBilinearClampSampler = device.CreateSampler(SamplerDesc{FilterMode::Bilinear, AddressMode::Clamp, AddressMode::Clamp, AddressMode::Clamp, CompareOp::Never, SamplerDescFlags::UnnormalizedCoordinates});
        _defaultSampler = _linearWrapSampler;

        _black2DSRV = device.CreateResource(CreateDesc(BindFlag::ShaderResource|BindFlag::TransferDst, TextureDesc::Plain2D(32, 32, Format::R8_UNORM)), "black2d")->CreateTextureView();
        _black2DArraySRV = device.CreateResource(CreateDesc(BindFlag::ShaderResource|BindFlag::TransferDst, TextureDesc::Plain2D(32, 32, Format::R8_UNORM, 1, 1)), "black2darray")->CreateTextureView();
        _black3DSRV = device.CreateResource(CreateDesc(BindFlag::ShaderResource|BindFlag::TransferDst, TextureDesc::Plain3D(8, 8, 8, Format::R8_UNORM)), "black3d")->CreateTextureView();
        _blackCubeSRV = device.CreateResource(CreateDesc(BindFlag::ShaderResource|BindFlag::TransferDst, TextureDesc::PlainCube(32, 32, Format::R8_UNORM)), "blackCube")->CreateTextureView();
        _blackCB = device.CreateResource(CreateDesc(BindFlag::ConstantBuffer|BindFlag::TransferDst, LinearBufferDesc{256}), "blackbuffer");

        _undefined2DUAV = device.CreateResource(CreateDesc(BindFlag::UnorderedAccess|BindFlag::TransferDst, TextureDesc::Plain2D(32, 32, Format::R8_UNORM)), "undefined2d")->CreateTextureView(BindFlag::UnorderedAccess);
        _undefined2DArrayUAV = device.CreateResource(CreateDesc(BindFlag::UnorderedAccess|BindFlag::TransferDst, TextureDesc::Plain2D(32, 32, Format::R8_UNORM, 1, 1)), "undefined2darray")->CreateTextureView(BindFlag::UnorderedAccess);
        _undefined3DUAV = device.CreateResource(CreateDesc(BindFlag::UnorderedAccess|BindFlag::TransferDst, TextureDesc::Plain3D(8, 8, 8, Format::R8_UNORM)), "undefined3d")->CreateTextureView(BindFlag::UnorderedAccess);
        _undefinedCubeUAV = device.CreateResource(CreateDesc(BindFlag::UnorderedAccess|BindFlag::TransferDst, TextureDesc::PlainCube(32, 32, Format::R8_UNORM)), "undefinedCube")->CreateTextureView(BindFlag::UnorderedAccess);
        _undefinedBufferUAV = device.CreateResource(CreateDesc(BindFlag::UnorderedAccess|BindFlag::TransferDst, LinearBufferDesc{256, 16}), "undefinedbuffer")->CreateBufferView(BindFlag::UnorderedAccess);

        _white2DSRV = device.CreateResource(CreateDesc(BindFlag::ShaderResource|BindFlag::TransferDst, TextureDesc::Plain2D(32, 32, Format::R8_UNORM)), "white2d")->CreateTextureView();
        _white2DArraySRV = device.CreateResource(CreateDesc(BindFlag::ShaderResource|BindFlag::TransferDst, TextureDesc::Plain2D(32, 32, Format::R8_UNORM, 1, 1)), "white2darray")->CreateTextureView();
        _white3DSRV = device.CreateResource(CreateDesc(BindFlag::ShaderResource|BindFlag::TransferDst, TextureDesc::Plain3D(8, 8, 8, Format::R8_UNORM)), "white3d")->CreateTextureView();
        _whiteCubeSRV = device.CreateResource(CreateDesc(BindFlag::ShaderResource|BindFlag::TransferDst, TextureDesc::PlainCube(32, 32, Format::R8_UNORM)), "whiteCube")->CreateTextureView();

        if (device.GetDeviceFeatures()._cubemapArrays) {
            _blackCubeArraySRV = device.CreateResource(CreateDesc(BindFlag::ShaderResource|BindFlag::TransferDst, TextureDesc::PlainCube(32, 32, Format::R8_UNORM, 1, 6)), "blackCubeArray")->CreateTextureView();
            _whiteCubeArraySRV = device.CreateResource(CreateDesc(BindFlag::ShaderResource|BindFlag::TransferDst, TextureDesc::PlainCube(32, 32, Format::R8_UNORM, 1, 6)), "whiteCubeArray")->CreateTextureView();
        }

        _pendingCompleteInitialization = true;
    }

    CommonResourceBox::~CommonResourceBox()
    {}

    void CommonResourceBox::CompleteInitialization(IThreadContext& threadContext)
    {
        if (!_pendingCompleteInitialization) return;
        std::vector<IResource*> blackResources {
            _black2DSRV->GetResource().get(),
            _black2DArraySRV->GetResource().get(),
            _black3DSRV->GetResource().get(),
            _blackCubeSRV->GetResource().get(),
            _blackCB.get(),

            _undefined2DUAV->GetResource().get(),
            _undefined2DArrayUAV->GetResource().get(),
            _undefined3DUAV->GetResource().get(),
            _undefinedCubeUAV->GetResource().get(),
            _undefinedBufferUAV->GetResource().get()
        };
        std::vector<IResource*> whiteResources {
            _white2DSRV->GetResource().get(),
            _white2DArraySRV->GetResource().get(),
            _white3DSRV->GetResource().get(),
            _whiteCubeSRV->GetResource().get()
        };
        
        if (_blackCubeArraySRV)
            blackResources.push_back(_blackCubeArraySRV->GetResource().get());
        if (_whiteCubeArraySRV)
            whiteResources.push_back(_whiteCubeArraySRV->GetResource().get());

        auto& metalContext = *Metal::DeviceContext::Get(threadContext);
        Metal::CompleteInitialization(metalContext, MakeIteratorRange(blackResources));
        Metal::CompleteInitialization(metalContext, MakeIteratorRange(whiteResources));
        
        // We also have to clear out data for the textures (since these can't be initialized
        // in the construction operation)
        // We might be able to do this with just a clear call on some APIs; but let's do it
        // it hard way, anyway
        size_t largest = 0;
        for (const auto& res:blackResources)
            largest = std::max(largest, (size_t)ByteCount(res->GetDesc()));
        for (const auto& res:whiteResources)
            largest = std::max(largest, (size_t)ByteCount(res->GetDesc()));

        {
            auto blackStaging = metalContext.MapTemporaryStorage(largest, BindFlag::TransferSrc);
            std::memset(blackStaging.GetData().begin(), 0, blackStaging.GetData().size());
            auto encoder = metalContext.BeginBlitEncoder();
            for (const auto& res:blackResources) {
                auto src = blackStaging.AsCopySource();
                auto dstDesc = res->GetDesc();
                if (dstDesc._type == ResourceDesc::Type::Texture) {
                    src._arrayLayerCount = ActualArrayLayerCount(dstDesc._textureDesc);
                    src._mipLevelCount = dstDesc._textureDesc._mipCount;
                }
                encoder.Copy(*res, src);
            }
        }
        {
            auto whiteStaging = metalContext.MapTemporaryStorage(largest, BindFlag::TransferSrc);
            std::memset(whiteStaging.GetData().begin(), 0xff, whiteStaging.GetData().size());
            auto encoder = metalContext.BeginBlitEncoder();
            for (const auto& res:whiteResources) {
                auto src = whiteStaging.AsCopySource();
                auto dstDesc = res->GetDesc();
                if (dstDesc._type == ResourceDesc::Type::Texture) {
                    src._arrayLayerCount = ActualArrayLayerCount(dstDesc._textureDesc);
                    src._mipLevelCount = dstDesc._textureDesc._mipCount;
                }
                encoder.Copy(*res, src);
            }
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
            case HistoryAcc: return "HistoryAcc";
            case MultisampleDepthPrev: return "MultisampleDepthPrev";
            case GBufferNormalPrev: return "GBufferNormalPrev";
            case ColorHDRPrev: return "ColorHDRPrev";
            case "SSReflection"_h: return "SSReflection";
            case "SSReflection"_h+1: return "SSReflectionPrev";
            case "SSRConfidence"_h: return "SSRConfidence";
            case "SSRConfidence"_h+1: return "SSRConfidencePrev";
            case "SSRConfidenceInt"_h: return "SSRConfidenceInt";
            case "SSRInt"_h: return "SSRInt";
            case "SSRDebug"_h: return "SSRDebug";
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
            else if ((hashValue - FONTTABLE) < 16) return std::make_pair("FONTTABLE", unsigned(hashValue - FONTTABLE));
            else return std::make_pair(nullptr, ~0u);
        }
    }
}}
