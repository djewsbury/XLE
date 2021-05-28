// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../RenderCore/Techniques/TechniqueUtils.h"
#include "../RenderCore/LightingEngine/StandardLightOperators.h"
#include "../RenderCore/LightingEngine/StandardLightScene.h"

namespace RenderCore { class IThreadContext; }
namespace RenderCore { namespace Techniques { class ProjectionDesc; class DrawablesPacket; enum class BatchFilter; } }
namespace RenderCore { namespace LightingEngine { class ILightScene; class LightSourceOperatorDesc; class ShadowOperatorDesc; }}

namespace SceneEngine
{
#pragma warning(push)
#pragma warning(disable:4324) //  'SceneEngine::SceneView': structure was padded due to alignment specifier
	class SceneView
	{
	public:
		enum class Type { Normal, Shadow, PrepareResources, Other };
		Type _type = SceneView::Type::Normal;
		RenderCore::Techniques::ProjectionDesc _projection;
	};
#pragma warning(pop)

    class ExecuteSceneContext
    {
    public:
        SceneView _view;
        RenderCore::Techniques::BatchFilter _batchFilter = RenderCore::Techniques::BatchFilter(0);
        RenderCore::Techniques::DrawablesPacket* _destinationPkt = nullptr;
    };

    class IScene
    {
    public:
        virtual void ExecuteScene(
            RenderCore::IThreadContext& threadContext,
            const ExecuteSceneContext& executeContext) const = 0;
		virtual ~IScene() = default;
	};

    class ToneMapSettings;

    class LightDesc
    {
    public:
        Float3x3    _orientation;
		Float3      _position;
		Float2      _radii;

        float       _cutoffRange;
        Float3      _diffuseColor;
		Float3      _specularColor;
		float       _diffuseWideningMin;
		float       _diffuseWideningMax;

        RenderCore::LightingEngine::LightSourceShape _shape;
        RenderCore::LightingEngine::DiffuseModel _diffuseModel;

        LightDesc();
    };

	using EnvironmentalLightingDesc = RenderCore::LightingEngine::EnvironmentalLightingDesc;

    class ShadowProjectionDesc
    {
    public:
    };

	class ILightingStateDelegate
	{
	public:
        virtual void        ConfigureLightScene(const RenderCore::Techniques::ProjectionDesc& mainSceneCameraDesc, RenderCore::LightingEngine::ILightScene& lightScene) const = 0;
        virtual std::vector<RenderCore::LightingEngine::LightSourceOperatorDesc> GetLightResolveOperators() const = 0;
		virtual std::vector<RenderCore::LightingEngine::ShadowOperatorDesc> GetShadowResolveOperators() const = 0;

        virtual auto        GetEnvironmentalLightingDesc() const -> EnvironmentalLightingDesc = 0;
        virtual auto        GetToneMapSettings() const -> ToneMapSettings = 0;

		virtual ~ILightingStateDelegate() = default;
    };
}
