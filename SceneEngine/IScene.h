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
#pragma warning(push)
#pragma warning(disable:4324) //  'SceneEngine::SceneView': structure was padded due to alignment specifier
	class SceneView
	{
	public:
		enum class Type { Normal, Shadow, PrepareResources, ShadowStatic, Other };
		Type _type = SceneView::Type::Normal;
		RenderCore::Techniques::ProjectionDesc _projection;
        ArbitraryConvexVolumeTester* _complexVolumeTester = nullptr;
	};
#pragma warning(pop)

    class ExecuteSceneContext
    {
    public:
        SceneView _view;
        IteratorRange<RenderCore::Techniques::DrawablesPacket**> _destinationPkts;
        mutable char _quickMetrics[4096];
        mutable RenderCore::BufferUploads::CommandListID _completionCmdList = 0;

        ExecuteSceneContext(const SceneView& view, IteratorRange<RenderCore::Techniques::DrawablesPacket**> destinationPkts)
        :  _view(view), _destinationPkts(destinationPkts)
        { _quickMetrics[0] = '\0'; }
        ExecuteSceneContext() { _quickMetrics[0] = '\0'; }
    };

    class IScene
    {
    public:
        virtual void ExecuteScene(
            RenderCore::IThreadContext& threadContext,
            const ExecuteSceneContext& executeContext) const = 0;
        virtual void ExecuteScene(
			RenderCore::IThreadContext& threadContext,
			const SceneEngine::ExecuteSceneContext& executeContext,
			IteratorRange<const RenderCore::Techniques::ProjectionDesc*> multiViews) const = 0;
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
