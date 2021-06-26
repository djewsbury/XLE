
set(Src
    Device.mm
    Metal/BasicLabelWithNSError.mm
    Metal/DeviceContext.cpp
    Metal/Format.cpp
    Metal/FrameBuffer.cpp
    Metal/InputLayout.cpp
    Metal/ObjectFactory.mm
    Metal/PipelineLayout.cpp
    Metal/QueryPool.cpp
    Metal/Resource.cpp
    Metal/Shader.cpp
    Metal/State.cpp
    Metal/TextureView.cpp)

add_library(RenderCore_AppleMetal STATIC ${Src} ${Headers})
xle_configure_library(RenderCore_AppleMetal)
# Every source file in this project; and every source file in every project that directly uses RenderCore::Metal
# must support mixing Obj-C and C++ code. This is because the underlying Apple Metal API is actually ObjC, not C or C++
target_compile_options(RenderCore_AppleMetal PUBLIC -ObjC++)            
target_link_libraries(RenderCore_AppleMetal PRIVATE ForeignMisc)
target_sources(RenderCore_AppleMetal INTERFACE ${CMAKE_CURRENT_LIST_DIR}/RegisterAppleMetal.cpp)
target_link_libraries(RenderCore_AppleMetal PUBLIC "-framework Foundation" "-framework Metal" objc)
target_link_libraries(RenderCore_AppleMetal PRIVATE "-framework Cocoa" "-framework QuartzCore")

