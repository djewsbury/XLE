
At a minimum, you need enough to compile and debug executables using a llvm, vscode, cmake & ninja environment
Other compilation environments are possible, but this is the simplest and uses the least platform-specific parts

If you haven't set up a windows dev environment like this before; you'll probably need the things listed below

Brief install instructions for windows (using clang & vscode)
* install vscode
* install llvm for windows (I'm using 11.0.0, win64) from https://releases.llvm.org/download.html [winget install --id LLVM.LLVM]
* install cmake
* change vscode settings to point to cmake executable location
* install ninja
* install windows 11 sdk (or get it from Visual Studio)
* this gets you 99% of the way there... but it turns out you also need Visual Studio 2019 for 2 .lib files: oldnames.lib & msvcrtd.lib. Install from https://visualstudio.microsoft.com/downloads/. Be aware of the licensing requirements, however
    - (it might be possible to get away with just installing "Build Tools for Visual Studio" here, but I haven't tried that)
* vs build tools lts: https://learn.microsoft.com/en-us/visualstudio/releases/2022/release-history

Note that Git and ssh can be a hassle for windows. If you don't already have a setup you like, the following
is what I tend to do:
* ensure OpenSSH is enabled in the windows optional features
* windows docs for creating keys, etc: https://learn.microsoft.com/en-us/windows-server/administration/openssh/openssh_keymanagement
* configure git to use OpenSSH: git config --global core.sshCommand "C:/Windows/System32/OpenSSH/ssh.exe"
* you can now load keys using "ssh-add $env:USERPROFILE\.ssh\id_ecdsa"

There are some symlinks in the git repo. For these to work on windows, you may need to do the following:
* set the core.symlinks git configuration option to true [git config --global core.symlinks true]
* enable symlinks from a non-elevated user either through the local group policy editor or by enabling "developer mode" in system settings
* (there is more documentation amount symlinks with git on windows online)

For RenderCore you'll need a little more
* To get the OpenGLES target running, you'll need an implementation of OpenGLES for windows. Use google's project angle: https://github.com/google/angle/blob/master/doc/DevSetup.md
* Google's instructions failed for me the first time -- I had to explicitly set the env variable "vs2019_install" (my VS install is not on drive C:)
* After the first cmake configure, you should get an error message saying GLES could not be found. There's no default location for windows, you must configure it manually
* set the cmake cache variables XLE_OPENGLES_INCLUDE_DIR & XLE_OPENGLES_LIBRARY_DIR
* install java jre (choco install javaruntime) -- used by antlr for language parsing [winget install --id  Oracle.JavaRuntimeEnvironment]
* install vulkan sdk: https://vulkan.lunarg.com/sdk/home
* install vulkan runtime as well: https://vulkan.lunarg.com/sdk/home (at least ensure it matches your sdk version)
* download dxcompiler from https://github.com/microsoft/DirectXShaderCompiler/releases (last used April 2021 release). You may need to set the XLE_DXCOMPILER_DIR cmake cache dir to the base dir where this is extracted
* Download AMD compressonator from the github releases page (https://github.com/GPUOpen-Tools/compressonator/releases/tag/V4.1.5083) (last used V4.1.5083). Set the XLE_COMPRESSONATOR_DIR cmake cache dir to the base dir where this is extracted

PhysX
* if you need physx, start by clone from https://github.com/NVIDIA-Omniverse/PhysX
* create projects using physx/generate_projects.bat
* for the cuda build you need cuda toolkit: https://developer.nvidia.com/cuda-toolkit

Some optional changes that make things a little bit nicer:
* in launch configuration set working directory (ie, launch.json -- "cwd": "${workspaceFolder}/Working")
* also use Microsoft debugger; launch.json -- "type": "cppvsdbg"
* install clangd in vscode for static analysis based swigglies and identifier highlighting

Temporary fixup in some submodules:
* in some submodules, there are some small changes I either haven't figured out entirely or just haven't got around to uploading to a fork:
* in Foreign/DirectXTex/DirectXTex/Shaders/CompileShaders.cmd, there are references to fxc.exe. This is part of the windows sdk and the batch file expects it to be in the path. Tou may need to add a line that sets the MSVC environment variables, ie:
    call "D:\Program Files (x86)\Microsoft Visual Studio\2019\Community\Common7\Tools\VsDevCmd.bat"
    (this wouldn't be required if compiling via VSStudio, but is via cmdline, or vscode). Alternatively, add a line that points to the windows sdk bin (eg: set PATH=$PATH;C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x64)

To use the CMakePresets.json infrastructure
* these just make screwing around with the cmake cache variables a little easier (should be supported in both vscode & Visual Studio)
* rename CMakeUserPresets.json.example to CMakeUserPresets.json
* adjust the user settings in CMakeUserPresets.json as required (there are some user-specific paths in there, for example)
* you might need to restart vscode, but the configurations from CMakeUserPresets.json should now be selectable in the cmake plugin
* There's some options for configurability in the cmake cache variables (such as what GFXAPIs to compile in, etc). You can search for "XLE_" in the cmake cache variables UI in vscode to get a list of them. Recommended settings are just:
* XLE_VULKAN_ENABLE ON
* XLE_DXCOMPILER_DIR & XLE_COMPRESSONATOR_DIR set appropriately (see above)
* once you've gotten CMakeUserPresets.json right, it's a good idea to completely delete your "build" output folder and start from complete scratch

** Note that cmake can be extremely temperamental and issue prone! ** 
    If you run into unexpected issues, they can sometimes be solved by completely deleting the "build" folder output
    (ie, even a clean rebuild is not enough)

