// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "ManipulatorsRender.h"
#include "../../SceneEngine/PlacementsManager.h"
#include "../../SceneEngine/IScene.h"
#include "../../RenderCore/Metal/DeviceContext.h"
#include "../../RenderCore/Metal/InputLayout.h"
#include "../../RenderCore/Techniques/DeferredShaderResource.h"
#include "../../RenderCore/Techniques/ParsingContext.h"
#include "../../RenderCore/Techniques/Techniques.h"
#include "../../RenderCore/Techniques/CommonResources.h"
#include "../../RenderCore/Techniques/TechniqueUtils.h"
#include "../../RenderCore/Techniques/SimpleModelRenderer.h"
#include "../../RenderCore/Techniques/TechniqueDelegates.h"
#include "../../RenderCore/Techniques/PipelineAccelerator.h"
#include "../../RenderCore/Techniques/Services.h"
#include "../../RenderCore/Techniques/PipelineOperators.h"
#include "../../RenderCore/Assets/PredefinedCBLayout.h"
#include "../../RenderCore/Techniques/RenderPass.h"
#include "../../RenderCore/Format.h"
#include "../../RenderCore/IThreadContext.h"
#include "../../RenderOverlays/HighlightEffects.h"
#include "../../Assets/Assets.h"
#include "../../Math/Transformations.h"
#include "../../ConsoleRig/ResourceBox.h"
#include "../../xleres/FileList.h"

// #include "../../RenderCore/DX11/Metal/DX11Utils.h"

namespace ToolsRig
{
    using namespace RenderCore;

    void Placements_RenderFiltered(
        RenderCore::IThreadContext& threadContext,
        Techniques::ParsingContext& parserContext,
        Techniques::IPipelineAcceleratorPool& pipelineAccelerators,
        const RenderCore::Techniques::SequencerConfig& sequencerConfig,
        SceneEngine::PlacementsRenderer& renderer,
        const SceneEngine::PlacementCellSet& cellSet,
        const SceneEngine::PlacementGUID* filterBegin,
        const SceneEngine::PlacementGUID* filterEnd,
        uint64_t materialGuid)
    {
		class PreDrawDelegate : public RenderCore::Techniques::ICustomDrawDelegate
		{
		public:
			virtual void OnDraw(
				RenderCore::Techniques::ParsingContext& parsingContext, const RenderCore::Techniques::ExecuteDrawableContext& executeContext,
				const RenderCore::Techniques::Drawable& d) override
			{
				if (GetMaterialGuid(d) == _materialGuid)
					ExecuteStandardDraw(parsingContext, executeContext, d);
			}
			uint64_t _materialGuid;
			PreDrawDelegate(uint64_t materialGuid) : _materialGuid(materialGuid) {}
		};

		using namespace RenderCore;
		using namespace SceneEngine;
		SceneEngine::ExecuteSceneContext sceneExeContext;
        Techniques::DrawablesPacket pkt;
        sceneExeContext._destinationPkt = &pkt;
        sceneExeContext._view = {SceneView::Type::Normal, parserContext.GetProjectionDesc()};
        sceneExeContext._batchFilter = Techniques::BatchFilter::General;
		if (materialGuid == ~0ull) {
			renderer.BuildDrawables(
				sceneExeContext,
				cellSet, filterBegin, filterEnd);
		} else {
			auto del = std::make_shared<PreDrawDelegate>(materialGuid);
			renderer.BuildDrawables(sceneExeContext, cellSet, filterBegin, filterEnd, del);
		}

		Techniques::Draw(
			parserContext,
            pipelineAccelerators,
			sequencerConfig, 
			pkt);
    }

	class TechniqueBox
	{
	public:
		::Assets::PtrToFuturePtr<RenderCore::Techniques::TechniqueSetFile> _techniqueSetFile;
		std::shared_ptr<RenderCore::Techniques::ITechniqueDelegate> _forwardIllumDelegate;

		const ::Assets::DependencyValidation& GetDependencyValidation() { return _techniqueSetFile->GetDependencyValidation(); }

		TechniqueBox()
		{
			_techniqueSetFile = ::Assets::MakeAssetPtr<RenderCore::Techniques::TechniqueSetFile>(ILLUM_TECH);
			_forwardIllumDelegate = RenderCore::Techniques::CreateTechniqueDelegate_Utility(
                _techniqueSetFile, RenderCore::Techniques::UtilityDelegateType::FlatColor);
		}
	};

