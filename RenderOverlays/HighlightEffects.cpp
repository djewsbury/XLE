// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "HighlightEffects.h"
#include "../../RenderCore/Metal/DeviceContext.h"
#include "../../RenderCore/Metal/Shader.h"
#include "../../RenderCore/Metal/TextureView.h"
#include "../../RenderCore/Metal/Resource.h"
#include "../../RenderCore/Metal/ObjectFactory.h"
#include "../../RenderCore/Metal/InputLayout.h"
#include "../../RenderCore/Metal/Resource.h"
#include "../../RenderCore/Techniques/CommonResources.h"
#include "../../RenderCore/Techniques/CommonBindings.h"
#include "../../RenderCore/Techniques/RenderPass.h"
#include "../../RenderCore/Techniques/RenderPassUtils.h"
#include "../../RenderCore/Techniques/ParsingContext.h"
#include "../../RenderCore/Techniques/Techniques.h"
#include "../../RenderCore/Techniques/DeferredShaderResource.h"
#include "../../RenderCore/Techniques/PipelineOperators.h"
#include "../../RenderCore/Format.h"
#include "../../RenderCore/BufferView.h"
#include "../../RenderCore/IThreadContext.h"
#include "../../Assets/Assets.h"
#include "../../ConsoleRig/ResourceBox.h"
#include "../../Utility/StringFormat.h"
#include "../../xleres/FileList.h"

namespace RenderOverlays
{
    using namespace RenderCore;

	::Assets::PtrToFuturePtr<Metal::ShaderProgram> LoadShaderProgram(
        const std::shared_ptr<ICompiledPipelineLayout>& pipelineLayout,
		StringSection<> vs,
		StringSection<> ps,
		StringSection<> definesTable = {})
	{
		return ::Assets::MakeAsset<Metal::ShaderProgram>(
            pipelineLayout,
            vs, ps, definesTable);
	}

    static std::shared_ptr<ICompiledPipelineLayout> GetMainPipelineLayout(std::shared_ptr<IDevice> device)
    {
        return ::Assets::Actualize<Techniques::CompiledPipelineLayoutAsset>(device, MAIN_PIPELINE ":GraphicsMain")->GetPipelineLayout();
    }

    HighlightByStencilSettings::HighlightByStencilSettings()
    {
        _outlineColor = Float3(1.5f, 1.35f, .7f);
        _highlightedMarker = 0;
        _backgroundMarker = 0;
    }

    class HighlightShaders
    {
    public:
        std::shared_ptr<Metal::ShaderProgram> _drawHighlight;
        Metal::BoundUniforms _drawHighlightUniforms;

		std::shared_ptr<Metal::ShaderProgram> _drawShadow;
        Metal::BoundUniforms _drawShadowUniforms;

        std::shared_ptr<RenderCore::IResourceView> _distinctColorsSRV;

        const ::Assets::DependencyValidation& GetDependencyValidation() const { return _validationCallback; }

        HighlightShaders(std::shared_ptr<Metal::ShaderProgram> drawHighlight, std::shared_ptr<Metal::ShaderProgram> drawShadow, std::shared_ptr<Techniques::DeferredShaderResource> distinctColors);
        static void ConstructToFuture(
			::Assets::FuturePtr<HighlightShaders>&,
			const std::shared_ptr<ICompiledPipelineLayout>& pipelineLayout);
    protected:
        ::Assets::DependencyValidation  _validationCallback;
        
    };

    void HighlightShaders::ConstructToFuture(
        ::Assets::FuturePtr<HighlightShaders>& result,
        const std::shared_ptr<ICompiledPipelineLayout>& pipelineLayout)
    {
        //// ////
        auto drawHighlightFuture = LoadShaderProgram(
            pipelineLayout,
            BASIC2D_VERTEX_HLSL ":fullscreen:vs_*", 
            OUTLINE_VIS_PIXEL_HLSL ":main:ps_*");
        auto drawShadowFuture = LoadShaderProgram(
            pipelineLayout,
            BASIC2D_VERTEX_HLSL ":fullscreen:vs_*", 
            OUTLINE_VIS_PIXEL_HLSL ":main_shadow:ps_*");

        auto tex = ::Assets::MakeAsset<RenderCore::Techniques::DeferredShaderResource>(DISTINCT_COLORS_TEXTURE);

        ::Assets::WhenAll(drawHighlightFuture, drawShadowFuture, tex).ThenConstructToFuture(result);
    }

