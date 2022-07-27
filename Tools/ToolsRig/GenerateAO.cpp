// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "GenerateAO.h"
#include "../../BufferUploads/ResourceLocator.h"
#include "../../BufferUploads/IBufferUploads.h"
#include "../../BufferUploads/DataPacket.h"
#include "../../SceneEngine/LightInternal.h"    // for shadow projection constants;
#include "../../SceneEngine/SceneEngineUtils.h"
#include "../../SceneEngine/LightingParser.h"   // for SetFrameGlobalStates
#include "../../FixedFunctionModel/ModelRunTime.h"
#include "../../FixedFunctionModel/SharedStateSet.h"
#include "../../RenderCore/GeoProc/MeshDatabase.h"
#include "../../RenderCore/Assets/ModelImmutableData.h"
#include "../../RenderCore/Assets/ModelScaffold.h"
#include "../../RenderCore/Assets/MaterialScaffold.h"
#include "../../RenderCore/Assets/Services.h"
#include "../../RenderCore/Metal/TextureView.h"
#include "../../RenderCore/Metal/DeviceContext.h"
#include "../../RenderCore/Metal/DeviceContextImpl.h"
#include "../../RenderCore/Metal/Shader.h"
#include "../../RenderCore/Techniques/Techniques.h"
#include "../../RenderCore/Techniques/CommonResources.h"
#include "../../RenderCore/Techniques/CommonBindings.h"
#include "../../RenderCore/Techniques/ParsingContext.h"
#include "../../RenderCore/Techniques/TechniqueUtils.h"
#include "../../RenderCore/Techniques/Services.h"
#include "../../RenderCore/Format.h"
#include "../../Assets/AssetServices.h"
#include "../../Assets/CompileAndAsyncManager.h"
#include "../../Assets/IFileSystem.h"
#include "../../Assets/Assets.h"
#include "../../Assets/CompilationThread.h"
#include "../../Assets/IntermediateAssets.h"
#include "../../OSServices/RawFS.h"
#include "../../Utility/Streams/PathUtils.h"
#include "../../Utility/StringFormat.h"
#include "../../Math/Transformations.h"
#include "../../Math/ProjectionMath.h"

#include "../../Assets/ChunkFile.h"
#include "../../OSServices/AttachableLibrary.h"
#include "../../OSServices/Log.h"
#include "../../ConsoleRig/GlobalServices.h"
#include "../../RenderCore/Assets/AssetUtils.h"

#include <queue>

// #define GEN_AO_DEBUG
#if defined(GEN_AO_DEBUG)
    #include "../../Utility/Conversion.h"
    #include "../../OSServices/WinAPI/IncludeWindows.h"
    #include "../../Foreign/DirectXTex/DirectXTex/DirectXTex.h"
    #include "../../RenderCore/DX11/Metal/DX11Utils.h"

    static void SaveTexture(
        const ::Assets::ResChar destinationFile[],
        BufferUploads::DataPacket& pkt, unsigned subresource, 
        UInt2 dims, RenderCore::Metal::NativeFormat::Enum format)
    {
            // using DirectXTex to save to disk as a TGA file...
        auto rowPitch = pkt.GetPitches(subresource)._rowPitch;
        DirectX::Image image {
            dims[0], dims[1],
            RenderCore::Metal::AsDXGIFormat(format),
            rowPitch, rowPitch * dims[1],
            (uint8_t*)pkt.GetData(subresource) };
        auto fn = Conversion::Convert<std::wstring>(std::string(destinationFile));

        const GUID GUID_ContainerFormatTiff = 
            { 0x163bcc30, 0xe2e9, 0x4f0b, { 0x96, 0x1d, 0xa3, 0xe9, 0xfd, 0xb7, 0x88, 0xa3 }};
        auto hresult = DirectX::SaveToWICFile(
            image, DirectX::WIC_FLAGS_NONE,
            GUID_ContainerFormatTiff,
            fn.c_str());
        (void)hresult;
        assert(SUCCEEDED(hresult));
    }
#endif

namespace ToolsRig
{
    using namespace RenderCore;
    using ModelScaffold = RenderCore::Assets::ModelScaffold;
    using MaterialScaffold = RenderCore::Assets::MaterialScaffold;
    using ModelRenderer = FixedFunctionModel::ModelRenderer;
    using SharedStateSet = FixedFunctionModel::SharedStateSet;
    using namespace RenderCore::Assets::GeoProc;

    class AoGen::Pimpl
    {
    public:
        Desc        _settings;

        using ResLocator = intrusive_ptr<BufferUploads::ResourceLocator>;
        using SRV = Metal::ShaderResourceView;
        using DSV = Metal::DepthStencilView;
        using UAV = Metal::UnorderedAccessView;
        ResLocator  _cubeLocator;
        DSV         _cubeDSV;
        SRV         _cubeSRV;

        ResLocator  _miniLocator;
        UAV         _miniUAV;

        const Metal::ComputeShader* _stepDownShader;
        ::Assets::DependencyValidation _depVal;
    };

    static void SetupCubeMapShadowProjection(
        Metal::DeviceContext& metalContext,
        Techniques::ParsingContext& parserContext,
        float nearClip, float farClip);