    void Placements_RenderHighlight(
        Techniques::ParsingContext& parserContext,
        Techniques::IPipelineAcceleratorPool& pipelineAccelerators,
        SceneEngine::PlacementsRenderer& renderer,
        const SceneEngine::PlacementCellSet& cellSet,
        const SceneEngine::PlacementGUID* filterBegin,
        const SceneEngine::PlacementGUID* filterEnd,
        uint64_t materialGuid)
    {
        CATCH_ASSETS_BEGIN
            RenderOverlays::BinaryHighlight highlight{parserContext};
			auto sequencerCfg = pipelineAccelerators.CreateSequencerConfig(
                "render-highlight",
				ConsoleRig::FindCachedBox<TechniqueBox>()._forwardIllumDelegate, ParameterBox{}, 
				highlight.GetFrameBufferDesc());
            Placements_RenderFiltered(
                parserContext, pipelineAccelerators,
				*sequencerCfg,
                renderer, cellSet, filterBegin, filterEnd, materialGuid);
            highlight.FinishWithOutline(Float3(.65f, .8f, 1.5f));
        CATCH_ASSETS_END(parserContext)
    }

	void Placements_RenderHighlightWithOutlineAndOverlay(
        Techniques::ParsingContext& parserContext,
        Techniques::IPipelineAcceleratorPool& pipelineAccelerators,
        SceneEngine::PlacementsRenderer& renderer,
        const SceneEngine::PlacementCellSet& cellSet,
		const SceneEngine::PlacementGUID* filterBegin,
        const SceneEngine::PlacementGUID* filterEnd,
        uint64_t materialGuid)
    {
		CATCH_ASSETS_BEGIN
            RenderOverlays::BinaryHighlight highlight{parserContext};
			auto sequencerCfg = pipelineAccelerators.CreateSequencerConfig(
                "render-highlight",
				ConsoleRig::FindCachedBox<TechniqueBox>()._forwardIllumDelegate, ParameterBox{}, 
				highlight.GetFrameBufferDesc());
            Placements_RenderFiltered(
                parserContext, pipelineAccelerators,
				*sequencerCfg,
                renderer, cellSet, filterBegin, filterEnd, materialGuid);

			const Float3 highlightCol(.75f, .8f, 0.4f);
            const unsigned overlayCol = 2;

            highlight.FinishWithOutlineAndOverlay(highlightCol, overlayCol);
        CATCH_ASSETS_END(parserContext)
	}

    void Placements_RenderShadow(
        Techniques::ParsingContext& parserContext,
        Techniques::IPipelineAcceleratorPool& pipelineAccelerators,
        SceneEngine::PlacementsRenderer& renderer,
        const SceneEngine::PlacementCellSet& cellSet,
        const SceneEngine::PlacementGUID* filterBegin,
        const SceneEngine::PlacementGUID* filterEnd,
        uint64_t materialGuid)
    {
        CATCH_ASSETS_BEGIN
            RenderOverlays::BinaryHighlight highlight{parserContext};
			auto sequencerCfg = pipelineAccelerators.CreateSequencerConfig(
                "render-shadow",
				ConsoleRig::FindCachedBox<TechniqueBox>()._forwardIllumDelegate, ParameterBox{}, 
				highlight.GetFrameBufferDesc());
			Placements_RenderFiltered(
                parserContext, pipelineAccelerators,
				*sequencerCfg,
                renderer, cellSet, filterBegin, filterEnd, materialGuid);
            highlight.FinishWithShadow(Float4(.025f, .025f, .025f, 0.85f));
        CATCH_ASSETS_END(parserContext)
    }

    static void DrawAutoFullscreenImmediately(
        IThreadContext& threadContext,
        ::Assets::FuturePtr<Metal::ShaderProgram>& shader,
        const UniformsStreamInterface& uniformStreamInterface,
        const UniformsStream& uniforms,
        const AttachmentBlendDesc& ab = Techniques::CommonResourceBox::s_abStraightAlpha,
        const DepthStencilDesc& ds = Techniques::CommonResourceBox::s_dsReadWrite)
    {
        auto* actualShader = shader.TryActualize()->get();

        auto& metalContext = *Metal::DeviceContext::Get(threadContext);            
        auto encoder = metalContext.BeginGraphicsEncoder_ProgressivePipeline(actualShader->GetPipelineLayout());
        encoder.Bind(*actualShader);
        
        Metal::BoundUniforms boundLayout(*actualShader, uniformStreamInterface);
        boundLayout.ApplyLooseUniforms(metalContext, encoder, uniforms);
        encoder.Bind({&ab, &ab+1});
        encoder.Bind(ds);
        encoder.Bind(Metal::BoundInputLayout{}, Topology::TriangleStrip);
        encoder.Draw(4);
    }

