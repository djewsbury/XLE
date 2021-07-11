// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "RenderStep.h"
#include "LightingParserContext.h"
#include "LightInternal.h"
#include "SceneEngineUtils.h"
#include "RenderStepUtils.h"
#include "CommonTechniqueDelegates.h"
#include "../RenderCore/Techniques/RenderPass.h"
#include "../RenderCore/Techniques/CommonBindings.h"
#include "../RenderCore/Techniques/CommonResources.h"
#include "../RenderCore/Techniques/Techniques.h"
#include "../RenderCore/Techniques/ParsingContext.h"
#include "../RenderCore/Techniques/TechniqueDelegates.h"
#include "../RenderCore/Format.h"
#include "../RenderCore/FrameBufferDesc.h"
#include "../RenderCore/Metal/DeviceContext.h"
#include "../Assets/Assets.h"
#include "../ConsoleRig/Console.h"
#include "../ConsoleRig/ResourceBox.h"
#include "../Utility/PtrUtils.h"
#include "../xleres/FileList.h"

namespace SceneEngine
{
	using namespace RenderCore;

	class ViewDelegate_Forward : public IViewDelegate
	{
	public:
		Techniques::DrawablesPacket _preDepth;
		Techniques::DrawablesPacket _general;

		RenderCore::Techniques::DrawablesPacket* GetDrawablesPacket(Techniques::BatchFilter batch) override
		{
			switch (batch) {
			case Techniques::BatchFilter::General:
			case Techniques::BatchFilter::PostOpaque:
				return &_general;
			case Techniques::BatchFilter::PreDepth:
				return &_preDepth;
			default:
				return nullptr;
			}
		}

		void Reset() override
		{
			_preDepth.Reset();
			_general.Reset();
		}
	};

	static void ForwardLightingModel_Render(
        IThreadContext& threadContext,
		Techniques::ParsingContext& parsingContext,
        LightingParserContext& lightingParserContext,
		RenderStepFragmentInstance& rpi,
		ViewDelegate_Forward& executedScene);

	class RenderStep_Forward : public IRenderStep
	{
	public:
		std::shared_ptr<IViewDelegate> CreateViewDelegate() override;
		const RenderStepFragmentInterface& GetInterface() const override;
		void Execute(
			RenderCore::IThreadContext& threadContext,
			RenderCore::Techniques::ParsingContext& parsingContext,
			LightingParserContext& lightingParserContext,
			RenderStepFragmentInstance& rpi,
			IViewDelegate* viewDelegate) override;

		RenderStep_Forward(bool precisionTargets);
		~RenderStep_Forward();
	private:
		RenderStepFragmentInterface _forward;
		std::shared_ptr<RenderCore::Techniques::ITechniqueDelegate> _forwardIllumDelegate;
		std::shared_ptr<RenderCore::Techniques::ITechniqueDelegate> _depthOnlyDelegate;
	};

	const RenderStepFragmentInterface& RenderStep_Forward::GetInterface() const
	{
		return _forward;
	}

	std::shared_ptr<IViewDelegate> RenderStep_Forward::CreateViewDelegate()
	{
		return std::make_shared<ViewDelegate_Forward>();
	}

	void RenderStep_Forward::Execute(
		RenderCore::IThreadContext& threadContext,
		RenderCore::Techniques::ParsingContext& parsingContext,
		LightingParserContext& lightingParserContext,
		RenderStepFragmentInstance& rpi,
		IViewDelegate* viewDelegate)
	{
		assert(viewDelegate);
		ForwardLightingModel_Render(
			threadContext, parsingContext, lightingParserContext,
			rpi,
			*checked_cast<ViewDelegate_Forward*>(viewDelegate));
	}