    float AoGen::CalculateSkyDomeOcclusion(
        RenderCore::IThreadContext& threadContext,
        const ModelRenderer& renderer,
        FixedFunctionModel::SharedStateSet& sharedStates,
        const FixedFunctionModel::MeshToModel& meshToModel,
        const Float3& samplePoint)
    {
            //
            // We want to calculate a rough approximation for the amount of
            // the sky dome that is occluded at the given sample point.
            //
            // We can use this value to occlude indirect light (assuming that
            // geometry that occludes the sky dome will also occlude indirect
            // light -- which should be reasonable for indirect sources
            // out side of the model).
            //
            // But we can also use this to occlude direct light from the sky
            // dome.
            //
            // To generate the value, we need to render a half-cube-map (we
            // are only interested in illumination coming from above the equator)
            // of depths around the sample point.
            //
            // For each texel in the cube map, if there is a value in the depth
            // texture, then there must be a nearby occluder in that direction. 
            // We weight the texel's occlusion by it's solid angle and sum up
            // all of the occlusion.
            //

            //
            // To render the object, we can use the normal ModelRenderer object.
            // The ModelRenderer might be a bit heavy-weight (we could alternatively
            // just parse through the ModelScaffold (for example, like 
            // MaterialSceneParser::DrawModel)
            // But the ModelRenderer is perhaps the most future-proof approach.
            //
            // We can re-use the shadow rendering path for this (because it works
            // in the same way; with the geometry shader splitting the geometry
            // between viewports, depth-only output & alpha test support)
            //  -- and, after all, we are rendering a kind of shadow
            //

        auto metalContextPtr = Metal::DeviceContext::Get(threadContext);
        auto& metalContext = *metalContextPtr;
        SceneEngine::SavedTargets savedTargets(metalContext);

        metalContext.Clear(_pimpl->_cubeDSV, Metal::DeviceContext::ClearFilter::Depth|Metal::DeviceContext::ClearFilter::Stencil, 1.f, 0u);
        metalContext.Bind(ResourceList<Metal::RenderTargetView, 0>(), &_pimpl->_cubeDSV);

            // configure rendering for the shadow shader
        const auto& settings = _pimpl->_settings;
        const auto& commonRes = Techniques::CommonResources();
        ViewportDesc viewport(
            0.f, 0.f, float(settings._renderResolution), float(settings._renderResolution), 
            0.f, 1.f);
        metalContext.Bind(viewport);
        metalContext.Bind(commonRes._defaultRasterizer);

        Techniques::TechniqueContext techniqueContext;
        Techniques::ParsingContext parserContext(techniqueContext);
		parserContext.GetSubframeShaderSelectors().SetParameter((const utf8*)"SHADOW_CASCADE_MODE", 1u);            // arbitrary projection mode
        parserContext.GetSubframeShaderSelectors().SetParameter((const utf8*)"FRUSTUM_FILTER", 31u);                // enable writing to 5 frustums
        parserContext.GetSubframeShaderSelectors().SetParameter((const utf8*)"VSOUT_HAS_SHADOW_PROJECTION_COUNT", 5u);

        SceneEngine::SetFrameGlobalStates(metalContext);

            // We shouldn't need to fill in the "global transform" constant buffer
            // The model renderer will manage local transform constant buffers internally,
            // we should only need to set the shadow projection constants.
        
        SetupCubeMapShadowProjection(metalContext, parserContext, settings._minDistance, settings._maxDistance);

            // Render the model onto our cube map surface
        {
			// auto captureMarker = sharedStates.CaptureState(threadContext, parserContext.GetRenderStateDelegate(), {});
            TRY {
                FixedFunctionModel::DelayedDrawCallSet delayedDraws(typeid(ModelRenderer).hash_code());
                renderer.Prepare(
                    delayedDraws, sharedStates, AsFloat4x4(Float3(-samplePoint)), meshToModel);

                ModelRenderer::Sort(delayedDraws);
                for (unsigned c=0; c<unsigned(FixedFunctionModel::DelayStep::Max); ++c)
                    ModelRenderer::RenderPrepared(
                        FixedFunctionModel::ModelRendererContext(
                            threadContext, parserContext, Techniques::TechniqueIndex::ShadowGen),
                        sharedStates, delayedDraws, FixedFunctionModel::DelayStep(c));

            } CATCH(...) {
                savedTargets.ResetToOldTargets(metalContext);
                throw;
            } CATCH_END
            savedTargets.ResetToOldTargets(metalContext);
        }

            //
            // Now we have to read-back the results from the cube map,
            // and weight by the solid angle. Let's do this with a 
            // compute shader. We'll split each face into 16 squares, and
            // assign a separate thread to each. The output will be a 4x4x5
            // texture of floats
            //

        metalContext.GetNumericUniforms(ShaderStage::Compute).Bind(MakeResourceList(_pimpl->_cubeSRV));
        metalContext.GetNumericUniforms(ShaderStage::Compute).Bind(MakeResourceList(_pimpl->_miniUAV));

        // metalContext.Bind(*_pimpl->_stepDownShader);
        metalContext.Bind(
            ::Assets::Legacy::GetAssetDep<Metal::ComputeShader>(
                "xleres/toolshelper/aogenprocess.hlsl:CubeMapStepDown:cs_*"));
        metalContext.Dispatch(1u);

		metalContext.GetNumericUniforms(ShaderStage::Compute).Reset();
        // metalContext.UnbindCS<Metal::ShaderResourceView>(0, 1);
        // metalContext.UnbindCS<Metal::UnorderedAccessView>(0, 1);

        auto& bufferUploads = RenderCore::Techniques::Services::GetBufferUploads();
        auto readback = bufferUploads.Resource_ReadBack(*_pimpl->_miniLocator);

        #if defined(GEN_AO_DEBUG)
            {
                static unsigned CubeMapsWritten = 0;
                CreateDirectoryRecursive("aogen_debug");

                BufferUploads::ResourceLocator faceRes = Metal::ExtractResource<ID3D::Resource>(_pimpl->_cubeSRV.GetUnderlying());
                const auto fDesc = BufferUploads::ExtractDesc(*faceRes.GetUnderlying())._textureDesc;
                auto fReadback = bufferUploads.Resource_ReadBack(faceRes);
                for (unsigned c=0; c<5; ++c)
                    SaveTexture(
                        StringMeld<256>() << "aogen_debug/" << CubeMapsWritten << "_face_" << c << ".tif",
                        *fReadback, c, 
                        UInt2(fDesc._width, fDesc._height), Metal::NativeFormat::R32_FLOAT);

                const auto sdDesc = BufferUploads::ExtractDesc(*_pimpl->_miniLocator->GetUnderlying())._textureDesc;
                for (unsigned c=0; c<5; ++c)
                    SaveTexture(
                        StringMeld<256>() << "aogen_debug/" << CubeMapsWritten << "_stepdown_" << c << ".tif",
                        *readback, c, 
                        UInt2(sdDesc._width, sdDesc._height), (RenderCore::Metal::NativeFormat::Enum)sdDesc._nativePixelFormat);
                ++CubeMapsWritten;
            }
        #endif

            // Note that we're currently using the full face for the side faces
            // (as opposed to just half of the face, which would correspond to
            // a hemisphere)
            // We could ignore some of the texels in "readback" to only use
            // the top half of the side faces.
        const float solidAngleFace = 4.f * gPI / 6.f;
        const float solidAngleTotal = 5.f * solidAngleFace;
        float occlusionTotal = 0.f;
        for (unsigned f=0; f<5; ++f) {
            auto pitches = readback->GetPitches({f});
            auto* d = (float*)readback->GetData({f});
            for (unsigned y=0; y<4; ++y)
                for (unsigned x=0; x<4; ++x)
                    occlusionTotal += PtrAdd(d, y*pitches._rowPitch)[x];
        }

            // Our final result is a proportion of the sampled sphere that is 
            // occluded.
        return occlusionTotal / solidAngleTotal;
    }

