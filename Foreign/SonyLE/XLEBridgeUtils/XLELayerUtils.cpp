// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "XLELayerUtils.h"
#include "../../Tools/ToolsRig/VisualisationUtils.h"
#include "../../Tools/EntityInterface/EntityInterface.h"
#include "../../Tools/GuiLayer/MarshalString.h"
#include "../../Tools/GuiLayer/NativeEngineDevice.h"
#include "../../RenderCore/Techniques/TechniqueUtils.h"
#include "../../Math/Transformations.h"
#include "../../Assets/IFileSystem.h"
#include "../../Assets/AssetServices.h"
#include "../../ConsoleRig/GlobalServices.h"

using namespace Sce::Atf;
using namespace Sce::Atf::Applications;

#if 0       // legacy definition; used to be used below
    namespace ConsoleRig
    {
        enum class LogLevel
        {
            Fatal,
            Error,
            Warning,
            Info,
            Verbose
        };

        /// <summary>Base class from which custom log handles can be derived</summary>
        /// The callback is initialised in a disabled state. Use Enable() to install
        /// the callback and start receiving events.
        class LogCallback : std::enable_shared_from_this<LogCallback>
        {
        public:
            virtual void OnDispatch(LogLevel, const std::string&) = 0;

            void Enable();
            void Disable();
            LogCallback();
            virtual ~LogCallback();

        private:
            uint64 _guid;
        };
    }
#endif

namespace XLEBridgeUtils
{
    public interface class INativeDocumentAdapter
    {
    public:
        property EntityInterface::DocumentId NativeDocumentId { EntityInterface::DocumentId get(); }
    };

	public interface class INativeObjectAdapter
	{
	public:
		void OnAddToDocument(INativeDocumentAdapter^ doc);
		void OnRemoveFromDocument(INativeDocumentAdapter^ doc);
		void OnSetParent(INativeObjectAdapter^ newParent, uint childListId, int insertionPosition);
	};

    GUILayer::CameraDescWrapper^ Utils::AsCameraDesc(Sce::Atf::Rendering::Camera^ camera)
    {
        ToolsRig::VisCameraSettings visCam;
        visCam._position = AsFloat3(camera->WorldEye);
        visCam._focus = AsFloat3(camera->WorldLookAtPoint);
        visCam._verticalFieldOfView = camera->YFov * 180.f / gPI;
        visCam._nearClip = camera->NearZ;
        visCam._farClip = camera->FarZ;
        return gcnew GUILayer::CameraDescWrapper(visCam);
    }

    Sce::Atf::VectorMath::Matrix4F^ Utils::MakeFrustumMatrix(
        GUILayer::CameraDescWrapper^ camera,
        System::Drawing::RectangleF rectangle,
        System::Drawing::Size viewportSize)
    {
            //  Given a camera and rectangle, calculate a
            //  frustum matrix that will represents that area.
        System::Drawing::RectangleF fRect(
            rectangle.Left / float(viewportSize.Width), rectangle.Top / float(viewportSize.Height),
            rectangle.Width / float(viewportSize.Width), rectangle.Height / float(viewportSize.Height));

            // skew XY in the projection matrix to suit the rectangle
        float sx = 1.f / (fRect.Width);
        float sy = 1.f / (fRect.Height);
        float tx = -(2.f * .5f * (fRect.Left + fRect.Right) - 1.f) * sx;
        float ty = -(-2.f * .5f * (fRect.Top + fRect.Bottom) + 1.f) * sy;

        Float4x4 rectangleAdj = MakeFloat4x4(
            sx, 0.f, 0.f,  tx,
            0.f,  sy, 0.f,  ty,
            0.f, 0.f, 1.f, 0.f,
            0.f, 0.f, 0.f, 1.f);

        std::unique_ptr<Float4x4> worldToProjPtr;
        worldToProjPtr.reset((Float4x4*)GUILayer::EditorInterfaceUtils::CalculateWorldToProjection(
            camera, viewportSize.Width / float(viewportSize.Height)));
            
        auto worldToProj = Combine(*worldToProjPtr, rectangleAdj);

            // note -- forcing a transpose here!
        return gcnew Sce::Atf::VectorMath::Matrix4F(
            worldToProj(0,0), worldToProj(1,0), worldToProj(2,0), worldToProj(3,0),
            worldToProj(0,1), worldToProj(1,1), worldToProj(2,1), worldToProj(3,1),
            worldToProj(0,2), worldToProj(1,2), worldToProj(2,2), worldToProj(3,2),
            worldToProj(0,3), worldToProj(1,3), worldToProj(2,3), worldToProj(3,3));
    }

    DomChangeInspector::DomChangeInspector(IContextRegistry^ contextRegistry) 
            : m_contextRegistry(contextRegistry)
    {
        m_observableContext = nullptr;
        contextRegistry->ActiveContextChanged += 
            gcnew EventHandler(this, &DomChangeInspector::ContextRegistry_ActiveContextChanged);
    }

