// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "RayTracedShadows.h"
#include "RenderStep_PrepareShadows.h"
#include "SceneEngineUtils.h"
#include "LightingParserContext.h"
#include "SceneParser.h"
#include "SceneEngineUtils.h"
#include "MetalStubs.h"
#include "LightDesc.h"
#include "LightInternal.h"
#include "LightingTargets.h"        // for MainTargetsBox in RTShadows_DrawMetrics
#include "RenderStepUtils.h"

#include "../BufferUploads/IBufferUploads.h"
#include "../BufferUploads/ResourceLocator.h"

#include "../RenderCore/Metal/TextureView.h"
#include "../RenderCore/Metal/Shader.h"
#include "../RenderCore/Metal/InputLayout.h"
#include "../RenderCore/Metal/DeviceContext.h"
#include "../RenderCore/Metal/Buffer.h"
#include "../RenderCore/Techniques/CommonResources.h"
#include "../RenderCore/Techniques/Techniques.h"
#include "../RenderCore/Techniques/ParsingContext.h"
#include "../RenderCore/ResourceUtils.h"
#include "../RenderCore/IAnnotator.h"
#include "../Assets/Assets.h"
#include "../ConsoleRig/ResourceBox.h"
#include "../ConsoleRig/Console.h"
#include "../Utility/StringFormat.h"
#include "../Utility/FunctionUtils.h"
#include "../xleres/FileList.h"

#include "../RenderCore/DX11/Metal/IncludeDX11.h"

namespace SceneEngine
{
    using namespace RenderCore;

    class RTShadowsBox
    {
    public:
        class Desc
        {
        public:
            unsigned _width, _height;
            unsigned _storageCount;
            unsigned _indexDepth;   // 16 or 32
            unsigned _triangleCount;

            Desc(unsigned width, unsigned height, unsigned storageCount, unsigned indexDepth, unsigned triangleCount)
                : _width(width), _height(height), _storageCount(storageCount), _indexDepth(indexDepth), _triangleCount(triangleCount) {}
        };

        using ResLocator = intrusive_ptr<BufferUploads::ResourceLocator>;
        using RTV = RenderCore::Metal::RenderTargetView;
        using UAV = RenderCore::Metal::UnorderedAccessView;
        using SRV = RenderCore::Metal::ShaderResourceView;

        UAV _gridBufferUAV;
        SRV _gridBufferSRV;
        UAV _listsBufferUAV;
        SRV _listsBufferSRV;
        ResLocator _gridBuffer;
        ResLocator _listsBuffer;

        RTV _dummyRTV;
        ResLocator _dummyTarget;

        ResLocator              _triangleBufferRes;
        IResourcePtr			_triangleBufferVB;
        SRV                     _triangleBufferSRV;

        ViewportDesc _gridBufferViewport;

        RTShadowsBox(const Desc&);
        ~RTShadowsBox();
    };

