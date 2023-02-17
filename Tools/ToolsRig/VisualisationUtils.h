// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../../PlatformRig/OverlaySystem.h"
#include "../../Assets/AssetsCore.h"
#include "../../OSServices/FileSystemMonitor.h"
#include "../../Math/Vector.h"
#include "../../Utility/Optional.h"
#include <string>
#include <chrono>
#include <functional>

namespace RenderCore { namespace Techniques 
{
	class CameraDesc;
	class ITechniqueDelegate;
	class IPipelineAcceleratorPool;
    class IDeformAcceleratorPool;
    class ImmediateDrawingApparatus;
    class ICustomDrawDelegate;
    class DrawingApparatus;
    class IDrawablesPool;
}}
namespace RenderCore { namespace LightingEngine { class LightingEngineApparatus; }}
namespace RenderCore { namespace Assets { class RawMaterial; } }
namespace RenderOverlays { class IOverlayContext; struct Rect; }
namespace OSServices { class OnChangeCallback; }
namespace SceneEngine { class IScene; class ILightingStateDelegate; class IRenderStep; class ExecuteSceneContext; class DrawableMetadataLookupContext; }
namespace Assets { class OperationContext; }
namespace std { class any; }

namespace ToolsRig
{
	class ChangeEvent
    {
    public:
        std::vector<std::shared_ptr<OSServices::OnChangeCallback>> _callbacks;
        void Invoke();
        ~ChangeEvent();
    };

    class VisCameraSettings
    {
    public:
        Float3      _position;
        Float3      _focus;
        float       _nearClip, _farClip;

        enum class Projection { Perspective, Orthogonal };
        Projection  _projection;

        // perspective settings
        float       _verticalFieldOfView;

        // orthogonal settings
        float       _left, _top;
        float       _right, _bottom;

        VisCameraSettings();
    };

    const char* AsString(VisCameraSettings::Projection);
    std::optional<VisCameraSettings::Projection> AsProjection(StringSection<>);

    VisCameraSettings AlignCameraToBoundingBox(
        float verticalFieldOfView, 
        const std::pair<Float3, Float3>& boxIn);

	RenderCore::Techniques::CameraDesc AsCameraDesc(const VisCameraSettings& camSettings);
    VisCameraSettings AsVisCameraSettings(const RenderCore::Techniques::CameraDesc&, float distanceToFocus=5.f);
    void ConfigureParsingContext(RenderCore::Techniques::ParsingContext&, const VisCameraSettings&);

	class VisOverlaySettings
	{
	public:
        unsigned		_colourByMaterial = 0;
		unsigned		_skeletonMode = 0;
        bool			_drawNormals = false;
        bool			_drawWireframe = false;
        bool            _drawBasisAxis = true;
        bool            _drawGrid = true;
    };

    class VisMouseOver
    {
    public:
        bool			_hasMouseOver = false;
        Float3			_intersectionPt = Zero<Float3>();
        unsigned		_drawCallIndex = 0u;
        uint64_t		_materialGuid = 0;
        std::function<std::any(uint64_t)> _metadataQuery;
        ChangeEvent		_changeEvent;
    };

	class VisAnimationState
	{
	public:
		struct AnimationDetails
		{
			std::string _name;
			float _beginTime, _endTime;
		};
		std::vector<AnimationDetails> _animationList;
		std::string _activeAnimation;
		float _animationTime = 0.f;
		std::chrono::steady_clock::time_point _anchorTime;
		enum class State { Stopped, Playing, BindPose };
		State _state = State::Stopped;

		ChangeEvent _changeEvent;
	};

	class IVisContent
	{
	public:
		virtual std::pair<Float3, Float3> GetBoundingBox() const = 0;

		virtual std::shared_ptr<RenderCore::Techniques::ICustomDrawDelegate> SetCustomDrawDelegate(
			const std::shared_ptr<RenderCore::Techniques::ICustomDrawDelegate>&) = 0;
		virtual void RenderSkeleton(
			RenderOverlays::IOverlayContext& overlayContext, 
			RenderCore::Techniques::ParsingContext& parserContext, 
			bool drawBoneNames) const = 0;

		virtual void LookupDrawableMetadata(
            SceneEngine::ExecuteSceneContext& exeContext,
			SceneEngine::DrawableMetadataLookupContext& context) const = 0;

		virtual void BindAnimationState(const std::shared_ptr<VisAnimationState>& animState) = 0;
		virtual bool HasActiveAnimation() const = 0;

		virtual ~IVisContent();
	};

///////////////////////////////////////////////////////////////////////////////////////////////////

    class ISimpleSceneOverlay : public PlatformRig::IOverlaySystem
    {
    public:
        virtual void Set(std::shared_ptr<SceneEngine::ILightingStateDelegate> envSettings) = 0;
		virtual void Set(std::shared_ptr<SceneEngine::IScene> scene, std::shared_ptr<::Assets::OperationContext> loadingContext = nullptr) = 0;

        virtual void Set(std::shared_ptr<VisCameraSettings> camera) = 0;
		virtual void ResetCamera() = 0;

        virtual ~ISimpleSceneOverlay() = default;
    };