    void DomChangeInspector::ContextRegistry_ActiveContextChanged(System::Object^ sender, EventArgs^ e)
    {
        IObservableContext^ observableContext = m_contextRegistry->GetActiveContext<IObservableContext^>();
        if (m_observableContext == observableContext) return;
        if (m_observableContext != nullptr) {
            m_observableContext->ItemInserted -= gcnew EventHandler<ItemInsertedEventArgs<System::Object^>^>(this, &DomChangeInspector::m_observableContext_ItemInserted);
            m_observableContext->ItemRemoved -= gcnew EventHandler<ItemRemovedEventArgs<System::Object^>^>(this, &DomChangeInspector::m_observableContext_ItemRemoved);
            m_observableContext->ItemChanged -= gcnew EventHandler<Sce::Atf::ItemChangedEventArgs<System::Object^>^>(this, &DomChangeInspector::m_observableContext_ItemChanged);
            m_observableContext->Reloaded -= gcnew EventHandler(this, &DomChangeInspector::m_observableContext_Reloaded);
        }
        m_observableContext = observableContext;

        if (m_observableContext != nullptr) {
            m_observableContext->ItemInserted += gcnew EventHandler<ItemInsertedEventArgs<System::Object^>^>(this, &DomChangeInspector::m_observableContext_ItemInserted);
            m_observableContext->ItemRemoved += gcnew EventHandler<ItemRemovedEventArgs<System::Object^>^>(this, &DomChangeInspector::m_observableContext_ItemRemoved);
            m_observableContext->ItemChanged += gcnew EventHandler<Sce::Atf::ItemChangedEventArgs<System::Object^>^>(this, &DomChangeInspector::m_observableContext_ItemChanged);
            m_observableContext->Reloaded += gcnew EventHandler(this, &DomChangeInspector::m_observableContext_Reloaded);
        }
        OnActiveContextChanged(sender);
    }

///////////////////////////////////////////////////////////////////////////////////////////////////

#if 0
	namespace Internal 
    { 
        static Sce::Atf::OutputMessageType AsOutputMessageType(ConsoleRig::LogLevel level)
        {
            switch (level) {
            default:
            case ConsoleRig::LogLevel::Fatal: 
            case ConsoleRig::LogLevel::Error: return Sce::Atf::OutputMessageType::Error;
            case ConsoleRig::LogLevel::Warning: return Sce::Atf::OutputMessageType::Warning;
            case ConsoleRig::LogLevel::Info:
            case ConsoleRig::LogLevel::Verbose: return Sce::Atf::OutputMessageType::Info;
            }
        }

        ref class LoggingRedirectDelegate
        {
        public:
            static void Callback(Object^ obj)
            {
                LoggingRedirectDelegate^ del = (LoggingRedirectDelegate^)obj;
                Sce::Atf::Outputs::Write(del->Type, del->Msg);
            }

            property System::String^ Msg;
            property Sce::Atf::OutputMessageType Type;
        };

        class LoggingRedirectHelper : public ConsoleRig::LogCallback
        {
        public:
            virtual void OnDispatch(ConsoleRig::LogLevel level, const std::string& str)
            {
                    // Note -- it is not safe to handle this message immediately
                    //  Calling Sce::Atf::Outputs::Write can invoke a windows events (such as WM_PAINT)
                    //  This is a problem because we can enter this function at any time. 
                    //  If we are currently in the middle of one paint operation, this can effectively
                    //  cause us to paint recursively. 
                    //
                    //  But there's another worse issue. This thread will currently have the logging mutex
                    //  locked. So if any paint or other operations cause a log operation, then the logging
                    //  system will attempt a recursive lock on it's mutex -- which results in an exception
                    //  and ends up causing a call to std::abort()
                    //
                    // We have to delay handling the message by invoking a delegate in the main message loop.
                    // However; note that this could cause the log messages to end up in the wrong order
                    // (if we are getting log messages from different sources)

                if (!str.empty()) {
                    auto s = clix::marshalString<clix::E_UTF8>(str);
                        // We must append a new line if one isn't already there!
                        // this falls in line with the behaviour of easylogging++,
                        // which will automatically add a new line at the end of each
                        // message.
                    if (!s->EndsWith(System::Environment::NewLine))
                        s += System::Environment::NewLine;
                    
                    LoggingRedirectDelegate^ del = gcnew LoggingRedirectDelegate;
                    del->Msg = s;
                    del->Type = AsOutputMessageType(level);
                    _context->Post(
                        gcnew System::Threading::SendOrPostCallback(&LoggingRedirectDelegate::Callback), 
                        del);
                }
            }

            LoggingRedirectHelper() 
            {
                    // note -- expecting to be called from the main thread.
                _context = System::Threading::SynchronizationContext::Current;
            }

            ~LoggingRedirectHelper() {}

        protected:
            gcroot<System::Threading::SynchronizationContext^> _context;
        };
    }
#endif

    LoggingRedirect::LoggingRedirect()
    {
        // _helper = std::make_shared<Internal::LoggingRedirectHelper>();
        // _helper->Enable();
    }

    LoggingRedirect::~LoggingRedirect() {}
    LoggingRedirect::!LoggingRedirect() {}

    ConsoleRig::WeakAttachablePtr<ConsoleRig::GlobalServices> s_globalServices;

    void Utils::AttachLibrary(GUILayer::EngineDevice^ device)
    {
        device->GetNative().GetGlobalServices()->GetCrossModule().RegisterCurrentModule();
    }

    void Utils::DetachLibrary(GUILayer::EngineDevice^ device)
    {
        ConsoleRig::CrossModule::DeregisterCurrentModule();
    }

}