    RTShadowsBox::RTShadowsBox(const Desc& desc)
    {
        // we need 2 main buffers:
        //  1. "grid buffer" -- for each cell in the screen space grid, this has the starting
        //          point of the linked list of triangles
        //  2. "lists buffer" -- this contains all of the linked lists; interleaved together

        auto& uploads = GetBufferUploads();
        auto indexFormat = (desc._indexDepth==16) ? Format::R16_UINT : Format::R32_UINT;
        unsigned indexSize = (desc._indexDepth==16) ? 2 : 4;

        _gridBuffer = uploads.Transaction_Immediate(
            CreateDesc(
                BindFlag::UnorderedAccess | BindFlag::ShaderResource,
                0, GPUAccess::Read | GPUAccess::Write,
                BufferUploads::TextureDesc::Plain2D(desc._width, desc._height, indexFormat),
                "RTShadowsGrid"));

        _listsBuffer = uploads.Transaction_Immediate(
            CreateDesc(
                BindFlag::UnorderedAccess | BindFlag::ShaderResource,
                0, GPUAccess::Read | GPUAccess::Write,
                BufferUploads::LinearBufferDesc::Create(indexSize*2*desc._storageCount, indexSize*2),
                "RTShadowsList"));

            // note that if we want to use alpha test textures on the triangles,
            // we will need to leave space for the texture coordinates as well.
        unsigned triangleSize = 52;
        _triangleBufferRes = uploads.Transaction_Immediate(
            CreateDesc(
                BindFlag::StreamOutput | BindFlag::ShaderResource | BindFlag::VertexBuffer | BindFlag::RawViews,
                0, GPUAccess::Read | GPUAccess::Write,
                BufferUploads::LinearBufferDesc::Create(triangleSize*desc._triangleCount, triangleSize),
                "RTShadowsTriangles"));

#if GFXAPI_TARGET == GFXAPI_DX11	// platformtemp
        _triangleBufferVB = _triangleBufferRes->GetUnderlying();
        _triangleBufferSRV = SRV::RawBuffer(_triangleBufferRes->GetUnderlying(), triangleSize*desc._triangleCount);
#endif

        _gridBufferUAV = UAV(_gridBuffer->GetUnderlying());
        _gridBufferSRV = SRV(_gridBuffer->GetUnderlying());
        _listsBufferUAV = UAV(
			_listsBuffer->GetUnderlying(),
			TextureViewDesc{
				Format::Unknown,
				TextureViewDesc::All,
				TextureViewDesc::All,
				TextureDesc::Dimensionality::Undefined,
				TextureViewDesc::Flags::AttachedCounter});
        _listsBufferSRV = SRV(_listsBuffer->GetUnderlying());

        _dummyTarget = uploads.Transaction_Immediate(
            CreateDesc(
                BindFlag::RenderTarget,
                0, GPUAccess::Read | GPUAccess::Write,
                BufferUploads::TextureDesc::Plain2D(desc._width, desc._height, Format::R8_UINT),
                "RTShadowsDummy"));
        _dummyRTV = RTV(_dummyTarget->GetUnderlying());

        _gridBufferViewport = ViewportDesc { 0.f, 0.f, float(desc._width), float(desc._height), 0.f, 1.f };
    }

    RTShadowsBox::~RTShadowsBox() {}


    static const utf8* StringShadowCascadeMode = "SHADOW_CASCADE_MODE";