    HighlightShaders::HighlightShaders(std::shared_ptr<Metal::ShaderProgram> drawHighlight, std::shared_ptr<Metal::ShaderProgram> drawShadow, std::shared_ptr<Techniques::DeferredShaderResource> distinctColors)
    : _drawHighlight(std::move(drawHighlight))
    , _drawShadow(std::move(drawShadow))
    , _distinctColorsSRV(distinctColors->GetShaderResource())
    {
		UniformsStreamInterface drawHighlightInterface;
		drawHighlightInterface.BindImmediateData(0, Hash64("Settings"));
        drawHighlightInterface.BindResourceView(0, Hash64("InputTexture"));
		_drawHighlightUniforms = Metal::BoundUniforms(*_drawHighlight, drawHighlightInterface);

        //// ////
        
		UniformsStreamInterface drawShadowInterface;
		drawShadowInterface.BindImmediateData(0, Hash64("ShadowHighlightSettings"));
        drawShadowInterface.BindResourceView(0, Hash64("InputTexture"));
		_drawShadowUniforms = Metal::BoundUniforms(*_drawShadow, drawShadowInterface);

        //// ////
        _validationCallback = ::Assets::GetDepValSys().Make();
        _validationCallback.RegisterDependency(_drawHighlight->GetDependencyValidation());
        _validationCallback.RegisterDependency(_drawShadow->GetDependencyValidation());
        _validationCallback.RegisterDependency(distinctColors->GetDependencyValidation());
    }    

    static void ExecuteHighlightByStencil(
        Metal::DeviceContext& metalContext,
        Metal::GraphicsEncoder_ProgressivePipeline& encoder,
        IResourceView* stencilSrv,
        const HighlightByStencilSettings& settings,
        bool onlyHighlighted,
        bool inputAttachmentMode)
    {
        assert(stencilSrv);
        auto shaders = ::Assets::MakeAsset<HighlightShaders>(encoder.GetPipelineLayout())->TryActualize();

        UniformsStream::ImmediateData cbData[] = {
            MakeOpaqueIteratorRange(settings)
        };
        auto numericUniforms = encoder.BeginNumericUniformsInterface();
        numericUniforms.BindConstantBuffers(3, cbData);
        if (inputAttachmentMode) {
            if (shaders) {
                IResourceView* srvs[] = { (*shaders)->_distinctColorsSRV.get() };
                numericUniforms.Bind(1, MakeIteratorRange(srvs));
            }
            numericUniforms.Bind(2, MakeIteratorRange(&stencilSrv, &stencilSrv+1));
        } else {
            IResourceView* srvs[] = { stencilSrv, shaders ? (*shaders)->_distinctColorsSRV.get() : nullptr };
            numericUniforms.Bind(0, MakeIteratorRange(srvs));
        }
        numericUniforms.Apply(metalContext, encoder);
        encoder.Bind(Techniques::CommonResourceBox::s_dsDisable);
        encoder.Bind(MakeIteratorRange(&Techniques::CommonResourceBox::s_abAlphaPremultiplied, &Techniques::CommonResourceBox::s_abAlphaPremultiplied+1));
        encoder.Bind(Metal::BoundInputLayout{}, Topology::TriangleStrip);

        auto desc = stencilSrv->GetResource()->GetDesc();
        if (desc._type != ResourceDesc::Type::Texture) return;
        
        auto components = GetComponents(desc._textureDesc._format);
        bool stencilInput = 
                components == FormatComponents::DepthStencil
            ||  components == FormatComponents::Stencil;
                
        StringMeld<64, ::Assets::ResChar> params;
        params << "ONLY_HIGHLIGHTED=" << unsigned(onlyHighlighted);
        if (inputAttachmentMode) params << ";INPUT_MODE=" << 2;
        else params << ";INPUT_MODE=" << (stencilInput?0:2);

        auto highlightShader = LoadShaderProgram(
            encoder.GetPipelineLayout(),
            BASIC2D_VERTEX_HLSL ":fullscreen:vs_*", 
            HIGHLIGHT_VIS_PIXEL_HLSL ":HighlightByStencil:ps_*",
            params.AsStringSection())->TryActualize();
        if (highlightShader) {
            encoder.Bind(**highlightShader);
            encoder.Draw(4);
        }

        // Note that the outline version doesn't work with inputAttachmentMode, because 
        // we need to read from several surrounding pixels
        if (!inputAttachmentMode) {
            auto outlineShader = LoadShaderProgram(
                encoder.GetPipelineLayout(),
                BASIC2D_VERTEX_HLSL ":fullscreen:vs_*", 
                HIGHLIGHT_VIS_PIXEL_HLSL ":OutlineByStencil:ps_*",
                params.AsStringSection())->TryActualize();
            if (outlineShader) {                
                encoder.Bind(**outlineShader);
                encoder.Draw(4);
            }
        }
    }

    static const bool s_inputAttachmentMode = false;