	RenderStep_Forward::RenderStep_Forward(bool precisionTargets)
	: _forward(RenderCore::PipelineType::Graphics)
	{
			//  We must disable z write (so all shaders can be early-depth-stencil)
            //      (this is because early-depth-stencil will normally write to the depth
            //      buffer before the alpha test has been performed. The pre-depth pass
            //      will switch early-depth-stencil on and off as necessary, but in the second
            //      pass we want it on permanently because the depth reject will end up performing
            //      the same job as alpha testing)
		_forwardIllumDelegate = ConsoleRig::FindCachedBoxDep2<CommonTechniqueDelegateBox>()._forwardIllumDelegate_DisableDepthWrite;
		_depthOnlyDelegate = ConsoleRig::FindCachedBoxDep2<CommonTechniqueDelegateBox>()._depthOnlyDelegate;

		AttachmentDesc lightResolveAttachmentDesc =
			{	(!precisionTargets) ? Format::R16G16B16A16_FLOAT : Format::R32G32B32A32_FLOAT,
				1.f, 1.f, 0u,
				AttachmentDesc::DimensionsMode::OutputRelative,
				AttachmentDesc::Flags::Multisampled | AttachmentDesc::Flags::ShaderResource | AttachmentDesc::Flags::RenderTarget };

		AttachmentDesc msDepthDesc =
            {   RenderCore::Format::D24_UNORM_S8_UINT, 1.f, 1.f, 0u,
				AttachmentDesc::DimensionsMode::OutputRelative, 
                AttachmentDesc::Flags::Multisampled | AttachmentDesc::Flags::ShaderResource | AttachmentDesc::Flags::DepthStencil };

        auto output = _forward.DefineAttachment(Techniques::AttachmentSemantics::ColorHDR, lightResolveAttachmentDesc);
		auto depth = _forward.DefineAttachment(Techniques::AttachmentSemantics::MultisampleDepth, msDepthDesc);

		SubpassDesc depthOnlySubpass;
		depthOnlySubpass.SetDepthStencil(depth, LoadStore::Clear_ClearStencil);

		SubpassDesc mainSubpass;
		mainSubpass.AppendOutput(output, LoadStore::Clear);
		mainSubpass.SetDepthStencil(depth);

		_forward.AddSubpass(depthOnlySubpass.SetName("DepthOnly"), _depthOnlyDelegate);

		// todo -- parameters should be configured based on how the scene is set up
		ParameterBox box;
		// box.SetParameter((const utf8*)"SKY_PROJECTION", lightBindRes._skyTextureProjection);
		box.SetParameter((const utf8*)"HAS_DIFFUSE_IBL", 1);
		box.SetParameter((const utf8*)"HAS_SPECULAR_IBL", 1);
		_forward.AddSubpass(mainSubpass.SetName("MainForward"), _forwardIllumDelegate, std::move(box));
	}

	RenderStep_Forward::~RenderStep_Forward() {}

	std::shared_ptr<IRenderStep> CreateRenderStep_Forward(bool precisionTargets) { return std::make_shared<RenderStep_Forward>(precisionTargets); }

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	class ViewDelegate_Direct : public IViewDelegate
	{
	public:
		Techniques::DrawablesPacket _general;

		RenderCore::Techniques::DrawablesPacket* GetDrawablesPacket(Techniques::BatchFilter batch) override
		{
			switch (batch) {
			case Techniques::BatchFilter::General:
			case Techniques::BatchFilter::PostOpaque:
				return &_general;
			default:
				return nullptr;
			}
		}

		void Reset() override
		{
			_general.Reset();
		}
	};

	class RenderStep_Direct : public IRenderStep
	{
	public:
		std::shared_ptr<IViewDelegate> CreateViewDelegate() override;
		const RenderStepFragmentInterface& GetInterface() const override;
		void Execute(
			RenderCore::IThreadContext& threadContext,
			RenderCore::Techniques::ParsingContext& parsingContext,
			LightingParserContext& lightingParserContext,
			RenderStepFragmentInstance& rpi,
			IViewDelegate* viewDelegate) override;

		RenderStep_Direct(const std::shared_ptr<RenderCore::Techniques::ITechniqueDelegate>& customDelegate);
		~RenderStep_Direct();
	private:
		RenderStepFragmentInterface _direct;
		std::shared_ptr<RenderCore::Techniques::ITechniqueDelegate> _techniqueDelegate;
	};

	const RenderStepFragmentInterface& RenderStep_Direct::GetInterface() const
	{
		return _direct;
	}

	std::shared_ptr<IViewDelegate> RenderStep_Direct::CreateViewDelegate()
	{
		return std::make_shared<ViewDelegate_Direct>();
	}