    PreparedRTShadowFrustum PrepareRTShadows(
        IThreadContext& threadContext,
        Techniques::ParsingContext& parserContext,
		LightingParserContext& lightingParserContext,
		ViewDelegate_Shadow& inputDrawables)
    {
		if (!BatchHasContent(inputDrawables._general))
            return PreparedRTShadowFrustum();

		const StandardShadowProjection& frustum = inputDrawables._shadowProj;

        GPUAnnotation anno(threadContext, "Prepare-RTShadows");

        auto& box = ConsoleRig::FindCachedBox2<RTShadowsBox>(256, 256, 1024*1024, 32, 64*1024);
        auto oldSO = MetalStubs::GetDefaultStreamOutputInitializers();
        
        static const InputElementDesc soVertex[] = 
        {
            InputElementDesc("A", 0, Format::R32G32B32A32_FLOAT),
            InputElementDesc("B", 0, Format::R32G32_FLOAT),
            InputElementDesc("C", 0, Format::R32G32B32A32_FLOAT),
            InputElementDesc("D", 0, Format::R32G32B32_FLOAT)
        };

        static const InputElementDesc il[] = 
        {
            InputElementDesc("A", 0, Format::R32G32B32A32_FLOAT),
            InputElementDesc("B", 0, Format::R32G32_FLOAT),
            InputElementDesc("C", 0, Format::R32G32B32A32_FLOAT),
            InputElementDesc("D", 0, Format::R32G32B32_FLOAT)
        };

		auto& metalContext = *RenderCore::Metal::DeviceContext::Get(threadContext);

        MetalStubs::UnbindPS<Metal::ShaderResourceView>(metalContext, 5, 3);

        const unsigned bufferCount = 1;
        unsigned strides[] = { 52 };
        unsigned offsets[] = { 0 };
        MetalStubs::SetDefaultStreamOutputInitializers(
			StreamOutputInitializers{MakeIteratorRange(soVertex), MakeIteratorRange(strides)});

        static_assert(bufferCount == dimof(strides), "Stream output buffer count mismatch");
        static_assert(bufferCount == dimof(offsets), "Stream output buffer count mismatch");
        MetalStubs::BindSO(metalContext, *box._triangleBufferVB);

            // set up the render state for writing into the grid buffer
        SavedTargets savedTargets(metalContext);
        metalContext.Bind(box._gridBufferViewport);
        MetalStubs::UnbindRenderTargets(metalContext);
        metalContext.Bind(Techniques::CommonResources()._blendOpaque);
        metalContext.Bind(Techniques::CommonResources()._defaultRasterizer);    // for newer video cards, we need "conservative raster" enabled

        PreparedRTShadowFrustum preparedResult;
        preparedResult.InitialiseConstants(&metalContext, frustum._projections);
        using TC = Techniques::TechniqueContext;
        parserContext.SetGlobalCB(metalContext, TC::CB_ShadowProjection, &preparedResult._arbitraryCBSource, sizeof(preparedResult._arbitraryCBSource));
        parserContext.SetGlobalCB(metalContext, TC::CB_OrthoShadowProjection, &preparedResult._orthoCBSource, sizeof(preparedResult._orthoCBSource));

        parserContext.GetSubframeShaderSelectors().SetParameter(
            StringShadowCascadeMode, 
            (preparedResult._mode == ShadowProjectionMode::Ortho)?2:1);

            // Now, we need to transform the object's triangle buffer into shadow
            // projection space during this step (also applying skinning, wind bending, and
            // any other animation effects. 
            //
            // Each object that will be used with projected shadows must have a buffer 
            // containing the triangle information.
            //
            // We can deal with this in a number of ways:
            //      1. rtwritetiles shader writes triangles out in a stream-output step
            //      2. transform triangles first, then pass that information through the rtwritetiles shader
            //      3. transform triangles completely separately from the rtwritetiles step
            //
            // Method 1 would avoid extra transformations of the input data, and actually
            // simplifies some of the shader work. We don't need any special input buffers
            // or extra input data. The shaders just take generic model information, and build
            // everything they need, as they need it.
            //
            // We can also choose to reject backfacing triangles at this point, as well as 
            // removing triangles that are culled from the frustum.
            //
        Float4x4 savedWorldToProjection = parserContext.GetProjectionDesc()._worldToProjection;
        parserContext.GetProjectionDesc()._worldToProjection = frustum._worldToClip;
        auto cleanup = MakeAutoCleanup(
            [&parserContext, &savedWorldToProjection]() {
                parserContext.GetProjectionDesc()._worldToProjection = savedWorldToProjection;
                parserContext.GetSubframeShaderSelectors().SetParameter(StringShadowCascadeMode, 0);
            });

        CATCH_ASSETS_BEGIN
			// ExecuteDrawablesContext executeDrawblesContext(parserContext);
            ExecuteDrawables(
                threadContext, parserContext,
				MakeSequencerContext(parserContext, TechniqueIndex_RTShadowGen),
				inputDrawables._general,
				"RTShadowGen");
        CATCH_ASSETS_END(parserContext)

        MetalStubs::UnbindSO(metalContext);
        MetalStubs::SetDefaultStreamOutputInitializers(oldSO);

            // We have the list of triangles. Let's render then into the final
            // grid buffer viewport. This should create a list of triangles for
            // each cell in the grid. The goal is to reduce the number of triangles
            // that the ray tracing shader needs to look at.
            //
            // We could attempt to do this in the same step above. But that creates
            // some problems with frustum cull and back face culling. This order
            // allows us reduce the total triangle count before we start assigning
            // primitive ids.
            //
            // todo -- also calculate min/max for each grid during this step
        CATCH_ASSETS_BEGIN
            auto& shader = ::Assets::Legacy::GetAssetDep<Metal::ShaderProgram>(
                SCENE_ENGINE_RES "/ShadowGen/rtwritetiles.hlsl:vs_passthrough:vs_*",
                SCENE_ENGINE_RES "/ShadowGen/ConsRaster.hlsl:gs_conservativeRasterization:gs_*",
                SCENE_ENGINE_RES "/ShadowGen/rtwritetiles.hlsl:ps_main:ps_*",
                "OUTPUT_PRIM_ID=1;INPUT_RAYTEST_TRIS=1");
            metalContext.Bind(shader);

            Metal::BoundInputLayout inputLayout(MakeIteratorRange(il), shader);
			VertexBufferView vbvs[] = { VertexBufferView{box._triangleBufferVB, offsets[0]} };
            inputLayout.Apply(metalContext, MakeIteratorRange(vbvs));

                // no shader constants/resources required

            metalContext.ClearUInt(box._gridBufferUAV, { 0, 0, 0, 0 });

            metalContext.Bind(Techniques::CommonResources()._blendOpaque);
            metalContext.Bind(Techniques::CommonResources()._dssDisable);
            metalContext.Bind(Techniques::CommonResources()._cullDisable);
            metalContext.Bind(Topology::PointList);

            metalContext.Bind(
                MakeResourceList(box._dummyRTV), nullptr,
                MakeResourceList(box._gridBufferUAV, box._listsBufferUAV));
            metalContext.DrawAuto();
        CATCH_ASSETS_END(parserContext)

        metalContext.Bind(Topology::TriangleList);
        savedTargets.ResetToOldTargets(metalContext);

        preparedResult._listHeadSRV = box._gridBufferSRV;
        preparedResult._linkedListsSRV = box._listsBufferSRV;
        preparedResult._trianglesSRV = box._triangleBufferSRV;
        return std::move(preparedResult);
    }


