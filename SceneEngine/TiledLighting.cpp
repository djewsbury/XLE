// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "TiledLighting.h"
/*#include "SceneEngineUtils.h"
#include "MetricsBox.h"
#include "SceneParser.h"
#include "LightDesc.h"
#include "MetalStubs.h"*/

#include "../RenderCore/Techniques/CommonResources.h"
#include "../RenderCore/Techniques/Techniques.h"
#include "../RenderCore/Techniques/ParsingContext.h"
#include "../RenderCore/Techniques/DeferredShaderResource.h"
#include "../RenderCore/Techniques/PipelineOperators.h"
#include "../RenderCore/Techniques/Drawables.h"
#include "../RenderCore/Techniques/RenderPassUtils.h"
#include "../RenderCore/Techniques/RenderPass.h"
#include "../RenderCore/Metal/TextureView.h"
#include "../RenderCore/Metal/State.h"
#include "../RenderCore/Metal/Shader.h"
#include "../RenderCore/Metal/InputLayout.h"
#include "../RenderCore/Metal/DeviceContext.h"
#include "../RenderCore/Metal/DeviceContextImpl.h"
#include "../RenderCore/IThreadContext.h"
#include "../RenderCore/IDevice.h"
#include "../RenderCore/RenderUtils.h"
#include "../RenderCore/Format.h"
#include "../Assets/Assets.h"
#include "../Math/Matrix.h"
#include "../Math/Transformations.h"
#include "../ConsoleRig/ResourceBox.h"
#include "../ConsoleRig/Console.h"
#include "../xleres/FileList.h"

#include "../Utility/StringFormat.h"

namespace SceneEngine
{
    using namespace RenderCore;

    ///////////////////////////////////////////////////////////////////////////////////////////////

    class TileLightingResources
    {
    public:
        std::shared_ptr<RenderCore::IResourceView>  _debuggingTexture[3];
        std::shared_ptr<RenderCore::IResourceView>  _debuggingTextureSRV[3];

        std::shared_ptr<RenderCore::IResourceView>  _lightOutputTextureUAV;
        std::shared_ptr<RenderCore::IResourceView>  _temporaryProjectedLightsUAV;
        std::shared_ptr<RenderCore::IResourceView>  _lightOutputTextureSRV;

        std::shared_ptr<RenderCore::IResource> _resLocator0;
        std::shared_ptr<RenderCore::IResource> _resLocator1;
        std::shared_ptr<RenderCore::IResource> _resLocator2;
        std::shared_ptr<RenderCore::IResource> _lightOutputResource;
        std::shared_ptr<RenderCore::IResource> _temporaryProjectedLights;

        bool _pendingCompleteInitialization = true;

        void CompleteInitialization(IThreadContext& threadContext)
        {
            auto& metalContext = *RenderCore::Metal::DeviceContext::Get(threadContext);
            Metal::CompleteInitialization(metalContext, {_resLocator0.get(), _resLocator1.get(), _resLocator2.get(), _lightOutputResource.get(), _temporaryProjectedLights.get()});

            UINT clearValues[4] = { 0x3fffffff, 0x3fffffff, 0x3fffffff, 0x3fffffff };
            metalContext.Clear(*_debuggingTexture[0], clearValues);
            metalContext.Clear(*_debuggingTexture[1], clearValues);
            metalContext.Clear(*_debuggingTexture[2], clearValues);
            _pendingCompleteInitialization = false;
        }

        TileLightingResources(IDevice& device, unsigned width, unsigned height, unsigned bitDepth);
        ~TileLightingResources();
    };

    TileLightingResources& GetTileLightingResources(IDevice& device, unsigned width, unsigned height, unsigned bitDepth)
    {
        static TileLightingResources result { device, width, height, bitDepth };
        return result;
    }

    static ResourceDesc BuildTextureResourceDesc(BindFlag::BitField bindFlags, TextureDesc tDesc, const char name[])
    {
        return CreateDesc(bindFlags, 0, GPUAccess::Read|GPUAccess::Write, tDesc, name);
    }