	void RenderStep_Direct::Execute(
		RenderCore::IThreadContext& threadContext,
		RenderCore::Techniques::ParsingContext& parsingContext,
		LightingParserContext& lightingParserContext,
		RenderStepFragmentInstance& rpi,
		IViewDelegate* viewDelegate)
	{
		assert(viewDelegate);
		auto& executedScene = *checked_cast<ViewDelegate_Direct*>(viewDelegate);
		auto& metalContext = *Metal::DeviceContext::Get(threadContext);

		////////////
		if (!lightingParserContext._preparedDMShadows.empty())
            BindShadowsForForwardResolve(metalContext, parsingContext, lightingParserContext._preparedDMShadows[0].second);
        auto lightBindRes = LightingParser_BindLightResolveResources(metalContext, parsingContext, *lightingParserContext._delegate);
		if (lightBindRes._skyTextureProjection != ~0u) {
			parsingContext.GetTechniqueContext()._globalEnvironmentState.SetParameter((const utf8*)"SKY_PROJECTION", lightBindRes._skyTextureProjection);
			parsingContext.GetTechniqueContext()._globalEnvironmentState.SetParameter((const utf8*)"HAS_DIFFUSE_IBL", lightBindRes._hasDiffuseIBL?1:0);
			parsingContext.GetTechniqueContext()._globalEnvironmentState.SetParameter((const utf8*)"HAS_SPECULAR_IBL", lightBindRes._hasSpecularIBL?1:0);
		}
		////////////

		ReturnToSteadyState(metalContext);
		ExecuteDrawables(
            threadContext, parsingContext, MakeSequencerContext(parsingContext, *rpi.GetSequencerConfig(), TechniqueIndex_General),
			executedScene._general,
			"MainScene-Direct");
	}

	RenderStep_Direct::RenderStep_Direct(const std::shared_ptr<RenderCore::Techniques::ITechniqueDelegate>& customDelegate)
	: _direct(RenderCore::PipelineType::Graphics)
	{
		_techniqueDelegate = customDelegate;
		if (!_techniqueDelegate) {
			std::shared_ptr<Techniques::TechniqueSetFile> techniqueSetFile = ::Assets::AutoConstructAsset<RenderCore::Techniques::TechniqueSetFile>(ILLUM_TECH);
			_techniqueDelegate = RenderCore::Techniques::CreateTechniqueDelegate_DepthOnly(techniqueSetFile);
		}

		AttachmentDesc msDepthDesc =
            {   RenderCore::Format::D24_UNORM_S8_UINT, 1.f, 1.f, 0u,
				AttachmentDesc::DimensionsMode::OutputRelative, 
                AttachmentDesc::Flags::Multisampled | AttachmentDesc::Flags::ShaderResource | AttachmentDesc::Flags::DepthStencil };

        auto output = _direct.DefineAttachment(Techniques::AttachmentSemantics::ColorLDR);
		auto depth = _direct.DefineAttachment(Techniques::AttachmentSemantics::MultisampleDepth, msDepthDesc);

		SubpassDesc mainSubpass;
		mainSubpass.AppendOutput(output, LoadStore::Clear);
		mainSubpass.SetDepthStencil(depth, LoadStore::Clear_ClearStencil);

		_direct.AddSubpass(mainSubpass.SetName("MainForward"), _techniqueDelegate);
	}

	RenderStep_Direct::~RenderStep_Direct() {}