    void RTShadows_DrawMetrics(
        RenderCore::Metal::DeviceContext& context, 
        RenderCore::Techniques::ParsingContext& parserContext, 
		LightingParserContext& lightingParserContext)
    {
        SavedTargets savedTargets(context);
        auto restoreMarker = savedTargets.MakeResetMarker(context);
		auto& mainTargets = lightingParserContext.GetMainTargets();

#if GFXAPI_TARGET == GFXAPI_DX11	// platformtemp
        context.GetUnderlying()->OMSetRenderTargets(1, savedTargets.GetRenderTargets(), nullptr); // (unbind depth)
#endif

		bool precisionTargets = Tweakable("PrecisionTargets", false);
		auto diffuseAspect = (!precisionTargets) ? TextureViewDesc::Aspect::ColorSRGB : TextureViewDesc::Aspect::ColorLinear;
        context.GetNumericUniforms(ShaderStage::Pixel).Bind(
			MakeResourceList(5, 
				mainTargets.GetSRV(parserContext, Techniques::AttachmentSemantics::GBufferDiffuse, {diffuseAspect}), 
				mainTargets.GetSRV(parserContext, Techniques::AttachmentSemantics::GBufferNormal), 
				mainTargets.GetSRV(parserContext, Techniques::AttachmentSemantics::GBufferParameter), 
				mainTargets.GetSRV(parserContext, Techniques::AttachmentSemantics::MultisampleDepth)));
        const bool useMsaaSamplers = lightingParserContext._sampleCount > 1;

        StringMeld<256> defines;
        defines << "SHADOW_CASCADE_MODE=2";
        if (useMsaaSamplers) defines << ";MSAA_SAMPLERS=1";

        auto& debuggingShader = ::Assets::Legacy::GetAssetDep<Metal::ShaderProgram>(
            BASIC2D_VERTEX_HLSL ":fullscreen:vs_*", 
            SCENE_ENGINE_RES "/ShadowGen/rtshadmetrics.hlsl:ps_main:ps_*",
            defines.get());
		UniformsStreamInterface usi;
		usi.BindConstantBuffer(0, {Hash64("OrthogonalShadowProjection")});
		usi.BindConstantBuffer(1, {Hash64("ScreenToShadowProjection")});
		usi.BindShaderResource(0, Hash64("RTSListsHead"));
		usi.BindShaderResource(1, Hash64("RTSLinkedLists"));
		usi.BindShaderResource(2, Hash64("RTSTriangles"));
		usi.BindShaderResource(3, Hash64("DepthTexture"));
        Metal::BoundUniforms uniforms(
			debuggingShader,
			Metal::PipelineLayoutConfig{},
			Techniques::TechniqueContext::GetGlobalUniformsStreamInterface(),
			usi);

        context.Bind(debuggingShader);
        context.Bind(Techniques::CommonResources()._blendStraightAlpha);
        SetupVertexGeneratorShader(context);

        for (const auto& p:lightingParserContext._preparedRTShadows) {
			auto depthSRV = mainTargets.GetSRV(parserContext, Techniques::AttachmentSemantics::MultisampleDepth);
            const Metal::ShaderResourceView* srvs[] = 
                { &p.second._listHeadSRV, &p.second._linkedListsSRV, &p.second._trianglesSRV, &depthSRV };

			ConstantBufferView cbvs[] = {
				&p.second._orthoCB,
				BuildScreenToShadowConstants(
					p.second, parserContext.GetProjectionDesc()._cameraToWorld,
					parserContext.GetProjectionDesc()._cameraToProjection)};

            uniforms.Apply(context, 0, parserContext.GetGlobalUniformsStream());
            uniforms.Apply(context, 1, 
				UniformsStream{
					MakeIteratorRange(cbvs),
					UniformsStream::MakeResources(MakeIteratorRange(srvs))
					});
        }

        context.Draw(4);
    }

}