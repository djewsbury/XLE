// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../RenderCore/Techniques/TechniqueUtils.h"
#include "../RenderCore/LightingEngine/StandardLightOperators.h"
#include "../RenderCore/LightingEngine/StandardLightScene.h"
#include "../RenderCore/LightingEngine/ForwardLightingDelegate.h"
#include "../RenderCore/LightingEngine/DeferredLightingDelegate.h"
#include "../RenderCore/LightingEngine/UtilityLightingDelegate.h"
#include "../RenderCore/LightingEngine/ToneMapOperator.h"
#include "../RenderCore/LightingEngine/SkyOperator.h"
#include "../RenderCore/LightingEngine/ScreenSpaceReflections.h"
#include "../RenderCore/LightingEngine/SSAOOperator.h"
#include "../RenderCore/LightingEngine/AAOperators.h"
#include "../RenderCore/LightingEngine/PostProcessOperators.h"

namespace RenderCore { class IThreadContext; }
namespace RenderCore { namespace Techniques { class ProjectionDesc; class DrawablesPacket; class ParsingContext; class IImmediateDrawables; } }
namespace RenderCore { namespace LightingEngine { class ILightScene; struct LightSourceOperatorDesc; struct ShadowOperatorDesc; class IProbeRenderingInstance; }}
namespace RenderOverlays { class FontRenderingManager; }
namespace Assets { class DependencyValidation; }
namespace XLEMath { class ArbitraryConvexVolumeTester; }
namespace RenderCore { namespace BufferUploads { using CommandListID = uint32_t; }}
namespace Assets { class OperationContext; }
namespace std { template<typename T> class future; }

namespace SceneEngine
{
    class ExecuteSceneContext
    {
    public:
        IteratorRange<RenderCore::Techniques::DrawablesPacket**> _destinationPkts;
        IteratorRange<const RenderCore::Techniques::ProjectionDesc*> _views;
		const XLEMath::ArbitraryConvexVolumeTester* _complexCullingVolume = nullptr;
        char _quickMetrics[4096];
        RenderCore::BufferUploads::CommandListID _completionCmdList = 0;

        ExecuteSceneContext(
            IteratorRange<RenderCore::Techniques::DrawablesPacket**> destinationPkts,
            IteratorRange<const RenderCore::Techniques::ProjectionDesc*> views,
		    const XLEMath::ArbitraryConvexVolumeTester* complexCullingVolume = nullptr)
        :  _destinationPkts(destinationPkts), _views(views), _complexCullingVolume(complexCullingVolume)
        { _quickMetrics[0] = '\0'; }
        ExecuteSceneContext() { _quickMetrics[0] = '\0'; }
    };

#pragma warning(push)
#pragma warning(disable:4324)          // structure was padded due to alignment specifier
    class PrepareForViewContext
    {
    public:
        std::optional<RenderCore::Techniques::ProjectionDesc> _mainCamera;
        IteratorRange<const RenderCore::Techniques::ProjectionDesc*> _shadowViews;
        IteratorRange<const RenderCore::Techniques::ProjectionDesc*> _extraViews;
    };
#pragma warning(pop)

    class IScene
    {
    public:
        virtual void ExecuteScene(
            RenderCore::IThreadContext& threadContext,
            ExecuteSceneContext& executeContext) const = 0;
        virtual std::future<void> PrepareForView(
            PrepareForViewContext& prepareContext) const;
		virtual ~IScene();
	};

    class ISceneOverlay
    {
    public:
        virtual void ExecuteOverlay(
            RenderCore::Techniques::ParsingContext&,
            RenderCore::Techniques::IImmediateDrawables&,
            RenderOverlays::FontRenderingManager*) = 0;
        virtual ~ISceneOverlay();
    };

    class MergedLightingEngineCfg
    {
    public:
        unsigned Register(const RenderCore::LightingEngine::LightSourceOperatorDesc&);
        unsigned Register(const RenderCore::LightingEngine::ShadowOperatorDesc&);

