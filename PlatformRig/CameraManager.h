// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../RenderCore/Techniques/TechniqueUtils.h"
#include "../Math/Matrix.h"

namespace RenderCore { namespace Techniques { class CameraDesc; } }

namespace PlatformRig { class InputSnapshot; }
namespace PlatformRig { namespace Camera
{
    class UnitCamManager;
    
    class CameraInputHandler
    {
    public:
        void Update(float dt, const InputSnapshot& accumulatedInputState, const Float3x4& playerCharacterLocalToWorld);
        const RenderCore::Techniques::CameraDesc& GetCurrentState() const { return _camera; }
        
        CameraInputHandler(
            const RenderCore::Techniques::CameraDesc& initialState, 
            float charactersScale);
        ~CameraInputHandler();

    protected:
        RenderCore::Techniques::CameraDesc _camera;
        std::unique_ptr<UnitCamManager> _unitCamera;
        Float3 _orbitFocus;
        float _charactersScale;
    };
}}

