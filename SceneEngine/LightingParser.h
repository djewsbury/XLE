// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../RenderCore/IDevice_Forward.h"
#include "../RenderCore/ResourceDesc.h"
#include "../Math/Matrix.h"
#include "../Math/Vector.h"
#include "../Utility/IteratorUtils.h"
#include <functional>
#include <memory>

namespace RenderCore { namespace Techniques 
{ 
	class CameraDesc; class ProjectionDesc; 
	class ParsingContext; 
	enum class BatchFilter; 
	class IPipelineAcceleratorPool; 
}}

namespace RenderCore { class IThreadContext; class AttachmentDesc; }

namespace Assets { class IAsyncMarker; }

namespace SceneEngine
{
	class LightingParserContext;
    class IScene;
	class ILightingStateDelegate;
	class ILightingParserPlugin;
	class IRenderStep;

    namespace ShaderLightDesc { class BasicEnvironment; }

	enum class LightingModel { Forward, Deferred, Direct };
	std::vector<std::shared_ptr<IRenderStep>> CreateStandardRenderSteps(LightingModel lightingModel);

    class SceneTechniqueDesc
    {
    public:
		IteratorRange<const std::shared_ptr<IRenderStep>*> _renderSteps = {};
		IteratorRange<const std::shared_ptr<ILightingParserPlugin>*> _lightingPlugins = {};
		std::vector<RenderCore::LightingEngine::ShadowGeneratorDesc> _shadowGenerators = {};
		RenderCore::TextureSamples _sampling = RenderCore::TextureSamples::Create();
    };

	class CompiledSceneTechnique;
	std::shared_ptr<CompiledSceneTechnique> CreateCompiledSceneTechnique(
		const SceneTechniqueDesc& techniqueDesc,
		const std::shared_ptr<RenderCore::Techniques::IPipelineAcceleratorPool>& pipelineAccelerators,
		const RenderCore::AttachmentDesc& targetAttachmentDesc);

    /// <summary>Execute rendering</summary>
    /// This is the main entry point for rendering a scene.
    /// The lighting parser will organize buffers, perform lighting resolve
    /// operations and call out to the scene parser when parts of the scene
    /// need to be rendered. Typically this is called once per frame (though
    /// perhaps there are times when multiple renders are required for a frame,
    /// maybe for reflections).
    ///
    /// The "qualitySettings" parameter allows the caller to define the resolution
    /// and sampling quality for rendering the scene. Be careful to select valid 
    /// settings for sampling quality.
    ///
    /// Basic usage:
    /// <code>
    ///     auto renderDevice = RenderCore::CreateDevice();
    ///     auto presentationChain = renderDevice->CreatePresentationChain(...);
    ///     RenderCore::Techniques::ParsingContext parsingContext(...);
	///		std::shared_ptr<SceneEngine::ISceneParser> scene = ...;
    ///     renderDevice->BeginFrame(presentationChain.get());
    ///
    ///     auto presChainDesc = presentationChain->GetDesc();
	///     SceneEngine::SceneTechniqueDesc qualitySettings {
	///			UInt2(presChainDesc._width, presChainDesc._height) };
	///
    ///     auto lightingParserContext = SceneEngine::LightingParser_Execute(
	///			renderDevice->GetImmediateContext(), 
	///			parsingContext, *scene, qualitySettings);
    ///
    ///     presentationChain->Present();
    /// </code>
    LightingParserContext LightingParser_ExecuteScene(
        RenderCore::IThreadContext& context,
		const RenderCore::IResourcePtr& renderTarget,
		RenderCore::Techniques::ParsingContext& parserContext,
		const CompiledSceneTechnique& technique,
		const ILightingStateDelegate& lightingDelegate,
		IScene& scene,
        const RenderCore::Techniques::CameraDesc& camera);