    void ExecuteHighlightByStencil(
        IThreadContext& threadContext,
        Techniques::ParsingContext& parsingContext,
        const HighlightByStencilSettings& settings,
        bool onlyHighlighted)
    {
		Techniques::FrameBufferDescFragment fbDesc;
		Techniques::FrameBufferDescFragment::SubpassDesc mainPass;
		mainPass.SetName("VisualisationOverlay");
		mainPass.AppendOutput(fbDesc.DefineAttachment(Techniques::AttachmentSemantics::ColorLDR));
        TextureViewDesc stencilViewDesc{
            {TextureViewDesc::Aspect::Stencil},
            TextureViewDesc::All, TextureViewDesc::All, TextureDesc::Dimensionality::Undefined,
            TextureViewDesc::Flags::JustStencil};
        if (s_inputAttachmentMode) {
            mainPass.AppendInput(fbDesc.DefineAttachment(Techniques::AttachmentSemantics::MultisampleDepth), stencilViewDesc);
        } else {
            mainPass.AppendNonFrameBufferAttachmentView(fbDesc.DefineAttachment(Techniques::AttachmentSemantics::MultisampleDepth), BindFlag::ShaderResource, stencilViewDesc);
        }
        fbDesc.AddSubpass(std::move(mainPass));
		Techniques::RenderPassInstance rpi { threadContext, parsingContext, fbDesc };

        auto stencilSrv = s_inputAttachmentMode ? rpi.GetInputAttachmentView(0) : rpi.GetNonFrameBufferAttachmentView(0);
        if (!stencilSrv) return;

        auto& metalContext = *RenderCore::Metal::DeviceContext::Get(threadContext);
        auto pipelineLayout = GetMainPipelineLayout(threadContext.GetDevice());
        auto encoder = metalContext.BeginGraphicsEncoder_ProgressivePipeline(pipelineLayout);
        ExecuteHighlightByStencil(metalContext, encoder, stencilSrv.get(), settings, onlyHighlighted, s_inputAttachmentMode);
    }

///////////////////////////////////////////////////////////////////////////////////////////////////

    class BinaryHighlight::Pimpl
    {
    public:
        IThreadContext*                 _threadContext;
        std::shared_ptr<RenderCore::ICompiledPipelineLayout> _pipelineLayout;
        Techniques::RenderPassInstance  _rpi;

        Pimpl(IThreadContext& threadContext, std::shared_ptr<RenderCore::ICompiledPipelineLayout> pipelineLayout)
        : _threadContext(&threadContext), _pipelineLayout(std::move(pipelineLayout)) {}
        ~Pimpl() {}
    };

	const RenderCore::FrameBufferDesc& BinaryHighlight::GetFrameBufferDesc() const
	{
		return _pimpl->_rpi.GetFrameBufferDesc();
	}

    BinaryHighlight::BinaryHighlight(
        IThreadContext& threadContext, 
		Techniques::ParsingContext& parsingContext)
    {
        using namespace RenderCore;
        auto pipelineLayout = GetMainPipelineLayout(threadContext.GetDevice());
        _pimpl = std::make_unique<Pimpl>(threadContext, std::move(pipelineLayout));

		Techniques::FrameBufferDescFragment fbDescFrag;
		auto n_offscreen = fbDescFrag.DefineAttachment(
            0, AttachmentDesc { Format::R8G8B8A8_UNORM, 0u, LoadStore::Clear, LoadStore::DontCare, 0, BindFlag::ShaderResource });
		auto n_mainColor = fbDescFrag.DefineAttachment(RenderCore::Techniques::AttachmentSemantics::ColorLDR);
		const bool doDepthTest = true;
        auto n_depth = doDepthTest ? fbDescFrag.DefineAttachment(RenderCore::Techniques::AttachmentSemantics::MultisampleDepth) : ~0u;

		Techniques::FrameBufferDescFragment::SubpassDesc subpass0;
		subpass0.AppendOutput(n_offscreen);
		subpass0.SetDepthStencil(n_depth, TextureViewDesc{ TextureViewDesc::Aspect::DepthStencil });
		fbDescFrag.AddSubpass(std::move(subpass0));

		Techniques::FrameBufferDescFragment::SubpassDesc subpass1;
		subpass1.AppendOutput(n_mainColor);
        if (s_inputAttachmentMode) {
		    subpass1.AppendInput(n_offscreen);
        } else
            subpass1.AppendNonFrameBufferAttachmentView(n_offscreen);
		fbDescFrag.AddSubpass(std::move(subpass1));
        
		ClearValue clearValues[] = {MakeClearValue(0.f, 0.f, 0.f, 0.f)};
        _pimpl->_rpi = Techniques::RenderPassInstance(
            threadContext, parsingContext, fbDescFrag, 
			{MakeIteratorRange(clearValues)});
    }