    AoGen::AoGen(const Desc& settings)
    {
            // _renderResolution must be a multiple of 4 -- this is required
            // for the step-down compute shader to work correctly.
        if ((settings._renderResolution%4)!=0)
            Throw(::Exceptions::BasicLabel("Working texture in AOGen must have dimensions that are a multiple of 4"));

        _pimpl = std::make_unique<Pimpl>();
        _pimpl->_settings = settings;

        const unsigned cubeFaces = 5;

        auto& bufferUploads = RenderCore::Techniques::Services::GetBufferUploads();

        #if defined(GEN_AO_DEBUG)
            auto typelessFormat = Format::R32_TYPELESS;
            auto dsvFormat = Format::D32_FLOAT;
            auto srvFormat = Format::R32_FLOAT;
        #else
            auto typelessFormat = Format::R24G8_TYPELESS;
            auto dsvFormat = Format::D24_UNORM_S8_UINT;
            auto srvFormat = Format::R24_UNORM_X8_TYPELESS;
        #endif

        _pimpl->_cubeLocator = bufferUploads.Transaction_Immediate(
            CreateDesc( 
                BindFlag::DepthStencil | BindFlag::ShaderResource,
                TextureDesc::Plain2D(
                    settings._renderResolution, settings._renderResolution, 
                    typelessFormat, 1, cubeFaces),
                "AoGen"));
        _pimpl->_cubeDSV = Metal::DepthStencilView(_pimpl->_cubeLocator->GetUnderlying(), {dsvFormat});
        _pimpl->_cubeSRV = Metal::ShaderResourceView(_pimpl->_cubeLocator->GetUnderlying(), {srvFormat});

        _pimpl->_miniLocator = bufferUploads.Transaction_Immediate(
            CreateDesc( 
                BindFlag::UnorderedAccess,
                TextureDesc::Plain2D(4, 4, Format::R32_FLOAT, 1, cubeFaces),
                "AoGenMini"));
        _pimpl->_miniUAV = Metal::UnorderedAccessView(_pimpl->_miniLocator->GetUnderlying());

        // _pimpl->_stepDownShader = &::Assets::Legacy::GetAssetDep<Metal::ComputeShader>(
        //     "xleres/toolshelper/aogenprocess.hlsl:CubeMapStepDown:cs_*");

        // _pimpl->_depVal = ::Assets::GetDepValSys().Make();
        // _pimpl->_depVal.RegisterDependency(_pimpl->_stepDownShader->GetDependencyValidation());
    }

    auto AoGen::GetSettings() const -> const Desc& { return _pimpl->_settings; }

    AoGen::~AoGen() {}

    const ::Assets::DependencyValidation& AoGen::GetDependencyValidation() const
    {
        return _pimpl->_depVal;
    }

