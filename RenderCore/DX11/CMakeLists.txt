
set(Src
    Device.cpp
    Annotator.cpp
    Metal/Buffer.cpp
    Metal/CompiledShaderByteCode.cpp
    Metal/DeviceContext.cpp
    Metal/DX11Utils.cpp
    Metal/Format.cpp
    Metal/FrameBuffer.cpp
    Metal/InputLayout.cpp
    Metal/ObjectFactory.cpp
    Metal/QueryPool.cpp
    Metal/Resource.cpp
    Metal/Shader.cpp
    Metal/State.cpp
    Metal/TextureView.cpp
    Metal/PipelineLayout.cpp
    Metal/FunctionLinkingGraph.cpp)

add_library(RenderCore_DX11 STATIC ${Src} ${Headers})
xle_configure_library(RenderCore_DX11)
target_link_libraries(RenderCore_DX11 PRIVATE RenderCore OSServices ConsoleRig Utility ForeignMisc)
target_sources(RenderCore_DX11 INTERFACE ${CMAKE_CURRENT_LIST_DIR}/RegisterDX11.cpp)
target_link_libraries(RenderCore_DX11 PUBLIC dxguid.lib)

# Attach "d3dcompiler_47" as a required DLL
if (${CMAKE_SIZEOF_VOID_P} EQUAL 8)
    set(X64_OR_X86 "x64")
else ()
    set(X64_OR_X86 "x86")
endif ()

find_file(
    D3D11COMPILERDLL-SRC d3dcompiler_47.dll
    HINTS
        "$ENV{WindowsSdkVerBinPath}/${X64_OR_X86}")
set_property(TARGET RenderCore_DX11 APPEND PROPERTY RequiredDLLs ${D3D11COMPILERDLL-SRC})