    void RenderCylinderHighlight(
        Techniques::ParsingContext& parserContext,
        Techniques::IPipelineAcceleratorPool& pipelineAccelerators,
        const Float3& centre, float radius)
    {
        Techniques::FrameBufferDescFragment fbDesc;
		Techniques::FrameBufferDescFragment::SubpassDesc mainPass;
		mainPass.SetName("RenderCylinderHighlight");
		mainPass.AppendOutput(fbDesc.DefineAttachment(Techniques::AttachmentSemantics::ColorLDR));
		mainPass.AppendNonFrameBufferAttachmentView(fbDesc.DefineAttachment(Techniques::AttachmentSemantics::MultisampleDepth), BindFlag::ShaderResource, TextureViewDesc{{TextureViewDesc::Aspect::Depth}});
        fbDesc.AddSubpass(std::move(mainPass));
		Techniques::RenderPassInstance rpi { parserContext, fbDesc }; 

        auto depthSrv = rpi.GetNonFrameBufferAttachmentView(0);
        if (!depthSrv) return;

        TRY
        {
            struct HighlightParameters
            {
                Float3 _center;
                float _radius;
            } highlightParameters = { centre, radius };
            auto cbs = RenderCore::ImmediateDataStream { highlightParameters };

            auto& circleHighlight = *::Assets::MakeAssetPtr<RenderCore::Techniques::DeferredShaderResource>("xleres/DefaultResources/circlehighlight.png:L")->Actualize();
            const IResourceView* resources[] = { depthSrv.get(), circleHighlight.GetShaderResource().get() };

			UniformsStreamInterface usi;
			usi.BindImmediateData(0, Hash64("CircleHighlightParameters"));
            usi.BindResourceView(0, Hash64("DepthTexture"));
            usi.BindResourceView(1, Hash64("HighlightResource"));

                // note --  this will render a full screen quad. we could render cylinder geometry instead,
                //          because this decal only affects the area within a cylinder. But it's just for
                //          tools, so the easy way should be fine.            
            DrawAutoFullscreenImmediately(
                parserContext.GetThreadContext(),
                *::Assets::MakeAssetPtr<Metal::ShaderProgram>(      // note -- we might need access to the MSAA defines for this shader
                    ::Assets::ActualizeAssetPtr<Techniques::CompiledPipelineLayoutAsset>(parserContext.GetThreadContext().GetDevice(), MAIN_PIPELINE ":GraphicsMain")->GetPipelineLayout(),
                    BASIC2D_VERTEX_HLSL ":fullscreen_viewfrustumvector:vs_*",
                    "xleres/ui/terrainmanipulators.hlsl:ps_circlehighlight:ps_*"),
                usi, UniformsStream{MakeIteratorRange(resources), MakeIteratorRange(cbs._immediateDatas)},
                Techniques::CommonResourceBox::s_abAlphaPremultiplied,
                Techniques::CommonResourceBox::s_dsDisable);

            // SceneEngine::MetalStubs::UnbindPS<Metal::ShaderResourceView>(metalContext, 3, 1);
        } 
        CATCH_ASSETS(parserContext)
        CATCH(...) {} 
        CATCH_END
    }

