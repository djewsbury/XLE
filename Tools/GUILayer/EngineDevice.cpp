// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "EngineDevice.h"
#include "NativeEngineDevice.h"
#include "MarshalString.h"
#include "CLIXAutoPtr.h"
#include "WindowRig.h"
#include "DelayedDeleteQueue.h"
#include "ExportedNativeTypes.h"
#include "../ToolsRig/DivergentAsset.h"
#include "../ToolsRig/PreviewSceneRegistry.h"
#include "../ToolsRig/ToolsRigServices.h"
#include "../ToolsRig/MiscUtils.h"
#include "../ToolsRig/SampleUtils.h"
#include "../../OSServices/WinAPI/RunLoop_WinAPI.h"
#include "../../RenderCore/Techniques/Apparatuses.h"
#include "../../RenderCore/Techniques/Techniques.h"
#include "../../RenderCore/Techniques/Services.h"
#include "../../RenderCore/Techniques/RenderPass.h"
#include "../../RenderCore/LightingEngine/LightingEngineApparatus.h"
#include "../../RenderCore/IDevice.h"
#include "../../RenderCore/DeviceInitialization.h"
#include "../../RenderOverlays/OverlayApparatus.h"
#include "../../Assets/IFileSystem.h"
#include "../../Assets/MountingTree.h"
#include "../../Assets/OSFileSystem.h"
#include "../../Assets/AssetServices.h"
#include "../../Assets/AssetSetManager.h"
#include "../../Assets/XPak.h"
#include "../../Formatters/CommandLineFormatter.h"
#include "../../Formatters/FormatterUtils.h"
#include "../../ConsoleRig/AttachablePtr.h"
#include "../../ConsoleRig/GlobalServices.h"
#include "../../Utility/Streams/PathUtils.h"


namespace GUILayer
{
	ref class TimerMessageFilter : public System::Windows::Forms::IMessageFilter
	{
	public:
		virtual bool PreFilterMessage(System::Windows::Forms::Message% m)
		{
			if (m.Msg == WM_TIMER && _osRunLoop.get()) {
				// Return true to filter the event out of the message loop (which we will do if the timer id is recognized)
				return _osRunLoop->OnOSTrigger((UINT_PTR)m.WParam.ToPointer());
			}

			return false;
		}

		clix::shared_ptr<OSServices::OSRunLoop_BasicTimer> _osRunLoop;
	};

///////////////////////////////////////////////////////////////////////////////////////////////////
    RenderCore::IThreadContext*		NativeEngineDevice::GetImmediateContext()	{ return _renderDevice->GetImmediateContext().get(); }

    const std::shared_ptr<RenderCore::Techniques::IPipelineAcceleratorPool>& NativeEngineDevice::GetMainPipelineAcceleratorPool()
    {
        return _drawingApparatus->_pipelineAccelerators;
    }

    const std::shared_ptr<RenderCore::Techniques::IImmediateDrawables>& NativeEngineDevice::GetImmediateDrawables()
    {
        return _immediateDrawingApparatus->_immediateDrawables;
    }

    const std::shared_ptr<RenderCore::Techniques::DrawingApparatus>& NativeEngineDevice::GetDrawingApparatus()
    {
        return _drawingApparatus;
    }

    const std::shared_ptr<RenderOverlays::OverlayApparatus>& NativeEngineDevice::GetOverlayApparatus()
    {
        return _immediateDrawingApparatus;
    }

    const std::shared_ptr<RenderCore::Techniques::PrimaryResourcesApparatus>& NativeEngineDevice::GetPrimaryResourcesApparatus()
    {
        return _primaryResourcesApparatus;
    }

    const std::shared_ptr<RenderCore::Techniques::FrameRenderingApparatus>& NativeEngineDevice::GetFrameRenderingApparatus()
    {
        return _frameRenderingApparatus;
    }

    const std::shared_ptr<RenderCore::LightingEngine::LightingEngineApparatus>& NativeEngineDevice::GetLightingEngineApparatus()
    {
        return _lightingEngineApparatus;
    }

    void NativeEngineDevice::ResetFrameBufferPool()
    {
        _frameRenderingApparatus->_frameBufferPool->Reset();
    }

    void NativeEngineDevice::MountTextEntityDocument(StringSection<> mountingPt, StringSection<> documentFileName)
    {
        _entityDocumentMounts.push_back(ToolsRig::MountTextEntityDocument(mountingPt, documentFileName));
    }