    std::vector<unsigned> GetGeoList(const RenderCore::Assets::ModelImmutableData& immData)
    {
        std::vector<unsigned> result;
        const auto& cmdStream = immData._visualScene;
        for (unsigned c = 0; c < cmdStream.GetGeoCallCount(); ++c) {
            const auto& geoCall = cmdStream.GetGeoCall(c);
            result.push_back(geoCall._geoId);
        }

            // remove duplicates
        std::sort(result.begin(), result.end());
        auto newEnd = std::unique(result.begin(), result.end());
        result.erase(newEnd, result.end());

        return std::move(result);
    }
    
    static void WriteSupplementalModel(
        StringSection<::Assets::ResChar> destinationFile,
        IteratorRange<std::pair<unsigned, MeshDatabase>*> meshes,
        const NativeVBSettings& nativeVbSettings);

    static unsigned FindUniqueTransform(
        const RenderCore::Assets::ModelScaffold& model,
        unsigned geoId)
    {
        unsigned result = ~0u;
        const auto& cmdStream = model.CommandStream();
        for (unsigned c=0; c<cmdStream.GetGeoCallCount(); ++c) {
            const auto& geoCall = cmdStream.GetGeoCall(c);
            if (geoCall._geoId == geoId) {
                if (result != ~0u) return ~0u;  // not unique;
                result = geoCall._transformMarker;
            }
        }
        return result;
    }