    void RenderRectangleHighlight(
        Techniques::ParsingContext& parserContext,
        Techniques::IPipelineAcceleratorPool& pipelineAccelerators,
        const Float3& mins, const Float3& maxs,
		RectangleHighlightType type)
    {
        Techniques::FrameBufferDescFragment fbDesc;
		Techniques::FrameBufferDescFragment::SubpassDesc mainPass;
		mainPass.SetName("RenderRectangleHighlight");
		mainPass.AppendOutput(fbDesc.DefineAttachment(Techniques::AttachmentSemantics::ColorLDR));
		mainPass.AppendNonFrameBufferAttachmentView(fbDesc.DefineAttachment(Techniques::AttachmentSemantics::MultisampleDepth), BindFlag::ShaderResource, TextureViewDesc{{TextureViewDesc::Aspect::Depth}});
        fbDesc.AddSubpass(std::move(mainPass));
		Techniques::RenderPassInstance rpi { parserContext, fbDesc }; 

        auto depthSrv = rpi.GetNonFrameBufferAttachmentView(0);
        if (!depthSrv) return;

        TRY
        {
            struct HighlightParameters
            {
                Float3 _mins; float _dummy0;
                Float3 _maxs; float _dummy1;
            } highlightParameters = {
                mins, 0.f, maxs, 0.f
            };
            auto cbs = RenderCore::ImmediateDataStream { highlightParameters };

            auto& circleHighlight = *::Assets::MakeAssetPtr<RenderCore::Techniques::DeferredShaderResource>("xleres/DefaultResources/circlehighlight.png:L")->Actualize();
            const IResourceView* resources[] = { depthSrv.get(), circleHighlight.GetShaderResource().get() };

			UniformsStreamInterface usi;
			usi.BindImmediateData(0, Hash64("RectangleHighlightParameters"));
            usi.BindResourceView(0, Hash64("DepthTexture"));
            usi.BindResourceView(1, Hash64("HighlightResource"));

                // note --  this will render a full screen quad. we could render cylinder geometry instead,
                //          because this decal only affects the area within a cylinder. But it's just for
                //          tools, so the easy way should be fine.
            DrawAutoFullscreenImmediately(
                parserContext.GetThreadContext(),
                *::Assets::MakeAssetPtr<Metal::ShaderProgram>(      // note -- we might need access to the MSAA defines for this shader
                    ::Assets::ActualizeAssetPtr<Techniques::CompiledPipelineLayoutAsset>(parserContext.GetThreadContext().GetDevice(), MAIN_PIPELINE ":GraphicsMain")->GetPipelineLayout(),
                    BASIC2D_VERTEX_HLSL ":fullscreen_viewfrustumvector:vs_*",
                    (type == RectangleHighlightType::Tool)
                        ? "xleres/ui/terrainmanipulators.hlsl:ps_rectanglehighlight:ps_*"
                        : "xleres/ui/terrainmanipulators.hlsl:ps_lockedareahighlight:ps_*"),
                usi, UniformsStream{MakeIteratorRange(resources), MakeIteratorRange(cbs._immediateDatas)},
                Techniques::CommonResourceBox::s_abAlphaPremultiplied,
                Techniques::CommonResourceBox::s_dsDisable);

            // SceneEngine::MetalStubs::UnbindPS<Metal::ShaderResourceView>(metalContext, 3, 1);
        } 
        CATCH_ASSETS(parserContext)
        CATCH(...) {} 
        CATCH_END
    }

#if 0
    class ManipulatorResBox
    {
    public:
        class Desc {};

        const ::Assets::DependencyValidation& GetDependencyValidation() { return _depVal; }

        FixedFunctionModel::SimpleShaderVariationManager _materialGenCylinder;

        ManipulatorResBox(const Desc&);
    private:
        ::Assets::DependencyValidation _depVal;
    };

    ManipulatorResBox::ManipulatorResBox(const Desc&)
    : _materialGenCylinder(
        InputLayout((const InputElementDesc*)nullptr, 0),
        { Techniques::ObjectCB::LocalTransform, Techniques::ObjectCB::BasicMaterialConstants },
        ParameterBox({ std::make_pair("SHAPE", "4") }))
    {
    }
#endif

