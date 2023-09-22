// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#pragma warning(disable:4564) //  method '.ctor' of class 'Sce::Atf::ItemChangedEventArgs' defines unsupported default parameter 'reloaded'

#include "../../Tools/GUILayer/CLIXAutoPtr.h"
#include "../../../Math/Vector.h"

using namespace System;

namespace XLEBridgeUtils
{
    inline Float3 AsFloat3(Sce::Atf::VectorMath::Vec3F input) { return Float3(input.X, input.Y, input.Z); }

    inline GUILayer::Vector3 AsVector3(Sce::Atf::VectorMath::Vec3F input) { return GUILayer::Vector3(input.X, input.Y, input.Z); }
    inline Sce::Atf::VectorMath::Vec3F AsVec3F(GUILayer::Vector3 input) { return Sce::Atf::VectorMath::Vec3F(input.X, input.Y, input.Z); }

    public ref class Utils
    {
    public:
        static GUILayer::Vector3 AsVector3(Sce::Atf::VectorMath::Vec3F input) { return XLEBridgeUtils::AsVector3(input); }
        static Sce::Atf::VectorMath::Vec3F AsVec3F(GUILayer::Vector3 input)   { return XLEBridgeUtils::AsVec3F(input); }

        static GUILayer::CameraDescWrapper^ AsCameraDesc(Sce::Atf::Rendering::Camera^ camera);

        static Sce::Atf::VectorMath::Matrix4F^ MakeFrustumMatrix(
            GUILayer::CameraDescWrapper^ camera,
            System::Drawing::RectangleF rectangle,
            System::Drawing::Size viewportSize);

        static void AttachLibrary(GUILayer::EngineDevice^ device);
        static void DetachLibrary(GUILayer::EngineDevice^ device);

        static property GUILayer::EditorSceneManager^ GlobalSceneManager;
    };

    public ref class DomChangeInspector
    {
    public:
        delegate void OnChangedDelegate(System::Object^);
        event OnChangedDelegate^ OnActiveContextChanged;
        event OnChangedDelegate^ OnDOMObjectChanged;

        DomChangeInspector(Sce::Atf::Applications::IContextRegistry^ contextRegistry);

    private:
        void ContextRegistry_ActiveContextChanged(System::Object^ sender, EventArgs^ e);

        void m_observableContext_ItemChanged(System::Object^ sender, Sce::Atf::ItemChangedEventArgs<System::Object^>^ e) { OnDOMObjectChanged(e->Item); }
        void m_observableContext_ItemRemoved(System::Object^ sender, Sce::Atf::ItemRemovedEventArgs<System::Object^>^ e) { OnDOMObjectChanged(e->Item); }
        void m_observableContext_ItemInserted(System::Object^ sender, Sce::Atf::ItemInsertedEventArgs<System::Object^>^ e) { OnDOMObjectChanged(e->Item); }
        void m_observableContext_Reloaded(System::Object^ sender, EventArgs^ e) { OnDOMObjectChanged(nullptr); }
        
        Sce::Atf::IObservableContext^ m_observableContext;
        Sce::Atf::Applications::IContextRegistry^ m_contextRegistry;
    };

    namespace Internal { class LoggingRedirectHelper; }

    public ref class LoggingRedirect
    {
    public:
        LoggingRedirect();
        ~LoggingRedirect();
        !LoggingRedirect();
    private:
        clix::shared_ptr<Internal::LoggingRedirectHelper> _helper;
    };
}