    void CalculateVertexAO(
        RenderCore::IThreadContext& threadContext,
        StringSection<::Assets::ResChar> destinationFile,
        AoGen& gen,
        const RenderCore::Assets::ModelScaffold& model,
        const RenderCore::Assets::MaterialScaffold& material,
        const ::Assets::DirectorySearchRules* searchRules)
    {
        //
        // For each vertex in the given model, calculate an ambient 
        // occlusion value.
        //
        // Note that we might have problems here when a single mesh is
        // repeated multiple times in the same model (eg, reflected
        // left and right version). Since the ao values is tied to the
        // vertex, the system will attempt to share the same value for
        // each copy (but only one copy will be correct).
        //
        // Also note that there might be problems where there are multiple
        // vertices at the same position. Ideally we want to do a single
        // sample in these cases (even though we still need to store 
        // separate values) because it would look strange if we saw large
        // divergence in AO values at one point.
        //
        // We could push the sample points some distance long the vertex
        // normals. We still want small variations in the surface to have an
        // effect on the AO -- so we can't go to far. We also don't want to 
        // push the same point into the inside of some other structure.
        // Some geometry will be double-sided -- but there should still be
        // a normal to push along.
        //
        // Also note that sometimes we may be generating the AO for vertices
        // that are actually invisible (eg, pixels at that vertex are rejected
        // by alpha testing). In those cases the vertex AO is only used by 
        // interpolation (and is likely to be close to 1.f, anyway).
        //

        // Go through the geo calls in the input scaffold, and find the 
        // mesh instances. We need to know the vertex positions and normals
        // in those meshes.

        const auto& settings = gen.GetSettings();
        const float duplicatesThreshold = settings._duplicatesThreshold;
        const float normalsPushOut      = settings._samplePushOut;
        const float powerExaggerate     = settings._powerExaggerate;

            // Setup the rendering objects we'll need
            // We're going to be use a ModelRenderer to do the rendering
            // for calculating the AO -- so we have to create that now.
        SharedStateSet sharedStates(RenderCore::Assets::Services::GetTechniqueConfigDirs());
        std::unique_ptr<ModelRenderer> renderer;

            // we need to stall while pending...
        auto& asyncMan = ::Assets::Services::GetAsyncMan();
        auto& bufferUploads = RenderCore::Techniques::Services::GetBufferUploads();
        for (;;) {
            if (!renderer)
                renderer = std::make_unique<ModelRenderer>(
                    model, material, ModelRenderer::Supplements(), 
                    sharedStates, searchRules);

            auto state = renderer->TryResolve();
            if (state == ::Assets::AssetState::Ready) break;
            if (state == ::Assets::AssetState::Invalid)
                Throw(::Exceptions::BasicLabel("Got invalid asset while performing AO gen"));
            
                // stall...!
            asyncMan.Update();
            bufferUploads.Update(threadContext, false);     // (note -- render state can get reset here)
        }

        FixedFunctionModel::MeshToModel meshToModel(model);

            // We're going to be reading the vertex data directly from the
            // file on disk. We'll use a memory mapped file to access that
            // so we need to open that now...
        std::vector<std::pair<unsigned, MeshDatabase>> meshes;
        
        {
			auto file = model.OpenLargeBlocks();
			auto largeBlocksOffset = file->TellP();

            const auto& immData = model.ImmutableData();
            auto geoIndicies = GetGeoList(immData);
            for (auto c=geoIndicies.cbegin(); c!=geoIndicies.cend(); ++c) {
                auto& rawGeo = immData._geos[*c];
                if (rawGeo._drawCalls.empty() || rawGeo._vb._size==0 || rawGeo._vb._ia._vertexStride==0) continue;

                auto transformMarker = FindUniqueTransform(model, *c);
                if (transformMarker == ~0u) continue;   // skip duplicates

                Float4x4 toModel = Identity<Float4x4>();
                if (transformMarker < meshToModel._skeletonOutputCount)
                    toModel = meshToModel._skeletonOutput[transformMarker];

                auto vbStart = largeBlocksOffset + rawGeo._vb._offset;
                auto vbEnd = vbStart + rawGeo._vb._size;
                auto vertexCount = rawGeo._vb._size / rawGeo._vb._ia._vertexStride;

				std::vector<uint8> data(vbEnd - vbStart);
				file->Seek(vbStart);
				file->Read(data.data(), vbEnd - vbStart);

                MeshDatabase mesh;

                    // Material & index buffer are irrelevant.
                    // Find the normal and position streams, and add to
                    // our mesh database adapter.
                const auto& vbIA = rawGeo._vb._ia;
                for (unsigned e=0; e<unsigned(vbIA._elements.size()); ++e) {
                    const auto& ele = vbIA._elements[e];
                    if (    (XlEqStringI(ele._semanticName, "POSITION") && ele._semanticIndex == 0)
                        ||  (XlEqStringI(ele._semanticName, "NORMAL") && ele._semanticIndex == 0)) {

                        auto rawSource = CreateRawDataSource(
                            PtrAdd(data.data(), ele._alignedByteOffset),
                            AsPointer(data.end()),
                            vertexCount, vbIA._vertexStride, 
							ele._nativeFormat);

                        mesh.AddStream(
                            std::move(rawSource),
                            std::vector<unsigned>(),
                            vbIA._elements[e]._semanticName, vbIA._elements[e]._semanticIndex);
                    }
                }

                auto posElement = mesh.FindElement("POSITION");
                if (posElement == ~0u) {
                    Log(Warning) << "No vertex positions found in mesh! Cannot calculate AO for this mesh." << std::endl;
                    continue;
                }

                    // Compress the positions stream so that identical positions are
                    // combined into one. 
                    // Once that is done, we need to be able to find all of the normals
                    // associated with each point.

                const auto& stream = mesh.GetStreams()[posElement];
                std::vector<unsigned> remapping;
                auto newSource = RemoveDuplicates(
                    remapping, *stream.GetSourceData(), 
                    MakeIteratorRange(stream.GetVertexMap()), duplicatesThreshold);

                mesh.RemoveStream(posElement);
                posElement = mesh.AddStream(newSource, std::move(remapping), "POSITION", 0);

                auto nEle = mesh.FindElement("NORMAL");
                if (nEle == ~0u) {
                    Log(Warning) << "No vertex normals found in mesh! Cannot calculate AO for this mesh." << std::endl;
                    continue;
                }

                const auto& pStream = mesh.GetStreams()[posElement];
                const auto& nStream = mesh.GetStreams()[nEle];
                std::vector<std::pair<unsigned, unsigned>> pn;
                pn.reserve(mesh.GetUnifiedVertexCount());
                for (unsigned q=0; q<mesh.GetUnifiedVertexCount(); ++q)
                    pn.push_back(
                        std::make_pair(pStream.UnifiedToStream(q), nStream.UnifiedToStream(q)));
            
                    // sort by position index
                std::sort(pn.begin(), pn.end(), CompareFirst<unsigned, unsigned>());

                    // find the final sample points
                std::vector<Float3> samplePoints;
                samplePoints.resize(pStream.GetSourceData()->GetCount(), Float3(std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), std::numeric_limits<float>::max()));

                for (auto p=pn.cbegin(); p!=pn.cend();) {
                    auto p2 = p+1;
                    while (p2!=pn.cend() && p2->first == p->first) ++p2;

                    auto n = Zero<Float3>();
                    for (auto q=p; q<p2; ++q)
                        n += GetVertex<Float3>(*nStream.GetSourceData(), q->second);
                    n = Normalize(n);
                    auto baseSamplePoint = GetVertex<Float3>(*pStream.GetSourceData(), p->first);

                        // transform through to model space...
                    baseSamplePoint = TransformPoint(toModel, baseSamplePoint);
                    n = Normalize(TransformDirectionVector(toModel, n));

                    auto samplePoint = baseSamplePoint + normalsPushOut * n;
                    samplePoints[p->first] = samplePoint;

                    p = p2;
                }

                    // Now we can actually perform the AO 
                    // calculation to generate the final occlusion values
                    // This should be the most expensive part.
                std::vector<uint8> aoValues;
                aoValues.reserve(samplePoints.size());
            
                Log(Verbose) << "Starting AO gen of " << samplePoints.size() << " pts" << std::endl;
                for (size_t p=0; p!=samplePoints.size(); ++p) {
                    float skyDomeOcc;

                        // unfortunately we have to attempt in an infinite loop
                        // while we have pending assets...
                        // It would be much better if we could just collect the 
                        // assets we're going to use, and prepare them all
                        //  -- but currently we don't have a method to do that.
                    for (;;) {
                        TRY
                        {
                            skyDomeOcc = gen.CalculateSkyDomeOcclusion(
                                threadContext, 
                                *renderer, sharedStates, meshToModel,
                                samplePoints[p]);
                        } CATCH (::Assets::Exceptions::PendingAsset&) {
                            continue;
                        } CATCH_END
                        break;
                    }

                        // CalculateSkyDomeOcclusion returned the quantity of the skydome that is occluded
                        // We want to write the complement of this value (1.0f - occlusion) to the vertex
                        // buffer.
                    skyDomeOcc = 1.f-skyDomeOcc;
                    skyDomeOcc = std::pow(skyDomeOcc, powerExaggerate);
                    auto finalValue = (uint8)Clamp(skyDomeOcc * float(0xff), 0.f, float(0xff));
                    aoValues.push_back(finalValue);

                    if ((p % 100)==0)
                        Log(Verbose) << "Generated " << p << "/" << samplePoints.size() << " AO sample points" << std::endl;
                }
                Log(Verbose) << "Finished AO gen" << std::endl;

                    // Note that when we using the vertex map here, it should 
                    // guaranteed us a final VB that matches the vertices from the input
                MeshDatabase meshSupp;
                meshSupp.AddStream(
                    CreateRawDataSource(
                        std::move(aoValues),
                        samplePoints.size(), sizeof(uint8),
						Format::R8_UNORM),
                    std::vector<unsigned>(pStream.GetVertexMap()), // shares the same vertex map
                    "PER_VERTEX_AO", 0);

                meshes.push_back(std::make_pair(*c, std::move(meshSupp)));
            }
        }