    void BinaryHighlight::FinishWithOutlineAndOverlay(RenderCore::IThreadContext& threadContext, Float3 outlineColor, unsigned overlayColor)
    {
        _pimpl->_rpi.NextSubpass();
        auto* srv = s_inputAttachmentMode ? _pimpl->_rpi.GetInputAttachmentView(0).get() : _pimpl->_rpi.GetNonFrameBufferAttachmentView(0).get();
        assert(srv);

        if (srv) {
			static Float3 highlightColO(1.5f, 1.35f, .7f);
			static unsigned overlayColO = 1;

			outlineColor = highlightColO;
			overlayColor = overlayColO;

			auto& metalContext = *Metal::DeviceContext::Get(threadContext);

			HighlightByStencilSettings settings;
			settings._outlineColor = outlineColor;
            auto encoder = metalContext.BeginGraphicsEncoder_ProgressivePipeline(_pimpl->_pipelineLayout);
			ExecuteHighlightByStencil(
				metalContext, encoder, srv, 
				settings, false, s_inputAttachmentMode);
		}

        _pimpl->_rpi.End();
    }

    void BinaryHighlight::FinishWithOutline(RenderCore::IThreadContext& threadContext, Float3 outlineColor)
    {
            //  now we can render these objects over the main image, 
            //  using some filtering

        _pimpl->_rpi.NextSubpass();
        auto* srv = s_inputAttachmentMode ? _pimpl->_rpi.GetInputAttachmentView(0).get() : _pimpl->_rpi.GetNonFrameBufferAttachmentView(0).get();
        assert(srv);

        auto shaders = ::Assets::MakeAsset<HighlightShaders>(_pimpl->_pipelineLayout)->TryActualize();
		if (srv && shaders) {
			auto& metalContext = *Metal::DeviceContext::Get(threadContext);
            auto encoder = metalContext.BeginGraphicsEncoder_ProgressivePipeline(_pimpl->_pipelineLayout);

			struct Constants { Float3 _color; unsigned _dummy; } constants = { outlineColor, 0 };

			IResourceView* rvs[] = { srv };
            UniformsStream::ImmediateData immd[] = { MakeOpaqueIteratorRange(constants) };
            UniformsStream us;
            us._resourceViews = MakeIteratorRange(rvs);
            us._immediateData = MakeIteratorRange(immd);
            (*shaders)->_drawHighlightUniforms.ApplyLooseUniforms(metalContext, encoder, us);
			encoder.Bind(*(*shaders)->_drawHighlight);
			encoder.Bind(MakeIteratorRange(&Techniques::CommonResourceBox::s_abAlphaPremultiplied, &Techniques::CommonResourceBox::s_abAlphaPremultiplied+1));
			encoder.Bind(Techniques::CommonResourceBox::s_dsDisable);
			encoder.Bind({}, Topology::TriangleStrip);
			encoder.Draw(4);
		}

        _pimpl->_rpi.End();
    }

    void BinaryHighlight::FinishWithShadow(RenderCore::IThreadContext& threadContext, Float4 shadowColor)
    {
        _pimpl->_rpi.NextSubpass();
        auto* srv = s_inputAttachmentMode ? _pimpl->_rpi.GetInputAttachmentView(0).get() : _pimpl->_rpi.GetNonFrameBufferAttachmentView(0).get();
        assert(srv);

            //  now we can render these objects over the main image, 
            //  using some filtering

        auto shaders = ::Assets::MakeAsset<HighlightShaders>(_pimpl->_pipelineLayout)->TryActualize();
        if (srv && shaders) {
			auto& metalContext = *Metal::DeviceContext::Get(threadContext);
            auto encoder = metalContext.BeginGraphicsEncoder_ProgressivePipeline(_pimpl->_pipelineLayout);
			struct Constants { Float4 _shadowColor; } constants = { shadowColor };

            IResourceView* rvs[] = { srv };
            UniformsStream::ImmediateData immd[] = { MakeOpaqueIteratorRange(constants) };
            UniformsStream us;
            us._resourceViews = MakeIteratorRange(rvs);
            us._immediateData = MakeIteratorRange(immd);
			(*shaders)->_drawShadowUniforms.ApplyLooseUniforms(metalContext, encoder, us);
			encoder.Bind(*(*shaders)->_drawShadow);
			encoder.Bind(MakeIteratorRange(&Techniques::CommonResourceBox::s_abStraightAlpha, &Techniques::CommonResourceBox::s_abStraightAlpha+1));
			encoder.Bind(Techniques::CommonResourceBox::s_dsDisable);
			encoder.Bind({}, Topology::TriangleStrip);
			encoder.Draw(4);
		}

        _pimpl->_rpi.End();
    }

    BinaryHighlight::~BinaryHighlight() {}

}

