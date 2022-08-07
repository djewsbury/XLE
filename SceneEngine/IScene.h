// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../RenderCore/Techniques/TechniqueUtils.h"
#include "../RenderCore/LightingEngine/StandardLightOperators.h"
#include "../RenderCore/LightingEngine/StandardLightScene.h"

namespace RenderCore { class IThreadContext; }
namespace RenderCore { namespace Techniques { class ProjectionDesc; class DrawablesPacket; } }
namespace RenderCore { namespace LightingEngine { class ILightScene; class LightSourceOperatorDesc; class ShadowOperatorDesc; class IProbeRenderingInstance; }}
namespace Assets { class DependencyValidation; }
namespace XLEMath { class ArbitraryConvexVolumeTester; }
namespace RenderCore { namespace BufferUploads { using CommandListID = uint32_t; }}
namespace Assets { class OperationContext; }

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

    class IScene
    {
    public:
        virtual void ExecuteScene(
            RenderCore::IThreadContext& threadContext,
            ExecuteSceneContext& executeContext) const = 0;
		virtual ~IScene() = default;
	};

    class ToneMapSettings;

    class LightDesc
    {
    public:
        Float3x3    _orientation;
		Float3      _position;
		Float2      _radii;

		float       _diffuseWideningMin;
		float       _diffuseWideningMax;
        Float3      _brightness;
        float       _cutoffBrightness;

        RenderCore::LightingEngine::LightSourceShape _shape;
        RenderCore::LightingEngine::DiffuseModel _diffuseModel;
        bool _isDominantLight;

        LightDesc();
    };

	using EnvironmentalLightingDesc = RenderCore::LightingEngine::EnvironmentalLightingDesc;

	class ILightingStateDelegate
	{
	public:
        virtual void        PreRender(
            const RenderCore::Techniques::ProjectionDesc& mainSceneCameraDesc, 
            RenderCore::LightingEngine::ILightScene& lightScene) = 0;
        virtual void        PostRender(RenderCore::LightingEngine::ILightScene& lightScene) = 0;
        virtual void        BindScene(RenderCore::LightingEngine::ILightScene& lightScene, std::shared_ptr<::Assets::OperationContext> =nullptr) = 0;
        virtual void        UnbindScene(RenderCore::LightingEngine::ILightScene& lightScene) = 0;
        virtual std::shared_ptr<RenderCore::LightingEngine::IProbeRenderingInstance> BeginPrepareStep(RenderCore::LightingEngine::ILightScene& lightScene, RenderCore::IThreadContext& threadContext) = 0;

        struct Operators
        {
            std::vector<RenderCore::LightingEngine::LightSourceOperatorDesc> _lightResolveOperators;
            std::vector<RenderCore::LightingEngine::ShadowOperatorDesc> _shadowResolveOperators;
        };
        virtual Operators   GetOperators() = 0;

        virtual auto        GetEnvironmentalLightingDesc() -> EnvironmentalLightingDesc = 0;
        virtual auto        GetToneMapSettings() -> ToneMapSettings = 0;

        virtual const ::Assets::DependencyValidation& GetDependencyValidation() const = 0;

		virtual ~ILightingStateDelegate() = default;
    };
}
