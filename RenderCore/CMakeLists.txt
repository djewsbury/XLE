list(APPEND Src
    Format.cpp
    FrameBufferDesc.cpp
    DeviceInitialization.cpp
    MinimalShaderSource.cpp
    RenderUtils.cpp
    ResourceUtils.cpp
    ShaderLangUtil.cpp
    ShaderService.cpp
    StateDesc.cpp
    UniformsStream.cpp)
file(GLOB Headers "*.h")

add_library(RenderCore STATIC ${Src} ${Headers})
xle_configure_library(RenderCore)
target_link_libraries(RenderCore PUBLIC OSServices Utility ForeignMisc)

# todo -- ConsoleRig required just for SharedPkt/SubframeHeap globals -- would be better if we could remove this dependency
target_link_libraries(RenderCore PUBLIC ConsoleRig)

# todo -- Assets is also required, for ShaderService. Maybe this should be moved into RenderCore::Assets?
target_link_libraries(RenderCore PUBLIC Assets)