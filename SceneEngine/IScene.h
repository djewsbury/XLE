// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../RenderCore/Techniques/TechniqueUtils.h"
#include "../RenderCore/LightingEngine/StandardLightOperators.h"
#include "../RenderCore/LightingEngine/StandardLightScene.h"

namespace RenderCore { class IThreadContext; }
namespace RenderCore { namespace Techniques { class ProjectionDesc; class DrawablesPacket; class ParsingContext; class IImmediateDrawables; } }
namespace RenderCore { namespace LightingEngine { class ILightScene; class LightSourceOperatorDesc; class ShadowOperatorDesc; class IProbeRenderingInstance; }}
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
            RenderCore::Techniques::IImmediateDrawables&) = 0;
        virtual ~ISceneOverlay();
    };

    class MergedLightingEngineCfg
    {
    public:
        unsigned Register(const RenderCore::LightingEngine::LightSourceOperatorDesc&);
        unsigned Register(const RenderCore::LightingEngine::ShadowOperatorDesc&);
        void SetAmbientOperator(const RenderCore::LightingEngine::AmbientLightOperatorDesc&);

        IteratorRange<const RenderCore::LightingEngine::LightSourceOperatorDesc*> GetLightOperators() const { return _lightResolveOperators; }
        IteratorRange<const RenderCore::LightingEngine::ShadowOperatorDesc*> GetShadowOperators() const { return _shadowResolveOperators; }
        const RenderCore::LightingEngine::AmbientLightOperatorDesc& GetAmbientOperator() const { return _ambientOperator; }

    private:
        std::vector<RenderCore::LightingEngine::LightSourceOperatorDesc> _lightResolveOperators;
        std::vector<RenderCore::LightingEngine::ShadowOperatorDesc> _shadowResolveOperators;
        std::vector<uint64_t> _lightHashes, _shadowHashes;
        RenderCore::LightingEngine::AmbientLightOperatorDesc _ambientOperator;
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
