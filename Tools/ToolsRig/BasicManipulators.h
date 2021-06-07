// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../../RenderOverlays/DebuggingDisplay.h"
#include <memory>
#include <stdint.h>

namespace SceneEngine { class IIntersectionScene; }
namespace RenderCore { namespace Techniques { class DrawingApparatus; } }

namespace ToolsRig
{
    class IManipulator;
    class VisCameraSettings;

	enum class CameraManipulatorMode { Max_MiddleButton, Blender_RightButton };
    std::shared_ptr<IManipulator> CreateCameraManipulator(
		const std::shared_ptr<VisCameraSettings>& visCameraSettings,
		CameraManipulatorMode mode = CameraManipulatorMode::Max_MiddleButton);

    class ManipulatorStack : public PlatformRig::IInputListener
    {
    public:
        bool    OnInputEvent(const PlatformRig::InputContext& context, const PlatformRig::InputSnapshot& evnt);
        void    Register(uint64_t id, std::shared_ptr<ToolsRig::IManipulator> manipulator);

		void	Set(const std::shared_ptr<SceneEngine::IIntersectionScene>& intersectionScene);

        static const uint64_t CameraManipulator = 256;

        ManipulatorStack(
			const std::shared_ptr<VisCameraSettings>& camera,
			const std::shared_ptr<RenderCore::Techniques::DrawingApparatus>& drawingApparatus);
        ~ManipulatorStack();
    protected:
        std::vector<std::shared_ptr<ToolsRig::IManipulator>> _activeManipulators;
        std::vector<std::pair<uint64_t, std::shared_ptr<ToolsRig::IManipulator>>> _registeredManipulators;

        std::shared_ptr<VisCameraSettings> _camera;
		std::shared_ptr<RenderCore::Techniques::DrawingApparatus> _drawingApparatus;
		std::shared_ptr<SceneEngine::IIntersectionScene> _intersectionScene;
    };

}