    void DrawWorldSpaceCylinder(
        RenderCore::IThreadContext& threadContext, Techniques::ParsingContext& parserContext,
        Float3 origin, Float3 axis, float radius)
    {
        assert(0);
#if 0
        CATCH_ASSETS_BEGIN
            auto& box = ConsoleRig::FindCachedBoxDep2<ManipulatorResBox>();
            auto localToWorld = Identity<Float4x4>();
            SetTranslation(localToWorld, origin);
            SetUp(localToWorld, axis);

            Float3 forward = Float3(0.f, 0.f, 1.f);
            Float3 right = Cross(forward, axis);
            if (XlAbs(MagnitudeSquared(right)) < 1e-10f)
                right = Cross(Float3(0.f, 1.f, 0.f), axis);
            right = Normalize(right);
            Float3 adjustedForward = Normalize(Cross(axis, right));
            SetForward(localToWorld, radius * adjustedForward);
            SetRight(localToWorld, radius * right);

            auto shader = box._materialGenCylinder.FindVariation(
                parserContext, Techniques::TechniqueIndex::Forward, 
                AREA_LIGHT_TECH);
            
            if (shader._shader._shaderProgram) {
                auto& metalContext = *Metal::DeviceContext::Get(threadContext);
                ParameterBox matParams;
                matParams.SetParameter("MaterialDiffuse", Float3(0.03f, 0.03f, .33f));
                matParams.SetParameter("Opacity", 0.125f);
                auto transformPacket = Techniques::MakeLocalTransformPacket(
                    localToWorld, ExtractTranslation(parserContext.GetProjectionDesc()._cameraToWorld));
                shader._shader.Apply(
					metalContext, parserContext, {});

				ConstantBufferView cbvs[] = { transformPacket, shader._cbLayout->BuildCBDataAsPkt(matParams, RenderCore::Techniques::GetDefaultShaderLanguage()) };
				shader._shader._boundUniforms->Apply(metalContext, 1, { MakeIteratorRange(cbvs) });

                auto& commonRes = Techniques::CommonResources();
                metalContext.Bind(commonRes._blendStraightAlpha);
                metalContext.Bind(commonRes._dssReadOnly);
                // metalContext.Unbind<Metal::VertexBuffer>();
                metalContext.Bind(Topology::TriangleList);
                
                const unsigned vertexCount = 32 * 6;	// (must agree with the shader!)
                metalContext.Draw(vertexCount);
            }

        CATCH_ASSETS_END(parserContext)
#endif
    }

/*
    void DrawQuadDirect(
        RenderCore::IThreadContext& threadContext, const RenderCore::Metal::ShaderResourceView& srv, 
        Float2 screenMins, Float2 screenMaxs)
    {
        auto& metalContext = *RenderCore::Metal::DeviceContext::Get(threadContext);

        class Vertex
        {
        public:
            Float2  _position;
            Float2  _texCoord;
        } vertices[] = {
            { Float2(screenMins[0], screenMins[1]), Float2(0.f, 0.f) },
            { Float2(screenMins[0], screenMaxs[1]), Float2(0.f, 1.f) },
            { Float2(screenMaxs[0], screenMins[1]), Float2(1.f, 0.f) },
            { Float2(screenMaxs[0], screenMaxs[1]), Float2(1.f, 1.f) }
        };

        InputElementDesc vertexInputLayout[] = {
            InputElementDesc( "PIXELPOSITION", 0, Format::R32G32_FLOAT ),
            InputElementDesc( "TEXCOORD", 0, Format::R32G32_FLOAT )
        };

        auto vertexBuffer = Metal::MakeVertexBuffer(Metal::GetObjectFactory(), MakeIteratorRange(vertices));

        const auto& shaderProgram = ::Assets::Legacy::GetAssetDep<Metal::ShaderProgram>(
            BASIC2D_VERTEX_HLSL ":P2T:" VS_DefShaderModel, 
            BASIC_PIXEL_HLSL ":copy_bilinear:" PS_DefShaderModel);
        Metal::BoundInputLayout boundVertexInputLayout(MakeIteratorRange(vertexInputLayout), shaderProgram);
		VertexBufferView vbvs[] = {&vertexBuffer};
        boundVertexInputLayout.Apply(metalContext, MakeIteratorRange(vbvs));
        metalContext.Bind(shaderProgram);

        ViewportDesc viewport = metalContext.GetBoundViewport();
        float constants[] = { 1.f / viewport.Width, 1.f / viewport.Height, 0.f, 0.f };
        auto reciprocalViewportDimensions = MakeSharedPkt(constants);
        const Metal::ShaderResourceView* resources[] = { &srv };
        ConstantBufferView cbvs[] = { reciprocalViewportDimensions };
		UniformsStreamInterface interf;
		interf.BindConstantBuffer(0, {Hash64("ReciprocalViewportDimensionsCB")});
        interf.BindShaderResource(0, Hash64("DiffuseTexture"));

		Metal::BoundUniforms boundLayout(shaderProgram, Metal::PipelineLayoutConfig{}, {}, interf);
		boundLayout.Apply(metalContext, 1, {MakeIteratorRange(cbvs), UniformsStream::MakeResources(MakeIteratorRange(resources))});

        metalContext.Bind(Metal::BlendState(BlendOp::Add, Blend::SrcAlpha, Blend::InvSrcAlpha));
        metalContext.Bind(Topology::TriangleStrip);
        metalContext.Draw(dimof(vertices));

		boundLayout.UnbindShaderResources(metalContext, 1);
    }
*/
}

