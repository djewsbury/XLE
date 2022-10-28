
At a minimum, you need enough to compile and debug executables using a llvm, vscode, cmake & ninja environment
Other compilation environments are possible, but this is the simpliest and uses the least platform-specific parts

If you haven't set up a windows dev environment like this before; you'll probably need the things listed below

Brief install instructions for windows (using clang & vscode)
* install vscode
* install llvm for windows (I'm using 11.0.0, win64) from https://releases.llvm.org/download.html (you must add LLVM to the path during installation; because otherwise CMake Tools & CMake can't find llvm-rc -- somewhat oddly and surprisingly)
    - or use "choco install llvm"
* install cmake (I used choco install cmake)
* change vscode settings to point to cmake executable location
* install ninja (I used choco install ninja)
* install windows 10 sdk (https://developer.microsoft.com/en-us/windows/downloads/windows-10-sdk/)
* this gets you 99% of the way there... but it turns out you also need Visual Studio 2019 for 2 .lib files: oldnames.lib & msvcrtd.lib. Install from https://visualstudio.microsoft.com/downloads/. Be aware of the licensing requirements, however
    - (it might be possible to get away with just installing "Build Tools for Visual Studio" here, but I haven't tried that)

Note that Git and ssh can be a hassle for windows. If you don't already have a setup you like, the following
is what I tend to do:
* install chocolatey (https://chocolatey.org/install)
* install cmder (choco install cmder)
* this brings with it git (in the folder C:\tools\Cmder\vendor\git-for-windows\bin)
* enable openssh for windows using "Add or remove programs"/"Optional features"/"Add feature" -> add "OpenSSH Client"
* this gives you ssh-keygen, etc. You should use that to generate a key called ~/.ssh/id_rsa
* git doesn't play well with the ssh-agent in Windows' OpenSSH Client
* install, you must run "start-ssh-agent" from within cmder. This is a script within the cmder install, and will start an agent and load that key
* you may want to setup the cmder scripts so that start-ssh-agent is called every time 
* you also need to setup the path to git in settings.json for vscode ("git.path": "C:\\tools\\Cmder\\vendor\\git-for-windows\\bin\\git.exe")

There are some symlinks in the git repo. For these to work on windows, you may need to do the following:
* set the core.symlinks git configuration option to true
* enable symlinks from a non-elevated user either through the local group policy editor or by enabling "developer mode" in system settings
* (there is more documentation amount symlinks with git on windows online)

For RenderCore you'll need a little more
* To get the OpenGLES target running, you'll need an implementation of OpenGLES for windows. Use google's project angle: https://github.com/google/angle/blob/master/doc/DevSetup.md
* Google's instructions failed for me the first time -- I had to explicitly set the env variable "vs2019_install" (my VS install is not on drive C:)
* After the first cmake configure, you should get an error message saying GLES could not be found. There's no default location for windows, you must configure it manually
* set the cmake cache variables XLE_OPENGLES_INCLUDE_DIR & XLE_OPENGLES_LIBRARY_DIR
* install java jre (choco install javaruntime)
* install vulkan sdk: https://vulkan.lunarg.com/sdk/home. You'll also need the "Additional SDK components" (unzip into the same SDK directory, this the debug libs). Debug libs are required to against debug XLE components because of the iterator debugging functionality in the MSVC standard library
* download dxcompiler from https://github.com/microsoft/DirectXShaderCompiler/releases (last used April 2021 release). You may need to set the XLE_DXCOMPILER_DIR cmake cache dir to the base dir where this is extracted
* Download AMD compressonator from the github releases page (https://github.com/GPUOpen-Tools/compressonator/releases/tag/V4.1.5083) (last used V4.1.5083). Set the XLE_COMPRESSONATOR_DIR cmake cache dir to the base dir where this is extracted

Some optional changes that make things a little bit nicer:
* in launch configuration set working directory (ie, launch.json -- "cwd": "${workspaceFolder}/Working")
* also use Microsoft debugger; launch.json -- "type": "cppvsdbg"
* when using the microsoft compiler, you can use the microsoft intellisense plugins for vscode. However for clang-only, you might want to use a solution built around clang static analysis
    * CCLS is an option here -- "choco install ccls" and install ccls vscode extension
    * also consider setting the following in settings.json:
        - "C_Cpp.autocomplete": "Disabled",
        - "C_Cpp.formatting": "Disabled",
        - "C_Cpp.errorSquiggles": "Disabled",
        - "C_Cpp.intelliSenseEngine": "Disabled"

Temporary fixup in some submodules:
* in some submodules, there are some small changes I either haven't figured out entirely or just haven't got around to uploading to a fork:
* in Foreign/freetype/CMakeLists.txt, remove the file "ftver.rc"
* in Foreign/DirectXTex/DirectXTex/Shaders/CompileShaders.cmd, you may need to add a line that sets the MSVC environment variables, ie:
    call "D:\Program Files (x86)\Microsoft Visual Studio\2019\Community\Common7\Tools\VsDevCmd.bat"
    (this wouldn't be required if compiling via VSStudio, but is via cmdline, or vscode)

To use the CMakePresets.json infrastructure
* these just make screwing around with the cmake cache variables a little easier (should be supported in both vscode & Visual Studio)
* rename CMakeUserPresets.json.example to CMakeUserPresets.json
* adjust the user settings in CMakeUserPresets.json as required (there are some user-specific paths in there, for example)
* you might need to restart vscode, but the configurations from CMakeUserPresets.json should now be selectable in the cmake plugin
* There's some options for configurability in the cmake cache variables (such as what GFXAPIs to compile in, etc). You can search for "XLE_" in the cmake cache variables UI in vscode to get a list of them. Recommended settings are just:
* XLE_VULKAN_ENABLE ON
* XLE_DXCOMPILER_DIR & XLE_COMPRESSONATOR_DIR set appropriately (see above)