    struct CommandLineArgsDigest
    {
        StringSection<> _xleres = "xleres.pak";
        CommandLineArgsDigest(Formatters::CommandLineFormatter<char>&& fmttr)
        {
            StringSection<> keyname;
            for (;;) {
                if (fmttr.TryKeyedItem(keyname)) {
                    if (XlEqStringI(keyname, "xleres"))
                        _xleres = Formatters::RequireStringValue(fmttr);
                } else if (fmttr.PeekNext() == Formatters::FormatterBlob::None) {
                    break;
                } else
                    Formatters::SkipValueOrElement(fmttr);
            }
        }
    };

    struct CmdLineHelper
    {
        int _argc = 0;
        std::vector<const char*> _argv;
        std::vector<std::string> _buffers;

        CmdLineHelper()
        {
            auto args = System::Environment::GetCommandLineArgs();
            _argc = args->Length;
            _buffers.reserve(_argc);
            _argv.reserve(_argc);
            for (int c=0; c<_argc; ++c) {
                _buffers.push_back(clix::marshalString<clix::E_UTF8>(args[c]));
                _argv.push_back(_buffers.back().c_str());
            }
            delete args;
        }
    };

    NativeEngineDevice::NativeEngineDevice(const ConsoleRig::StartupConfig& startupCfg)
    {
        _services = std::make_shared<ConsoleRig::GlobalServices>(startupCfg);
		_assetServices = std::make_shared<::Assets::Services>();
        _fsMounts.push_back(::Assets::MainFileSystem::GetMountingTree()->Mount("rawos", ::Assets::MainFileSystem::GetDefaultFileSystem()));

        CmdLineHelper cmdLineHelper;
        CommandLineArgsDigest cmdLineDigest { Formatters::MakeCommandLineFormatter(cmdLineHelper._argc, cmdLineHelper._argv.data()) };
        std::shared_ptr<::Assets::ArchiveUtility::FileCache> fileCache;
        if (XlEqStringI(MakeFileNameSplitter(cmdLineDigest._xleres).Extension(), "pak")) {
            fileCache = ::Assets::CreateFileCache(4 * 1024 * 1024);
            std::string finalXleRes = cmdLineDigest._xleres.AsString();
            // by default, search next to the executable if we don't have a fully qualified name
            if (::Assets::MainFileSystem::TryGetDesc(finalXleRes)._snapshot._state == ::Assets::FileSnapshot::State::DoesNotExist) {
                char buffer[MaxPath];
                OSServices::GetProcessPath(buffer, dimof(buffer));
                finalXleRes = Concatenate(MakeFileNameSplitter(buffer).DriveAndPath(), "/", finalXleRes);
            }
            _fsMounts.push_back(::Assets::MainFileSystem::GetMountingTree()->Mount("xleres", ::Assets::CreateXPakFileSystem(finalXleRes, fileCache)));
        } else
            _fsMounts.push_back(::Assets::MainFileSystem::GetMountingTree()->Mount("xleres", ::Assets::CreateFileSystem_OS(cmdLineDigest._xleres, _services->GetPollingThread())));

        auto renderAPI = RenderCore::CreateAPIInstance(RenderCore::Techniques::GetTargetAPI());
        const unsigned deviceConfigurationIdx = 0;
        auto capability = renderAPI->QueryFeatureCapability(deviceConfigurationIdx);
        _renderDevice = renderAPI->CreateDevice(deviceConfigurationIdx, capability);
        _immediateContext = _renderDevice->GetImmediateContext();

        _techniquesServices = std::make_shared<RenderCore::Techniques::Services>(_renderDevice);

        _drawingApparatus = std::make_shared<RenderCore::Techniques::DrawingApparatus>(_renderDevice);
        _immediateDrawingApparatus = std::make_shared<RenderOverlays::OverlayApparatus>(_drawingApparatus);
        _primaryResourcesApparatus = std::make_shared<RenderCore::Techniques::PrimaryResourcesApparatus>(_renderDevice);
        _frameRenderingApparatus = std::make_shared<RenderCore::Techniques::FrameRenderingApparatus>(_renderDevice);
        _lightingEngineApparatus = std::make_shared<RenderCore::LightingEngine::LightingEngineApparatus>(_drawingApparatus);
        _previewSceneRegistry = ToolsRig::CreatePreviewSceneRegistry();
        _entityMountingTree = EntityInterface::CreateMountingTree();

        _services->LoadDefaultPlugins();

        _creationThreadId = System::Threading::Thread::CurrentThread->ManagedThreadId;
        RenderCore::Techniques::SetThreadContext(_immediateContext);

        ToolsRig::InvokeCheckCompleteInitialization(_techniquesServices->GetSubFrameEvents(), *_immediateContext);

		auto osRunLoop = std::make_shared<OSServices::OSRunLoop_BasicTimer>((HWND)0);
		OSServices::SetOSRunLoop(osRunLoop);

        auto messageFilter = gcnew TimerMessageFilter();
        messageFilter->_osRunLoop = osRunLoop;
		System::Windows::Forms::Application::AddMessageFilter(messageFilter);
        _messageFilter = messageFilter;
    }