    std::shared_ptr<ISimpleSceneOverlay> CreateSimpleSceneOverlay(
        const std::shared_ptr<RenderCore::Techniques::ImmediateDrawingApparatus>& immediateDrawingApparatus,
        const std::shared_ptr<RenderCore::LightingEngine::LightingEngineApparatus>& lightingEngineApparatus,
        const std::shared_ptr<RenderCore::Techniques::IDeformAcceleratorPool>& deformAccelerators);

	class VisualisationOverlay : public PlatformRig::IOverlaySystem
    {
    public:
		void Set(std::shared_ptr<SceneEngine::IScene> scene);
		void Set(const std::shared_ptr<VisCameraSettings>&);
		void Set(const VisOverlaySettings& overlaySettings);
		void Set(const std::shared_ptr<VisAnimationState>&);

		const VisOverlaySettings& GetOverlaySettings() const;
        std::shared_ptr<VisMouseOver> GetMouseOver() const;

        virtual void Render(
            RenderCore::Techniques::ParsingContext& parserContext) override;
		virtual OverlayState GetOverlayState() const override;
        virtual std::shared_ptr<PlatformRig::IInputListener> GetInputListener() override;

        virtual void OnRenderTargetUpdate(
            IteratorRange<const RenderCore::Techniques::PreregisteredAttachment*> preregAttachments,
            const RenderCore::FrameBufferProperties& fbProps,
            IteratorRange<const RenderCore::Format*> systemAttachmentFormats) override;

        VisualisationOverlay(
            const std::shared_ptr<RenderCore::Techniques::ImmediateDrawingApparatus>& immediateDrawingApparatus,
			const VisOverlaySettings& overlaySettings);
        ~VisualisationOverlay();
    protected:
        class Pimpl;
        std::unique_ptr<Pimpl> _pimpl;
    };

	std::shared_ptr<PlatformRig::IOverlaySystem> MakeLayerForInput(
		std::shared_ptr<PlatformRig::IInputListener> listener);

    std::shared_ptr<PlatformRig::IInputListener> CreateMouseTrackingInputListener(
        std::shared_ptr<VisMouseOver> mouseOver,
        std::shared_ptr<RenderCore::Techniques::DrawingApparatus> drawingApparatus,
        std::shared_ptr<VisCameraSettings> camera);

    class ModelVisSettings;
    class MaterialVisSettings;

    /// <summary>Assigns scene and environmental settings to visualisation overlays</summary>
    /// This separates the code for managing hot reloading events out of the overlays themselves,
    /// and allows a set of overlays to be managed all at once
    class VisOverlayController
    {
    public:
        void SetScene(const ModelVisSettings&);
        void SetScene(const MaterialVisSettings&, std::shared_ptr<RenderCore::Assets::RawMaterial> = nullptr);
        void SetScene(std::shared_ptr<SceneEngine::IScene>);
        void SetScene(::Assets::PtrToMarkerPtr<SceneEngine::IScene>);

        void SetEnvSettings(StringSection<>);
        void SetEnvSettings(::Assets::PtrToMarkerPtr<SceneEngine::ILightingStateDelegate>);
        void SetEnvSettings(std::shared_ptr<SceneEngine::ILightingStateDelegate>);

        void AttachSceneOverlay(std::shared_ptr<ISimpleSceneOverlay>);
        void AttachVisualisationOverlay(std::shared_ptr<VisualisationOverlay>);

        SceneEngine::IScene* TryGetScene();
        const std::shared_ptr<::Assets::OperationContext>& GetLoadingContext();

        VisOverlayController(
            std::shared_ptr<RenderCore::Techniques::IDrawablesPool> drawablesPool,
		    std::shared_ptr<RenderCore::Techniques::IPipelineAcceleratorPool> pipelineAcceleratorPool,
            std::shared_ptr<RenderCore::Techniques::IDeformAcceleratorPool> deformAcceleratorPool,
            std::shared_ptr<::Assets::OperationContext> loadingContext);
        ~VisOverlayController();
    private:
        struct Pimpl;
        std::unique_ptr<Pimpl> _pimpl;
    };

///////////////////////////////////////////////////////////////////////////////////////////////////

	enum class DrawPreviewResult
    {
        Error,
        Pending,
        Success
    };

	std::pair<DrawPreviewResult, std::string> DrawPreview(
        RenderCore::IThreadContext& context,
		const RenderCore::IResourcePtr& renderTarget,
        RenderCore::Techniques::ParsingContext& parserContext,
		const std::shared_ptr<RenderCore::Techniques::IPipelineAcceleratorPool>& pipelineAccelerators,
		VisCameraSettings& cameraSettings,
		StringSection<> envSettings,
		SceneEngine::IScene& scene,
		const std::shared_ptr<SceneEngine::IRenderStep>& renderStep);

	void StallWhilePending(SceneEngine::IScene& future);

///////////////////////////////////////////////////////////////////////////////////////////////////

    inline VisCameraSettings::VisCameraSettings()
    {
        _position = Float3(-10.f, 0.f, 0.f);
        _focus = Zero<Float3>();
        _nearClip = 0.1f;
        _farClip = 1000.f;
        _projection = Projection::Perspective;
        _verticalFieldOfView = 40.f;
        _left = _top = -1.f;
        _right = _bottom = 1.f;
    }
}

