// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)


/*!
    \mainpage

    # XLE prototype engine

    \section s0 Main namespaces

        \subsection ss0 Rendering
        \li RenderCore::Metal_DX11
        \li RenderCore
        \li BufferUploads
        \li SceneEngine

        \subsection ss1 Utility
        \li Utility
        \li Math
        \li RenderOverlays
        \li ConsoleRig

    \section s1 Getting started

        \subsection SSetup Solution setup
        XLE contains many self contained project files. Some of these compile into
        .lib statically linked libraries; and others compile into .dll libraries.

        Most projects are self-explanatory. But these may not be clear:
            \li **Foreign** This contains some small foreign libraries that don't have their own project file
            \li **ConsoleRig** This contains the behaviour for the debugging console, debugging output and cvars.
            \li **PlatformRig** Platform initialisation and OS event handling.
            \li **RenderOverlays** Contains code for rendering debugging and profiling tools over the normal game scene.
            \li **BufferUploads** manages some streaming tasks and background uploads of data to the GPU

        In Visual Studio, add dependencies to the projects you want to use. In some cases you must select which
        compilation configuration to use. In particular, for the following projects, you must select either OpenGL
        or DirectX11:
            \li BufferUploads
            \li RenderCore
            \li RenderOverlays

        To do this, select either "Debug-OpenGL" or "Debug-DX11" / "Release-OpenGL" or "Release-DX11" in solution
        configuration dialog. 

        Note that RenderCore_DX11 always compiles with a "-DX11" configuration, and RenderCore_OpenGLES always compiles
        with a "-OpenGL" configuration. The one that's not used will not be linked in. But it's handy to compile it,
        anyway, because it finds compile errors sooner.

        \subsection CC Compilation configurations
        XLE uses a standard set of 3 compilation configurations:
            \li **Debug** sets _DEBUG, XL_DEBUG
            \li **Profile** sets NDEBUG, PROFILE, XL_PROFILE
            \li **Release** sets NDEBUG, _RELEASE, XL_RELEASE

        Profile and Release both compile with the same compilation settings (optimisations on). But Profile may
        contain some extra profiling tools built into the code. 
        Generally, we want to use Profile for day-to-day execution on programmer, artists & designer machines. Release
        should be used for final builds, and for QA builds.

        \subsection Init Initialisation Steps
        Getting started with a new project. Here are the common initialisation steps for Win32 API-style platform:

        \code{.cpp}

            //  OSServices::WindowsSupport::VanillaWindow represents a simple window.
            //  The window is created in the constructor, and destroyed in the destructor.
            //  Note that normally we should create the window, and handle the Win32 message
            //  loop in the same thread. So, by creating the window here, this thread becomes
            //  the main api thread.
            //
            //  We need a window, even in fullscreen modes. In DirectX11, there is no "exclusive
            //  mode" any more. So, the easiest way to go fullscreen is to just make a window that
            //  covers the entire screen.
        OSServices::OverlappedWindow window;
        auto clientRect = window.GetRect();

            //  Construct the "Console" object from ConsoleRig. This is a standard debugging
            //  tool that many system use.
        auto console             = std::make_unique<ConsoleRig::Console>();

            //  Construct the RenderCore device object. This object represents one physical
            //  graphics card (or, at least one method of rendering). When creating the device,
            //  the system must select which graphics card to use, and set some initialization
            //  parameters. After the device is created, we can construct D3D objects, and we can
            //  render to offscreen render targets.
        auto renderDevice        = RenderCore::CreateDevice();

            //  Create a presentation chain bound to the window. This allows us to render to
            //  this window. Normally a game will have one main presentation chain. But some
            //  tools might need multiple chains.
            //
            //  Note that the presentation chain is directly bound to a window. Sometimes 
            //  Win32 API messages will require presentation chain changes (for example, 
            //  when we receive a WM_SIZE message, we will often resize the presentation chain).
            //  But also, sometimes presentation chain operations will send messages back to the
            //  window. When this happens, the presentation chain can stall waiting for a response
            //  from the window. This is important, because it can cause deadlocks in certain situations.
            //
            //  To solve this, it's recommended to do presentation chain management in the same thread
            //  as the main windows API thread. Windows handles this case in a way that will not cause
            //  deadlocks.
        auto presentationChain   = renderDevice->CreatePresentationChain(
            window.GetUnderlyingHandle(), 
            clientRect.second[0] - clientRect.first[0], clientRect.second[1] - clientRect.first[1]);

            //  Create the buffer uploads manager. This will be required by SceneEngine code for creating
            //  and managing device resources.
        auto bufferUploads       = BufferUploads::CreateManager(renderDevice.get());

            //  Note that all of these create functions return smart pointers. So these objects will
            //  automatically be destroyed, as they go out of scope.

        \endcode
   

*/