    TileLightingResources::TileLightingResources(IDevice& device, unsigned width, unsigned height, unsigned bitDepth)
    {
        _resLocator0 = device.CreateResource(
            BuildTextureResourceDesc(
                BindFlag::UnorderedAccess | BindFlag::ShaderResource | BindFlag::TransferDst,
                TextureDesc::Plain2D(width, height, Format::R32_TYPELESS),
                "TileLighting0"));
        _resLocator1 = device.CreateResource(
            BuildTextureResourceDesc(
                BindFlag::UnorderedAccess | BindFlag::ShaderResource | BindFlag::TransferDst,
                TextureDesc::Plain2D(width, height, Format::R32_TYPELESS),
                "TileLighting1"));
        _resLocator2 = device.CreateResource(
            BuildTextureResourceDesc(
                BindFlag::UnorderedAccess | BindFlag::ShaderResource | BindFlag::TransferDst,
                TextureDesc::Plain2D(width, height, Format::R16_UINT),
                "TileLighting2"));

        _debuggingTextureSRV[0] = _resLocator0->CreateTextureView(BindFlag::ShaderResource, {Format::R32_FLOAT});
        _debuggingTextureSRV[1] = _resLocator1->CreateTextureView(BindFlag::ShaderResource, {Format::R32_FLOAT});
        _debuggingTextureSRV[2] = _resLocator2->CreateTextureView(BindFlag::ShaderResource, {Format::R16_UINT});

        _debuggingTexture[0] = _resLocator0->CreateTextureView(BindFlag::UnorderedAccess, {Format::R32_UINT});
        _debuggingTexture[1] = _resLocator1->CreateTextureView(BindFlag::UnorderedAccess, {Format::R32_UINT});
        _debuggingTexture[2] = _resLocator2->CreateTextureView(BindFlag::UnorderedAccess, {Format::R16_UINT});

            /////

        _lightOutputResource = device.CreateResource(
            BuildTextureResourceDesc(
                BindFlag::UnorderedAccess | BindFlag::ShaderResource, 
                TextureDesc::Plain2D(width, height, (bitDepth==16)?Format::R16G16B16A16_FLOAT:Format::R32G32B32A32_FLOAT),
                "TileLighting3"));
        _lightOutputTextureUAV = _lightOutputResource->CreateTextureView(BindFlag::UnorderedAccess);
        _lightOutputTextureSRV = _lightOutputResource->CreateTextureView(BindFlag::ShaderResource);

        auto bufferDesc = CreateDesc(
            BindFlag::UnorderedAccess, 0, GPUAccess::Read|GPUAccess::Write,
            LinearBufferDesc::Create(1024*24, 24),
            "temporary-projected-lights");
        _temporaryProjectedLights = device.CreateResource(bufferDesc);
        _temporaryProjectedLightsUAV = _temporaryProjectedLights->CreateBufferView(BindFlag::UnorderedAccess);
    }

    TileLightingResources::~TileLightingResources()
    {}

    void TiledLighting_DrawDebugging(
        RenderCore::Techniques::ParsingContext& parsingContext,
        const std::shared_ptr<RenderCore::Techniques::PipelinePool>& pipelinePool,
        TileLightingResources& tileLightingResources)
    {
        auto rpi = Techniques::RenderPassToPresentationTarget(parsingContext);

        UniformsStreamInterface usi;
        usi.BindResourceView(0, Hash64("LightOutput"));
        usi.BindResourceView(1, Hash64("DebuggingTextureMin"));
        usi.BindResourceView(2, Hash64("DebuggingTextureMax"));
        usi.BindResourceView(3, Hash64("DebuggingLightCountTexture"));
        usi.BindResourceView(4, Hash64("DigitsTexture"));

        UniformsStream us;
        const IResourceView* srvs[] { 
            tileLightingResources._lightOutputTextureSRV.get(), 
            tileLightingResources._debuggingTextureSRV[0].get(), tileLightingResources._debuggingTextureSRV[1].get(), tileLightingResources._debuggingTextureSRV[2].get(),
            ::Assets::Actualize<RenderCore::Techniques::DeferredShaderResource>("xleres/DefaultResources/digits.dds:T")->GetShaderResource().get()
        };
        us._resourceViews = MakeIteratorRange(srvs);

        // AttachmentBlendDesc blends[] { Techniques::CommonResourceBox::s_abStraightAlpha };
        // encoder.Bind(MakeIteratorRange(blends));

        auto& debuggingShader = *Techniques::CreateFullViewportOperator(
            pipelinePool,
            Techniques::FullViewportOperatorSubType::DisableDepth,
            "xleres/Deferred/debugging.pixel.hlsl:DepthsDebuggingTexture", {},
            "xleres/Deferred/tiled.pipeline:ComputeMain",
            rpi,
            usi)->Actualize();
        debuggingShader.Draw(parsingContext, us);
    }