        void SetOperator(const RenderCore::LightingEngine::ForwardLightingTechniqueDesc&);
        void SetOperator(const RenderCore::LightingEngine::DeferredLightingTechniqueDesc&);
        void SetOperator(const RenderCore::LightingEngine::UtilityLightingTechniqueDesc&);
        void SetOperator(const RenderCore::LightingEngine::ToneMapAcesOperatorDesc&);
        void SetOperator(const RenderCore::LightingEngine::MultiSampleOperatorDesc&);
        void SetOperator(const RenderCore::LightingEngine::SkyOperatorDesc&);
        void SetOperator(const RenderCore::LightingEngine::SkyTextureProcessorDesc&);
        void SetOperator(const RenderCore::LightingEngine::ScreenSpaceReflectionsOperatorDesc&);
        void SetOperator(const RenderCore::LightingEngine::AmbientOcclusionOperatorDesc&);
        void SetOperator(const RenderCore::LightingEngine::TAAOperatorDesc&);
        void SetOperator(const RenderCore::LightingEngine::SharpenOperatorDesc&);
        void SetOperator(const RenderCore::LightingEngine::FilmGrainDesc&);

        IteratorRange<const RenderCore::LightingEngine::LightSourceOperatorDesc*> GetLightOperators() const { return _lightResolveOperators; }
        IteratorRange<const RenderCore::LightingEngine::ShadowOperatorDesc*> GetShadowOperators() const { return _shadowResolveOperators; }
        const RenderCore::LightingEngine::ChainedOperatorDesc* GetChainedGlobalOperators() const { return _firstChainedOperator; }

        MergedLightingEngineCfg();
        ~MergedLightingEngineCfg();
        MergedLightingEngineCfg(MergedLightingEngineCfg&&) = delete;
        MergedLightingEngineCfg& operator=(MergedLightingEngineCfg&&) = delete;
    private:
        std::vector<RenderCore::LightingEngine::LightSourceOperatorDesc> _lightResolveOperators;
        std::vector<RenderCore::LightingEngine::ShadowOperatorDesc> _shadowResolveOperators;
        std::vector<uint64_t> _lightHashes, _shadowHashes;

        template<typename T> using ChainingTemplate = RenderCore::LightingEngine::ChainedOperatorTemplate<T>;
        ChainingTemplate<RenderCore::LightingEngine::ForwardLightingTechniqueDesc> _forwardLightingOperator;
        ChainingTemplate<RenderCore::LightingEngine::DeferredLightingTechniqueDesc> _deferredLightingOperator;
        ChainingTemplate<RenderCore::LightingEngine::UtilityLightingTechniqueDesc> _utilityLightingOperator;
        ChainingTemplate<RenderCore::LightingEngine::ToneMapAcesOperatorDesc> _toneMapAcesOperator;
        ChainingTemplate<RenderCore::LightingEngine::MultiSampleOperatorDesc> _msaaOperator;
        ChainingTemplate<RenderCore::LightingEngine::SkyOperatorDesc> _skyOperator;
        ChainingTemplate<RenderCore::LightingEngine::SkyTextureProcessorDesc> _skyTextureProcessor;
        ChainingTemplate<RenderCore::LightingEngine::ScreenSpaceReflectionsOperatorDesc> _ssr;
        ChainingTemplate<RenderCore::LightingEngine::AmbientOcclusionOperatorDesc> _ssao;
        ChainingTemplate<RenderCore::LightingEngine::TAAOperatorDesc> _taaOperator;
        ChainingTemplate<RenderCore::LightingEngine::SharpenOperatorDesc> _sharpenOperator;
        ChainingTemplate<RenderCore::LightingEngine::FilmGrainDesc> _filmGrainOperator;
        RenderCore::LightingEngine::ChainedOperatorDesc* _firstChainedOperator = nullptr;

        template<typename T> void AddToOperatorList(T& op);
    };

	class ILightingStateDelegate
	{
	public:
        virtual void        PreRender(
            const RenderCore::Techniques::ProjectionDesc& mainSceneCameraDesc, 
            RenderCore::LightingEngine::ILightScene& lightScene) = 0;
        virtual void        PostRender(RenderCore::LightingEngine::ILightScene& lightScene) = 0;

        virtual void        BindScene(RenderCore::LightingEngine::ILightScene& lightScene, std::shared_ptr<::Assets::OperationContext> =nullptr) = 0;
        virtual void        UnbindScene(RenderCore::LightingEngine::ILightScene& lightScene) = 0;

        virtual void        BindCfg(MergedLightingEngineCfg& cfg) = 0;

        virtual std::shared_ptr<RenderCore::LightingEngine::IProbeRenderingInstance> BeginPrepareStep(
            RenderCore::LightingEngine::ILightScene& lightScene, RenderCore::IThreadContext& threadContext) = 0;

		virtual ~ILightingStateDelegate() = default;
    };
}