	std::shared_ptr<IRenderStep> CreateRenderStep_Direct(const std::shared_ptr<RenderCore::Techniques::ITechniqueDelegate>& customDelegate)
	{ 
		return std::make_shared<RenderStep_Direct>(customDelegate); 
	}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	static void ForwardLightingModel_Render(
        IThreadContext& threadContext,
		Techniques::ParsingContext& parsingContext,
        LightingParserContext& lightingParserContext,
		RenderStepFragmentInstance& rpi,
		ViewDelegate_Forward& executedScene)
    {
		auto& metalContext = *Metal::DeviceContext::Get(threadContext);

		if (!lightingParserContext._preparedDMShadows.empty())
            BindShadowsForForwardResolve(metalContext, parsingContext, lightingParserContext._preparedDMShadows[0].second);
        auto lightBindRes = LightingParser_BindLightResolveResources(metalContext, parsingContext, *lightingParserContext._delegate);
		if (lightBindRes._skyTextureProjection != ~0u) {
			parsingContext.GetTechniqueContext()._globalEnvironmentState.SetParameter((const utf8*)"SKY_PROJECTION", lightBindRes._skyTextureProjection);
			parsingContext.GetTechniqueContext()._globalEnvironmentState.SetParameter((const utf8*)"HAS_DIFFUSE_IBL", lightBindRes._hasDiffuseIBL?1:0);
			parsingContext.GetTechniqueContext()._globalEnvironmentState.SetParameter((const utf8*)"HAS_SPECULAR_IBL", lightBindRes._hasSpecularIBL?1:0);
		}

            //  Order independent transparency disabled when
            //  using MSAA modes... Still some problems in related to MSAA buffers
        const bool useOrderIndependentTransparency = Tweakable("UseOITrans", false) && (lightingParserContext._sampleCount <= 1);
		assert(!useOrderIndependentTransparency);	// (order independent transparency broken during refactoring)

        ReturnToSteadyState(metalContext);

		if (BatchHasContent(executedScene._preDepth)) {
			// RenderStateDelegateChangeMarker marker(parsingContext, GetStateSetResolvers()._depthOnly);
			// ExecuteDrawablesContext executeDrawablesContext(parsingContext);
			ExecuteDrawables(
				threadContext, parsingContext, MakeSequencerContext(parsingContext, *rpi.GetSequencerConfig(), TechniqueIndex_DepthOnly),
				executedScene._preDepth,
				"MainScene-DepthOnly");
			ReturnToSteadyState(metalContext);
		}

		rpi.GetRenderPassInstance().NextSubpass();

            /////
            
		// ExecuteDrawablesContext executeDrawablesContext(parsingContext);
        ExecuteDrawables(
            threadContext, parsingContext, MakeSequencerContext(parsingContext, *rpi.GetSequencerConfig(), TechniqueIndex_General),
			executedScene._general,
			"MainScene-General");

            /////

#if 0 // platformtemp
        Metal::ShaderResourceView duplicatedDepthBuffer;
        TransparencyTargetsBox* oiTransTargets = nullptr;
        if (useOrderIndependentTransparency) {
            duplicatedDepthBuffer = 
                BuildDuplicatedDepthBuffer(&metalContext, *targetsBox._msaaDepthBufferTexture->GetUnderlying());
                
            oiTransTargets = OrderIndependentTransparency_Prepare(metalContext, parsingContext, duplicatedDepthBuffer);

                //
                //      (render the parts of the scene that we want to use
                //       the order independent translucent shader)
                //      --  we can also do this in one ExecuteScene() pass, because
                //          it's ok to bind the order independent uav outputs during
                //          the normal render. But to do that requires some way to
                //          select the technique index used deeper within the
                //          scene parser.
                //
            ReturnToSteadyState(metalContext);
            ExecuteScene(
                context, parsingContext, Techniques::BatchFilter::OITransparent, preparedScene,
                TechniqueIndex_OrderIndependentTransparency, "MainScene-OITrans");
        }

            /////

            // note --  we have to careful about when we render the sky. 
            //          If we're rendering some transparent stuff, it must come
            //          after the transparent stuff. In the case of order independent
            //          transparency, we can do the "prepare" step before the
            //          sky (because the prepare step will only write opaque stuff, 
            //          but the "resolve" step must come after sky rendering.
        if (Tweakable("DoSky", true)) {
            Sky_Render(metalContext, parsingContext, true);
            Sky_RenderPostFog(metalContext, parsingContext, lightingParserContext.GetSceneParser()->GetGlobalLightingDesc());
        }

        if (useOrderIndependentTransparency) {
                //  feed the "duplicated depth buffer" into the resolve operation here...
                //  this a non-MSAA blend over a MSAA buffer; so it might kill the samples information.
                //  We can try to do this after the MSAA resolve... But we do the luminance sample
                //  before MSAA resolve, so transluent objects don't contribute to the luminance sampling.
            OrderIndependentTransparency_Resolve(metalContext, parsingContext, *oiTransTargets, duplicatedDepthBuffer); // mainTargets._msaaDepthBufferSRV);
        }
#endif
    }

}