    static float PowerForHalfRadius(float halfRadius, float powerFraction)
    {
        const float attenuationScalar = 1.f;
        return (attenuationScalar*(halfRadius*halfRadius)+1.f) * (1.0f / (1.f-powerFraction));
    }

    std::shared_ptr<RenderCore::IResourceView> TiledLighting_CalculateLighting(
        RenderCore::IThreadContext& threadContext, 
        RenderCore::Techniques::ParsingContext& parsingContext,
        const std::shared_ptr<RenderCore::Techniques::PipelinePool>& pipelinePool,
        RenderCore::IResourceView& depthsSRV, RenderCore::IResourceView& normalsSRV,
		RenderCore::IResourceView& metricBufferUAV)
    {
        const bool doTiledRenderingTest             = Tweakable("DoTileRenderingTest", true);
        const bool doClusteredRenderingTest         = Tweakable("TileClustering", false);
        const bool tiledBeams                       = Tweakable("TiledBeams", false);

        const unsigned maxLightCount                = 1024;
        const unsigned tileLightCount               = std::min(Tweakable("TileLightCount", 512), int(maxLightCount));
        const bool pause                            = Tweakable("Pause", false);

        if (doTiledRenderingTest && !tiledBeams) {
            CATCH_ASSETS_BEGIN
				auto tDesc = depthsSRV.GetResource()->GetDesc();
                unsigned width = tDesc._textureDesc._width, height = tDesc._textureDesc._height, sampleCount = tDesc._textureDesc._samples._sampleCount;

                auto& device = *threadContext.GetDevice();
                auto& metalContext = *Metal::DeviceContext::Get(threadContext);
                auto& tileLightingResources = GetTileLightingResources(device, width, height, 16);

                if (tileLightingResources._pendingCompleteInitialization)
                    tileLightingResources.CompleteInitialization(threadContext);

                auto worldToView = InvertOrthonormalTransform(
                    parsingContext.GetProjectionDesc()._cameraToWorld);
                auto coordinateFlipMatrix = Float4x4(
                    1.f, 0.f, 0.f, 0.f,
                    0.f, 0.f, -1.f, 0.f,
                    0.f, 1.f, 0.f, 0.f,
                    0.f, 0.f, 0.f, 1.f);
                worldToView = Combine(worldToView, coordinateFlipMatrix);

                struct LightStruct
                {
                    Float3  _worldSpacePosition;
                    float   _radius;
                    Float3  _colour;
                    float   _power;

                    LightStruct(const Float3& worldSpacePosition, float radius, const Float3& colour, float power) 
                        : _worldSpacePosition(worldSpacePosition), _radius(radius), _colour(colour), _power(power) {}
                };

                static float startingAngle = 0.f;
                std::shared_ptr<IResourceView> lightBufferResourceView;
                {
                    static Float3 baseLightPosition = Float3(0.f, 0.f, 0.f);

                    auto mappedStorage = metalContext.MapTemporaryStorage((tileLightCount+1) * sizeof(LightStruct), BindFlag::UnorderedAccess);
                    auto lightBufferResource = mappedStorage.GetResource();
                    auto beginAndEnd = mappedStorage.GetBeginAndEndInResource();
                    auto* dstLight = (LightStruct*)mappedStorage.GetData().begin();

                    for (unsigned c=0; c<tileLightCount; ++c) {
                        const float X = startingAngle + c / float(tileLightCount) * gPI * 2.f;
                        const float Y = 3.7397f * startingAngle + .7234f * c / float(tileLightCount) * gPI * 2.f;
                        const float Z = 13.8267f * startingAngle + 0.27234f * c / float(tileLightCount) * gPI * 2.f;
                        const float radius = 20.f + 10.f * XlSin(Z);
                        *dstLight++ = LightStruct(
                            baseLightPosition + Float3(50.f * XlCos(X), 2.f * c, 50.f * XlSin(Y) * XlCos(Y)), 
                            radius, .25f * Float3(.65f + .35f * XlSin(Y), .65f + .35f * XlCos(Y), .65f + .35f * XlCos(X)),
                            PowerForHalfRadius(radius, 0.05f));
                    }
                    if (!pause) {
                        startingAngle += 0.05f;
                    }

                        // add dummy light
                    *dstLight++ = LightStruct(Float3(0.f, 0.f, 0.f), 0.f, Float3(0.f, 0.f, 0.f), 0.f);
                    assert(beginAndEnd.second > beginAndEnd.first);
                    lightBufferResourceView = lightBufferResource->CreateBufferView(BindFlag::ConstantBuffer, beginAndEnd.first, beginAndEnd.second-beginAndEnd.first);
                }

                auto& projDesc = parsingContext.GetProjectionDesc();
                Float2 fov;
                fov[1] = projDesc._verticalFov;
                fov[0] = 2.f * XlATan(projDesc._aspectRatio * XlTan(projDesc._verticalFov  * .5f));
                
                const unsigned TileWidth = 8, TileHeight = 8;
                struct LightCulling
                {
					unsigned	_lightCount;
					unsigned    _groupCounts[2];
                    unsigned    _dummy0;
                    Float4x4    _worldToView;
                    Float2      _fov;
                    int         _dummy1[2];
                } lightCulling = { 
                    tileLightCount, { (width + TileWidth - 1) / TileWidth, (height + TileHeight + 1) / TileHeight }, 0u,
                    worldToView, 
                    fov, { 0, 0 }
                };

                UniformsStreamInterface usi;
                usi.BindResourceView(0, Hash64("InputLightList"));
                usi.BindResourceView(1, Hash64("DepthTexture"));
                usi.BindResourceView(2, Hash64("GBuffer_Normals"));

                usi.BindResourceView(3, Hash64("LightOutput"));
                usi.BindResourceView(4, Hash64("ProjectedLightList"));
                usi.BindResourceView(5, Hash64("MetricsObject"));
                usi.BindResourceView(6, Hash64("DebuggingTextureMin"));
                usi.BindResourceView(7, Hash64("DebuggingTextureMax"));
                usi.BindResourceView(8, Hash64("DebuggingLightCountTexture"));

                usi.BindImmediateData(0, Hash64("LightCulling"));

                UniformsStream us;
                UniformsStream::ImmediateData immData[] { MakeOpaqueIteratorRange(lightCulling) };
                const IResourceView* resViews[] { 
                    lightBufferResourceView.get(), &depthsSRV, &normalsSRV, 
                    tileLightingResources._lightOutputTextureUAV.get(), tileLightingResources._temporaryProjectedLightsUAV.get(),
                    &metricBufferUAV,
                    tileLightingResources._debuggingTexture[0].get(), tileLightingResources._debuggingTexture[1].get(), tileLightingResources._debuggingTexture[2].get() };
                us._immediateData = MakeIteratorRange(immData);
                us._resourceViews = MakeIteratorRange(resViews);

                auto& prepareLights = *Techniques::CreateComputeOperator(
                    pipelinePool, 
                    "xleres/Deferred/tiled.compute.hlsl:PrepareLights", {},
                    "xleres/Deferred/tiled.pipeline:ComputeMain",
                    usi)->Actualize();
                prepareLights.Dispatch(
                    parsingContext,
                    (tileLightCount + 256 - 1) / 256, 1, 1,
                    us);

                {
                    VkMemoryBarrier barrier = { VK_STRUCTURE_TYPE_MEMORY_BARRIER };
                    barrier.pNext = nullptr;
                    barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
                    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
                    vkCmdPipelineBarrier(
                        metalContext.GetActiveCommandList().GetUnderlying().get(),
                        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                        0,
                        1, &barrier,
                        0, nullptr,
                        0, nullptr);
                }

                ParameterBox definesTable;
                definesTable.SetParameter("MSAA_SAMPLES", sampleCount);
                definesTable.SetParameter("_METRICS", 1);
                if (doClusteredRenderingTest) {
                    auto& clusteredMain = *Techniques::CreateComputeOperator(
                        pipelinePool, 
                        "xleres/Deferred/clustered.compute.hlsl:main", definesTable,
                        "xleres/Deferred/tiled.pipeline:ComputeMain",
                        usi)->Actualize();
                    clusteredMain.Dispatch(
                        parsingContext,
                        lightCulling._groupCounts[0], lightCulling._groupCounts[1], 1,
                        us);
                } else {
                    auto& clusteredMain = *Techniques::CreateComputeOperator(
                        pipelinePool, 
                        "xleres/Deferred/tiled.compute.hlsl:main", definesTable,
                        "xleres/Deferred/tiled.pipeline:ComputeMain",
                        usi)->Actualize();
                    clusteredMain.Dispatch(
                        parsingContext,
                        lightCulling._groupCounts[0], lightCulling._groupCounts[1], 1,
                        us);
                }

                    //
                    //      inject point light sources into fog
                    //          (currently too expensive to use practically)
                    //
                #if 0
                    if (!processedResult.empty() && processedResult[0]._projectConstantBuffer && doVolumetricFog && fogPointLightSources) {
                        auto& fogRes = FindCachedBox<VolumetricFogResources>(VolumetricFogResources::Desc(processedResult[0]._frustumCount));
                        auto& fogShaders = FindCachedBoxDep<VolumetricFogShaders>(VolumetricFogShaders::Desc(1, useMsaaSamplers, false));
                        auto& airLight = FindCachedBox<AirLightResources>(AirLightResources::Desc());
                        
                        context->BindCS(MakeResourceList(3, fogRes._inscatterPointLightsValuesUnorderedAccess));
                        context->BindCS(MakeResourceList(13, Assets::Legacy::GetAssetDep<Techniques::DeferredShaderResource>("xleres/DefaultResources/balanced_noise.dds:LT").GetShaderResource()));
                        context->BindCS(MakeResourceList(1, airLight._lookupShaderResource));
                        context->Bind(*fogShaders._injectPointLightSources);
                        context->Dispatch(160/10, 90/10, 128/8);
                    }
                #endif

                /*if (Tweakable("TiledLightingDebugging", false) && !tiledBeams) {
                    parsingContext._pendingOverlays.push_back(
                        std::bind(&TiledLighting_DrawDebugging, std::placeholders::_1, std::placeholders::_2, tileLightingResources));
                }*/

                return tileLightingResources._lightOutputTextureSRV;
            CATCH_ASSETS_END(parsingContext)
        }

        return nullptr;
    }