        // Now we have a list of meshes containing supplemental vertex streams
        // we need to write these to a ModelSupplementImmutableData asset

        WriteSupplementalModel(destinationFile, MakeIteratorRange(meshes), NativeVBSettings { true });
    }

    static void WriteSupplementalModel(
        StringSection<::Assets::ResChar> destinationFile,
        IteratorRange<std::pair<unsigned, MeshDatabase>*> meshes,
        const NativeVBSettings& nativeVbSettings)
    {
        std::vector<uint8> largeResourcesChunk;
        std::unique_ptr<uint8[]> scaffoldChunk;
        size_t scaffoldChunkSize = 0;

        {
            ::Assets::BlockSerializer suppArray;
            for (auto m=meshes.begin(); m!=meshes.end(); ++m) {
                SerializationOperator(suppArray, m->first);

                NativeVBLayout vbLayout = BuildDefaultLayout(m->second, nativeVbSettings);
                auto nativeVB = m->second.BuildNativeVertexBuffer(vbLayout);

                auto vbOffset = largeResourcesChunk.size();
                auto vbSize = nativeVB.size();
                largeResourcesChunk.insert(largeResourcesChunk.end(), nativeVB.begin(), nativeVB.end());

                auto inputAssembly = RenderCore::Assets::CreateGeoInputAssembly(
                    vbLayout._elements, (unsigned)vbLayout._vertexStride);
                SerializationOperator(
                    suppArray, 
                    RenderCore::Assets::VertexData { inputAssembly, unsigned(vbOffset), unsigned(vbSize) });
            }

            ::Assets::BlockSerializer supplementImmutableData;
            supplementImmutableData.SerializeSubBlock(suppArray);
            SerializationOperator(supplementImmutableData, size_t(meshes.size()));

            scaffoldChunk = supplementImmutableData.AsMemoryBlock();
            scaffoldChunkSize = supplementImmutableData.Size();
        }
    
        {
            using namespace Assets::ChunkFile;
			auto libVersion = ConsoleRig::GetLibVersionDesc();
            SimpleChunkFileWriter file(
				::Assets::MainFileSystem::OpenBasicFile(destinationFile, "wb", 0),
                2, libVersion._versionString, libVersion._buildDateString);

            file.BeginChunk(
                RenderCore::Assets::ChunkType_ModelScaffold, 
                0, "AOSupplement");
            file.Write(scaffoldChunk.get(), scaffoldChunkSize, 1);
            file.FinishCurrentChunk();

            file.BeginChunk(
                RenderCore::Assets::ChunkType_ModelScaffoldLargeBlocks, 
                0, "AOSupplement");
            file.Write(AsPointer(largeResourcesChunk.cbegin()), largeResourcesChunk.size(), 1);
            file.FinishCurrentChunk();
        }
    }


    static void SetupCubeMapShadowProjection(
        Metal::DeviceContext& metalContext,
        Techniques::ParsingContext& parserContext,
        float nearClip, float farClip)
    {
            // set 5 faces of the cubemap...
        float n = nearClip;
        auto basicProj = PerspectiveProjection(
            -n, n, n, -n, nearClip, farClip,
            Techniques::GetDefaultClipSpaceType());

        Float4x4 cubeViewMatrices[6] = 
        {
            MakeCameraToWorld(Float3( 0.f,  0.f,  1.f), Float3( 1.f,  0.f,  0.f), Zero<Float3>()),
            MakeCameraToWorld(Float3( 1.f,  0.f,  0.f), Float3( 0.f,  0.f,  1.f), Zero<Float3>()),
            MakeCameraToWorld(Float3( 0.f,  1.f,  0.f), Float3( 0.f,  0.f,  1.f), Zero<Float3>()),
            MakeCameraToWorld(Float3(-1.f,  0.f,  0.f), Float3( 0.f,  0.f,  1.f), Zero<Float3>()),
            MakeCameraToWorld(Float3( 0.f, -1.f,  0.f), Float3( 0.f,  0.f,  1.f), Zero<Float3>()),
            MakeCameraToWorld(Float3( 0.f,  0.f, -1.f), Float3(-1.f,  0.f,  0.f), Zero<Float3>())
        };

        SceneEngine::CB_ArbitraryShadowProjection shadowProj;
        shadowProj._projectionCount = 5;
        for (unsigned c=0; c<dimof(cubeViewMatrices); ++c) {
            shadowProj._worldToProj[c] = Combine(InvertOrthonormalTransform(cubeViewMatrices[c]), basicProj);
            shadowProj._minimalProj[c] = ExtractMinimalProjection(shadowProj._worldToProj[c]);
        }

        parserContext.SetGlobalCB(
            metalContext, Techniques::TechniqueContext::CB_ShadowProjection,
            &shadowProj, sizeof(shadowProj));
    }