    void LightingParser_Overlays(
        RenderCore::IThreadContext& context,
		RenderCore::Techniques::ParsingContext& parserContext,
        LightingParserContext& lightingParserContext);

	std::shared_ptr<::Assets::IAsyncMarker> PreparePipelines(
		RenderCore::IThreadContext& threadContext,
		const CompiledSceneTechnique& technique,
		const ILightingStateDelegate& lightingDelegate,
		IScene& scene);

        ///////////////////////////////////////////////////////////////////////////
        
	class MainTargets;

		///////////////////////////////////////////////////////////////////////////

    /// <summary>The LightingResolveContext is used by lighting operations during the gbuffer resolve step</summary>
    /// Don't confuse with LightingParserContext. This is a different context object, representing
    /// a sub-step during the larger lighting parser process.
    /// During the lighting resolve step, we take the complete gbuffer, and generate the "lighting buffer"
    /// There are typically a number of steps to perform, for effects like reflections, indirect
    /// lighting and atmosphere attenuation
    class LightingResolveContext
    {
    public:
        struct Pass { enum Enum { PerSample, PerPixel, Prepare }; };

        Pass::Enum  GetCurrentPass() const;
        bool        UseMsaaSamplers() const;
        unsigned    GetSamplingCount() const;

        typedef void ResolveFn(RenderCore::IThreadContext&, RenderCore::Techniques::ParsingContext&, LightingParserContext&, LightingResolveContext&, unsigned resolvePass);
        void        AppendResolve(std::function<ResolveFn>&& fn);
        void        SetPass(Pass::Enum newPass);

            //  The following are bound resources used by the ambient resolve shader
            //  In this way, we can do the resolve for all of these effects in one step
            //  (rather than having to perform a bunch of separate passes)
            //  But it means we need some special case handling for these resources.
        std::shared_ptr<RenderCore::IResourceView>		_tiledLightingResult;
        std::shared_ptr<RenderCore::IResourceView>		_ambientOcclusionResult;
        std::shared_ptr<RenderCore::IResourceView>		_screenSpaceReflectionsResult;

        std::vector<std::function<ResolveFn>>		_queuedResolveFunctions;

        LightingResolveContext(const LightingParserContext& lightingParserContext);
        ~LightingResolveContext();
    private:
        unsigned _samplingCount;
        bool _useMsaaSamplers;
        Pass::Enum _pass;
    };

    /// <summary>Plug-in for the lighting parser</summary>
    /// This allows for some customization of the lighting parser operations.
    /// <list>
    ///     <item>OnPreScenePrepare --  this will be executed before the main rendering process begins. 
    ///             It is needed for preparing resources that will be used in later steps of the pipeline.</item>
    ///     <item>OnLightingResolvePrepare -- this will be executed before the lighting resolve step 
    ///             begins. There are two purposes to this: to prepare any resources that will be required 
    ///             during lighting resolve, and to queue operations that should happen during lighting 
    ///             resolve. To queue operations, use LightingResolveContext::AppendResolve</item>
    /// </list>
    /// 
    class ILightingParserPlugin
    {
    public:
        virtual void OnPreScenePrepare(
            RenderCore::IThreadContext&, RenderCore::Techniques::ParsingContext&, LightingParserContext&) const;

        virtual void OnLightingResolvePrepare(
            RenderCore::IThreadContext&, RenderCore::Techniques::ParsingContext&, LightingParserContext&, 
			LightingResolveContext&) const;

        virtual void OnPostSceneRender(
            RenderCore::IThreadContext&, RenderCore::Techniques::ParsingContext&, LightingParserContext&, 
			RenderCore::Techniques::BatchFilter filter, unsigned techniqueIndex) const;

        virtual void InitBasicLightEnvironment(
            RenderCore::IThreadContext&, RenderCore::Techniques::ParsingContext&, LightingParserContext&, 
			ShaderLightDesc::BasicEnvironment& env) const;

		virtual ~ILightingParserPlugin();
    };
}

