// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "CLIXAutoPtr.h"
#include "ManipulatorUtils.h"       // for IGetAndSetProperties

using namespace System;
using namespace System::ComponentModel;

namespace GUILayer
{
    ref class IOverlaySystem;

    class ErosionIterativeSystemPimpl;

    public ref class ErosionIterativeSystem
    {
    public:
        IOverlaySystem^ _overlay;
        IGetAndSetProperties^ _getAndSetProperties;

        ref class Settings
        {
        public:
            enum class Preview { WaterVelocity, HardMaterials, SoftMaterials };
            
            [Browsable(true)]
            [Category("Preview")] [Description("Rendering mode for the preview window")]
            property Preview ActivePreview;

            Settings();
        };
        Settings^ _settings;

        void Tick();

        ErosionIterativeSystem(String^ sourceHeights);
        !ErosionIterativeSystem();
        ~ErosionIterativeSystem();

    private:
        clix::auto_ptr<ErosionIterativeSystemPimpl> _pimpl;
    };


    public ref class CFDPreviewSettings
    {
    public:
        enum class Preview { Density, Velocity, Temperature };
            
        [Category("Preview")] [Description("Rendering mode for the preview window")]
        property Preview ActivePreview;

        [Category("Preview")] [Description("Time step")]
        property float DeltaTime;

        CFDPreviewSettings();
    };

    public interface class IterativeSystem : public IDisposable
    {
    public:
        property Object^ PreviewSettings { Object^ get(); }
        property IOverlaySystem^ Overlay { IOverlaySystem^ get(); }
        property IGetAndSetProperties^ SimulationSettings { IGetAndSetProperties^ get(); }

        virtual void Tick();
        virtual void OnMouseDown(float x, float y, float velX, float velY, unsigned mouseButton);
    };

    class CFDIterativeSystemPimpl;
    public ref class CFDIterativeSystem : public IterativeSystem
    {
    public:
        IOverlaySystem^ _overlay;
        IGetAndSetProperties^ _getAndSetProperties;
        CFDPreviewSettings^ _settings;

        property Object^ PreviewSettings { virtual Object^ get() { return _settings; } }
        property IOverlaySystem^ Overlay { virtual IOverlaySystem^ get() { return _overlay; } }
        property IGetAndSetProperties^ SimulationSettings { virtual IGetAndSetProperties^ get() { return _getAndSetProperties; } }

        virtual void Tick();
        virtual void OnMouseDown(float x, float y, float velX, float velY, unsigned mouseButton);

        CFDIterativeSystem(unsigned size);
        !CFDIterativeSystem();
        ~CFDIterativeSystem();

    private:
        clix::auto_ptr<CFDIterativeSystemPimpl> _pimpl;
    };

    class CFD3DIterativeSystemPimpl;
    public ref class CFD3DIterativeSystem : public IterativeSystem
    {
    public:
        IOverlaySystem^ _overlay;
        IGetAndSetProperties^ _getAndSetProperties;
        CFDPreviewSettings^ _settings;

        property Object^ PreviewSettings { virtual Object^ get() { return _settings; } }
        property IOverlaySystem^ Overlay { virtual IOverlaySystem^ get() { return _overlay; } }
        property IGetAndSetProperties^ SimulationSettings { virtual IGetAndSetProperties^ get() { return _getAndSetProperties; } }

        virtual void Tick();
        virtual void OnMouseDown(float x, float y, float velX, float velY, unsigned mouseButton);

        CFD3DIterativeSystem(unsigned width, unsigned height, unsigned depth);
        !CFD3DIterativeSystem();
        ~CFD3DIterativeSystem();

    private:
        clix::auto_ptr<CFD3DIterativeSystemPimpl> _pimpl;
    };
    
    class CFDRefIterativeSystemPimpl;
    public ref class CFDRefIterativeSystem : public IterativeSystem
    {
    public:
        IOverlaySystem^ _overlay;
        IGetAndSetProperties^ _getAndSetProperties;
        CFDPreviewSettings^ _settings;

        property Object^ PreviewSettings { virtual Object^ get() { return _settings; } }
        property IOverlaySystem^ Overlay { virtual IOverlaySystem^ get() { return _overlay; } }
        property IGetAndSetProperties^ SimulationSettings { virtual IGetAndSetProperties^ get() { return _getAndSetProperties; } }

        virtual void Tick();
        virtual void OnMouseDown(float x, float y, float velX, float velY, unsigned mouseButton);

        CFDRefIterativeSystem(unsigned size);
        !CFDRefIterativeSystem();
        ~CFDRefIterativeSystem();

    private:
        clix::auto_ptr<CFDRefIterativeSystemPimpl> _pimpl;
    };
    
}