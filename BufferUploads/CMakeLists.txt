
foreach (metal_macro metal_name IN ZIP_LISTS MetalSelectMacros MetalSelectName)

    set(Src 
        BufferUploads.cpp
        BufferUploads_Manager.cpp
        DataPacket.cpp
        MemoryManagement.cpp
        PlatformInterface.cpp
        ResourceSource.cpp
        ThreadContext.cpp)
    set(Headers 
        BufferUploads_Manager.h
        DataPacket.h
        Documentation.h
        IBufferUploads_Forward.h
        IBufferUploads.h
        MemoryManagement.h
        Metrics.h
        PlatformInterface.h
        ResourceLocator.h
        ResourceSource.h
        ThreadContext.h)

    list(APPEND Src
        DX11/PlatformInterfaceDX11.cpp
        OpenGL/PlatformInterfaceOpenGL.cpp
        Vulkan/PlatformInterfaceVulkan.cpp)

    add_library(BufferUploads-${metal_name} STATIC ${Src} ${Headers})
    xle_configure_library(BufferUploads-${metal_name})
    target_compile_definitions(BufferUploads-${metal_name} PUBLIC ${metal_macro})
    target_link_libraries(BufferUploads-${metal_name} PRIVATE ForeignMisc)

endforeach()