///////////////////////////////////////////////////////////////////////////////////////////////////

    class AOSupplementCompiler::Pimpl
    {
    public:
        std::shared_ptr<RenderCore::IThreadContext> _threadContext;
        std::unique_ptr<AoGen> _aoGen;

		class CompileResult
        {
        public:
            std::vector<::Assets::DependentFileState> _dependencies;
            ::Assets::rstring _baseDir;
        };

        CompileResult PerformCompile(
            StringSection<::Assets::ResChar> modelFilename, StringSection<::Assets::ResChar> materialFilename,
            StringSection<::Assets::ResChar> destinationFile);
    };

    auto AOSupplementCompiler::Pimpl::PerformCompile(
        StringSection<::Assets::ResChar> modelFilename, StringSection<::Assets::ResChar> materialFilename,
        StringSection<::Assets::ResChar> destinationFile) -> CompileResult
    {
        ::Assets::ResChar modelFilenameNoParam[MaxPath];
        XlCopyString(modelFilenameNoParam, MakeFileNameSplitter(modelFilename).AllExceptParameters());

        const auto& model    = ::Assets::Legacy::GetAssetComp<ModelScaffold>(modelFilename);
        const auto& material = ::Assets::Legacy::GetAssetComp<MaterialScaffold>(materialFilename, MakeStringSection(modelFilenameNoParam));
        auto searchRules     = ::Assets::DefaultDirectorySearchRules(modelFilename);

        CalculateVertexAO(
            *_threadContext, destinationFile,
            *_aoGen, model, material, &searchRules);

        return CompileResult 
            {
                std::vector<::Assets::DependentFileState>
                    {
                        ::Assets::IntermediatesStore::GetDependentFileState(modelFilename),
                        ::Assets::IntermediatesStore::GetDependentFileState(materialFilename)
                    },
                MakeFileNameSplitter(modelFilename).DriveAndPath().AsString()
            };
    }

///////////////////////////////////////////////////////////////////////////////////////////////////

    class AOSupplementCompiler::PollingOp : public ::Assets::IPollingAsyncProcess
    {
    public:
        Result::Enum Update();
        using Op = ::Assets::ArtifactCollectionFuture;
        PollingOp(
			std::shared_ptr<Pimpl> pimpl, std::shared_ptr<Op> queuedOp, 
			const std::string& modelFilename,
			const std::string& materialFilename,
			const std::string& destinationFilename,
			const ::Assets::IntermediatesStore& store);
        ~PollingOp();
    private:
        std::weak_ptr<Pimpl> _pimpl;
        std::shared_ptr<Op> _queuedOp;
		std::string _modelFilename;
		std::string _materialFilename;
		std::string _destinationFilename;
		const ::Assets::IntermediatesStore* _store;
    };

    auto AOSupplementCompiler::PollingOp::Update() -> Result::Enum
    {
        auto p = _pimpl.lock();
        if (!p) {
            _queuedOp->SetState(::Assets::AssetState::Invalid);
            return Result::Finish;
        }
        
		TRY
		{
			auto compileResult = p->PerformCompile(
				MakeStringSection(_modelFilename), MakeStringSection(_materialFilename),
				MakeStringSection(_destinationFilename));
            auto depVal = _store->WriteDependencies(
                MakeStringSection(_destinationFilename), MakeStringSection(compileResult._baseDir), 
                MakeIteratorRange(compileResult._dependencies));
			_queuedOp->AddArtifact("main", std::make_shared<::Assets::ChunkFileArtifactCollection>(_destinationFilename, depVal));
            _queuedOp->SetState(::Assets::AssetState::Ready);
        } CATCH(const ::Assets::Exceptions::PendingAsset&) {
            return Result::KeepPolling;
        } CATCH(const std::exception& e) {
            Log(Error) << "Got exception while compiling AO supplement. Exception details: " << e.what() << std::endl;
            _queuedOp->SetState(::Assets::AssetState::Invalid);
        } CATCH(...) {
            _queuedOp->SetState(::Assets::AssetState::Invalid);
        } CATCH_END

        return Result::Finish;
    }

    AOSupplementCompiler::PollingOp::PollingOp(
        std::shared_ptr<Pimpl> pimpl,
        std::shared_ptr<::Assets::ArtifactCollectionFuture> queuedOp,
		const std::string& modelFilename,
		const std::string& materialFilename,
		const std::string& destinationFilename,
		const ::Assets::IntermediatesStore& store)
    : _pimpl(std::move(pimpl))
    , _queuedOp(std::move(queuedOp))
	, _modelFilename(modelFilename)
	, _materialFilename(materialFilename)
	, _destinationFilename(destinationFilename)
	, _store(&store)
    {}

    AOSupplementCompiler::PollingOp::~PollingOp() {}