    NativeEngineDevice::~NativeEngineDevice()
    {
        RenderCore::Techniques::SetThreadContext(nullptr);
		if (_messageFilter.get())
			System::Windows::Forms::Application::RemoveMessageFilter(_messageFilter.get());
		OSServices::SetOSRunLoop(nullptr);
        for (auto r=_entityDocumentMounts.rbegin(); r!=_entityDocumentMounts.rend(); ++r)
            ToolsRig::UnmountEntityDocument(*r);
        _services->PrepareForDestruction();
        for (auto r=_fsMounts.rbegin(); r!=_fsMounts.rend(); ++r)
            ::Assets::MainFileSystem::GetMountingTree()->Unmount(*r);
    }

    static ConsoleRig::StartupConfig AsNativeStartupConfig(StartupConfig^ cfg)
    {
        ConsoleRig::StartupConfig result;
        if (cfg && cfg->_applicationName) {
            result._applicationName = clix::marshalString<clix::E_UTF8>(cfg->_applicationName);
        } else
            result._applicationName = clix::marshalString<clix::E_UTF8>(System::Windows::Forms::Application::ProductName);
        return result;
    }

///////////////////////////////////////////////////////////////////////////////////////////////////
    RenderCore::IThreadContext* EngineDevice::GetNativeImmediateContext()
    {
        return _pimpl->GetImmediateContext();
    }

    void EngineDevice::PrepareForShutdown()
    {
        for each(auto i in _shutdownCallbacks) {
            auto callback = dynamic_cast<IOnEngineShutdown^>(i->Target);
            if (callback)
                callback->OnEngineShutdown();
        }
        _shutdownCallbacks->Clear();

        // It's a good idea to force a GC collect here...
        // it will help flush out managed references to native objects
        // before we go through the shutdown steps
        System::GC::Collect();
        System::GC::WaitForPendingFinalizers();
        DelayedDeleteQueue::FlushQueue();
    }

    void EngineDevice::MountTextEntityDocument(System::String^ mountingPt, System::String^ documentFileName)
    {
        auto nativeMountingPt = clix::marshalString<clix::E_UTF8>(mountingPt);
        auto nativeDocumentFileName = clix::marshalString<clix::E_UTF8>(documentFileName);
        _pimpl->MountTextEntityDocument(nativeMountingPt, nativeDocumentFileName);
    }

    void EngineDevice::AddOnShutdown(IOnEngineShutdown^ callback)
    {
        // It will be nicer to do this with delegates, but we can't create a 
        // delegate with captures in C++/CLR
        _shutdownCallbacks->Add(gcnew System::WeakReference(callback));
    }
    
    EngineDevice::EngineDevice(StartupConfig^ startupConfig)
    {
        assert(s_instance == nullptr);
        _shutdownCallbacks = gcnew System::Collections::Generic::List<System::WeakReference^>();

        _pimpl = new NativeEngineDevice(AsNativeStartupConfig(startupConfig));
        s_instance = this;
    }

    EngineDevice::~EngineDevice()
    {
        assert(s_instance == this);
        s_instance = nullptr;

        PrepareForShutdown();
        delete _pimpl;
		_pimpl = nullptr;
    }

    EngineDevice::!EngineDevice() 
    {
		if (_pimpl) {
			System::Diagnostics::Debug::Assert(false, "Non deterministic delete of EngineDevice");
		}
    }
}

