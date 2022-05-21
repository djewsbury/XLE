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
}}
namespace RenderCore { namespace LightingEngine { class LightingEngineApparatus; }}
namespace RenderOverlays { class IOverlayContext; struct Rect; }
namespace OSServices { class OnChangeCallback; }
namespace SceneEngine { class IScene; class ILightingStateDelegate; class IRenderStep; }

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
    void ConfigureParsingContext(RenderCore::Techniques::ParsingContext&, const VisCameraSettings&);

    Assets::PtrToMarkerPtr<SceneEngine::ILightingStateDelegate> MakeLightingStateDelegate(StringSection<> cfgSource);

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
        uint64			_materialGuid = 0;
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

		struct DrawCallDetails { std::string _modelName, _materialName; };
		virtual DrawCallDetails GetDrawCallDetails(unsigned drawCallIndex, uint64_t materialGuid) const = 0;

		virtual void BindAnimationState(const std::shared_ptr<VisAnimationState>& animState) = 0;
		virtual bool HasActiveAnimation() const = 0;

		virtual ~IVisContent();
	};

///////////////////////////////////////////////////////////////////////////////////////////////////

    template<typename AssetType>
        using RefreshableFuture = std::function<::Assets::PtrToMarkerPtr<AssetType>()>;

    class ISimpleSceneLayer : public PlatformRig::IOverlaySystem
    {
    public:
        virtual void Set(Assets::PtrToMarkerPtr<SceneEngine::ILightingStateDelegate> envSettings) = 0;
		virtual void Set(Assets::PtrToMarkerPtr<SceneEngine::IScene> scene) = 0;

        virtual void Set(RefreshableFuture<SceneEngine::ILightingStateDelegate> envSettings) = 0;
		virtual void Set(RefreshableFuture<SceneEngine::IScene> scene) = 0;

        virtual std::shared_ptr<VisCameraSettings> GetCamera() = 0;
		virtual void ResetCamera() = 0;
        virtual ~ISimpleSceneLayer() = default;
    };

    std::shared_ptr<ISimpleSceneLayer> CreateSimpleSceneLayer(
        const std::shared_ptr<RenderCore::Techniques::ImmediateDrawingApparatus>& immediateDrawingApparatus,
        const std::shared_ptr<RenderCore::LightingEngine::LightingEngineApparatus>& lightingEngineApparatus,
        const std::shared_ptr<RenderCore::Techniques::IDeformAcceleratorPool>& deformAccelerators);

	class VisualisationOverlay : public PlatformRig::IOverlaySystem
    {
    public:
		void Set(Assets::PtrToMarkerPtr<SceneEngine::IScene> scene);
		void Set(const std::shared_ptr<VisCameraSettings>&);
		void Set(const VisOverlaySettings& overlaySettings);
		void Set(const std::shared_ptr<VisAnimationState>&);

		const VisOverlaySettings& GetOverlaySettings() const;

        virtual void Render(
            RenderCore::Techniques::ParsingContext& parserContext) override;
		virtual OverlayState GetOverlayState() const override;

        virtual void OnRenderTargetUpdate(
            IteratorRange<const RenderCore::Techniques::PreregisteredAttachment*> preregAttachments,
            const RenderCore::FrameBufferProperties& fbProps,
            IteratorRange<const RenderCore::Format*> systemAttachmentFormats) override;

        VisualisationOverlay(
            const std::shared_ptr<RenderCore::Techniques::ImmediateDrawingApparatus>& immediateDrawingApparatus,
			const VisOverlaySettings& overlaySettings,
            std::shared_ptr<VisMouseOver> mouseOver);
        ~VisualisationOverlay();
    protected:
        class Pimpl;
        std::unique_ptr<Pimpl> _pimpl;
    };

	class MouseOverTrackingListener;

    class MouseOverTrackingOverlay : public PlatformRig::IOverlaySystem
    {
    public:
        virtual std::shared_ptr<PlatformRig::IInputListener> GetInputListener() override;

        virtual void Render(
            RenderCore::Techniques::ParsingContext& parserContext) override;

		void Set(Assets::PtrToMarkerPtr<SceneEngine::IScene> scene);

		MouseOverTrackingOverlay(
            const std::shared_ptr<VisMouseOver>& mouseOver,
            const std::shared_ptr<RenderCore::Techniques::DrawingApparatus>& drawingApparatus,
            const std::shared_ptr<VisCameraSettings>& camera);
        ~MouseOverTrackingOverlay();
    protected:
        std::shared_ptr<MouseOverTrackingListener> _inputListener;
        std::shared_ptr<VisCameraSettings> _camera;
        std::shared_ptr<VisMouseOver> _mouseOver;
    };

	std::shared_ptr<PlatformRig::IOverlaySystem> MakeLayerForInput(
		const std::shared_ptr<PlatformRig::IInputListener>& listener);

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