///////////////////////////////////////////////////////////////////////////////////////////////////

    class AOSupplementCompiler::Marker : public ::Assets::IIntermediateCompileMarker
    {
    public:
        std::shared_ptr<::Assets::IArtifactCollection> GetExistingAsset() const;
        std::shared_ptr<::Assets::ArtifactCollectionFuture> InvokeCompile() const;
        StringSection<::Assets::ResChar> Initializer() const;

        Marker(
            StringSection<::Assets::ResChar> modelFilename,
            StringSection<::Assets::ResChar> materialFilename,
            uint64 typeCode,
            const ::Assets::IntermediatesStore& store,
            std::shared_ptr<AOSupplementCompiler> compiler);
        ~Marker();
    private:
        ::Assets::rstring _modelFilename, _materialFilename;
        ::Assets::rstring _initializer;
        std::weak_ptr<AOSupplementCompiler> _compiler;
        uint64 _typeCode;
        const ::Assets::IntermediatesStore* _store;
        void MakeIntermediateName(::Assets::ResChar destination[], size_t destinationCount) const;
    };

    std::shared_ptr<::Assets::IArtifactCollection> AOSupplementCompiler::Marker::GetExistingAsset() const
    {
		::Assets::ResChar intermediateName[MaxPath];
        MakeIntermediateName(intermediateName, dimof(intermediateName));
        auto depVal = _store->MakeDependencyValidation(intermediateName);
		return std::make_shared<::Assets::ChunkFileArtifactCollection>(intermediateName, depVal);
    }

    std::shared_ptr<::Assets::ArtifactCollectionFuture> AOSupplementCompiler::Marker::InvokeCompile() const
    {
        auto c = _compiler.lock();
        if (!c) return nullptr;

        using QueuedOp = ::Assets::ArtifactCollectionFuture;

            // Because the we're using the immediate context, we must run in a foreground
            // thread. We can't push into a background thread here...

        auto backgroundOp = std::make_shared<QueuedOp>();
        /*backgroundOp->SetInitializer(_initializer.c_str());
        XlCopyString(backgroundOp->_initializer0, _modelFilename);
        XlCopyString(backgroundOp->_initializer1, _materialFilename);
        backgroundOp->_destinationStore = _store;
        backgroundOp->_typeCode = _typeCode;*/

		::Assets::ResChar intermediateName[MaxPath];
		MakeIntermediateName(intermediateName, dimof(intermediateName));

        ::Assets::Services::GetAsyncMan().Add(
            std::make_shared<PollingOp>(c->_pimpl, backgroundOp, _modelFilename, _materialFilename, intermediateName, *_store));
        return std::move(backgroundOp);
    }

    void AOSupplementCompiler::Marker::MakeIntermediateName(::Assets::ResChar destination[], size_t destinationCount) const
    {
        _store->MakeIntermediateName(destination, (unsigned)destinationCount, MakeStringSection(_modelFilename));
        StringMeldAppend(destination, &destination[destinationCount])
            << "-" << MakeFileNameSplitter(_materialFilename).File().AsString() << "-ao";
    }

    StringSection<::Assets::ResChar> AOSupplementCompiler::Marker::Initializer() const { return MakeStringSection(_initializer); }

    AOSupplementCompiler::Marker::Marker(
        StringSection<::Assets::ResChar> modelFilename,
        StringSection<::Assets::ResChar> materialFilename,
        uint64 typeCode,
        const ::Assets::IntermediatesStore& store,
        std::shared_ptr<AOSupplementCompiler> compiler)
    : _modelFilename(modelFilename.AsString()), _materialFilename(materialFilename.AsString())
    , _typeCode(typeCode), _store(&store), _compiler(std::move(compiler))
    {
        _initializer = modelFilename.AsString() + "(AO supplement)";
    }
    
    AOSupplementCompiler::Marker::~Marker() {}
    
    std::shared_ptr<::Assets::IIntermediateCompileMarker> 
        AOSupplementCompiler::Prepare(
            uint64 typeCode, 
            const StringSection<::Assets::ResChar> initializers[], unsigned initializerCount)
    {
		if (typeCode != ToolsRig::AOSupplementCompiler::CompilerType) return nullptr;
        if (initializerCount != 2 || initializers[0].IsEmpty() || initializers[1].IsEmpty()) 
            Throw(::Exceptions::BasicLabel("Expecting exactly 2 initializers in AOSupplementCompiler. Model filename first, then material filename"));
        const auto modelFilename = initializers[0], materialFilename = initializers[1];
        return std::make_shared<Marker>(modelFilename, materialFilename, typeCode, *::Assets::Services::GetAsyncMan().GetIntermediateStore(), shared_from_this());
    }

	std::vector<uint64_t> AOSupplementCompiler::GetTypesForAsset(const StringSection<::Assets::ResChar> initializers[], unsigned initializerCount)
	{
		return {};
	}

	std::vector<std::pair<std::string, std::string>> AOSupplementCompiler::GetExtensionsForType(uint64_t typeCode)
	{
		return {};
	}

    void AOSupplementCompiler::StallOnPendingOperations(bool)
    {
        // Any remaining queued PollingOp's in the async man will be 
        // cancelled automatically... so we don't need to stall for them.
    }

    AOSupplementCompiler::AOSupplementCompiler(std::shared_ptr<RenderCore::IThreadContext> threadContext)
    {
        _pimpl = std::make_shared<Pimpl>();
        _pimpl->_threadContext = threadContext;
        ToolsRig::AoGen::Desc settings(
            0.001f, 4.f, 128,
            2.f, 0.001f, 0.01f);
        _pimpl->_aoGen = std::make_unique<ToolsRig::AoGen>(settings);
    }

    AOSupplementCompiler::~AOSupplementCompiler() {}

}