    void TiledLighting_RenderBeamsDebugging(
        RenderCore::IThreadContext& threadContext, 
        RenderCore::Techniques::ParsingContext& parsingContext,
        const std::shared_ptr<RenderCore::Techniques::PipelinePool>& pool,
        bool active, unsigned mainViewportWidth, unsigned mainViewportHeight, 
        unsigned techniqueIndex)
    {
        static bool lastActive = false;
        if (active) {
            CATCH_ASSETS_BEGIN
                static Techniques::GlobalTransformConstants savedGlobalTransform;
                if (lastActive != active) {
                    savedGlobalTransform = Techniques::BuildGlobalTransformConstants(parsingContext.GetProjectionDesc());
                }

                auto& tileLightingResources = GetTileLightingResources(*threadContext.GetDevice(), mainViewportWidth, mainViewportHeight, 16);
                const bool isShadowsPass = false;
                auto& pipelineLayoutAsset = *::Assets::Actualize<Techniques::CompiledPipelineLayoutAsset>(
                    pool->GetDevice(),
                    "xleres/Deferred/tiled.pipeline:BeamsDebugging");
                auto& debuggingShader = *::Assets::Actualize<Metal::ShaderProgram>(
                    pipelineLayoutAsset.GetPipelineLayout(),
                    "xleres/Deferred/debugging/beams.vertex.hlsl:main:vs_*", 
                    "xleres/Deferred/debugging/beams.geo.hlsl:main:gs_*", 
                    "xleres/Deferred/debugging/beams.pixel.hlsl:main:ps_*",
                    isShadowsPass?"SHADOWS=1;SHADOW_CASCADE_MODE=1":"");    // hack -- SHADOW_CASCADE_MODE set explicitly here

				UniformsStreamInterface usi;
				usi.BindImmediateData(0, Hash64("RecordedTransform"));
                usi.BindImmediateData(1, Hash64("GlobalTransform"));
                usi.BindImmediateData(2, Hash64("Parameters"));
                usi.BindResourceView(0, Hash64("DebuggingTextureMin"));
                usi.BindResourceView(1, Hash64("DebuggingTextureMax"));

                Metal::BoundUniforms uniforms(debuggingShader, usi);
                
                const unsigned TileWidth = 8, TileHeight = 8;
                uint32 globals[4] = {   (mainViewportWidth + TileWidth - 1) / TileWidth, 
                                        (mainViewportHeight + TileHeight + 1) / TileHeight, 
                                        0, 0 };
                auto currentGlobalTransform = Techniques::BuildGlobalTransformConstants(parsingContext.GetProjectionDesc());
                UniformsStream::ImmediateData immData[] { MakeOpaqueIteratorRange(savedGlobalTransform), MakeOpaqueIteratorRange(currentGlobalTransform), MakeOpaqueIteratorRange(globals) };
                IResourceView* resViews[] { tileLightingResources._debuggingTextureSRV[0].get(), tileLightingResources._debuggingTextureSRV[1].get() };
                UniformsStream us;
                us._immediateData = MakeIteratorRange(immData);
                us._resourceViews = MakeIteratorRange(resViews);

                auto& metalContext = *Metal::DeviceContext::Get(threadContext);
                auto encoder = metalContext.BeginGraphicsEncoder_ProgressivePipeline(pipelineLayoutAsset.GetPipelineLayout());

                encoder.Bind(Techniques::CommonResourceBox::s_dsReadWrite);
                encoder.Bind({}, Topology::PointList);

                if (!isShadowsPass && Tweakable("TiledBeamsTransparent", false)) {
                    AttachmentBlendDesc abd[] {Techniques::CommonResourceBox::s_abStraightAlpha};
                    encoder.Bind(MakeIteratorRange(abd));
                    auto& predepth = *::Assets::Actualize<Metal::ShaderProgram>(
                        pipelineLayoutAsset.GetPipelineLayout(),
                        "xleres/Deferred/debugging/beams.vertex.hlsl:main:vs_*", 
                        "xleres/Deferred/debugging/beams.geo.hlsl:main:gs_*", 
                        "xleres/Deferred/debugging/beams.pixel.hlsl:predepth:ps_*",
                        "");
                    encoder.Bind(predepth);
                    encoder.Draw(globals[0]*globals[1]);
                } else {
                    AttachmentBlendDesc abd[] {Techniques::CommonResourceBox::s_abOpaque};
                    encoder.Bind(MakeIteratorRange(abd));
                }

                encoder.Bind(debuggingShader);
                encoder.Draw(globals[0]*globals[1]);

                if (!isShadowsPass) {
                    encoder.Bind(*::Assets::Actualize<Metal::ShaderProgram>(
                        pipelineLayoutAsset.GetPipelineLayout(),
                        "xleres/Deferred/debugging/beams.vertex.hlsl:main:vs_*", 
                        "xleres/Deferred/debugging/beams.geo.hlsl:Outlines:gs_*", 
                        "xleres/Deferred/debugging/beams.pixel.hlsl:main:ps_*",
                        ""));
                    encoder.Draw(globals[0]*globals[1]);
                }
            CATCH_ASSETS_END(parsingContext)
        }

        lastActive = active;
    }


}