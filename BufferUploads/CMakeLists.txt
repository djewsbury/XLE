
foreach (metal_macro metal_name IN ZIP_LISTS MetalSelectMacros MetalSelectName)

    set(Src 
        Metrics.cpp
        BufferUploads_Manager.cpp
        DataPacket.cpp
        ResourceUploadHelper.cpp
        ResourceSource.cpp
        BatchedResources.cpp)

    list(APPEND Src
        DX11/PlatformInterfaceDX11.cpp
        OpenGL/PlatformInterfaceOpenGL.cpp)

    add_library(BufferUploads-${metal_name} STATIC ${Src})
    xle_configure_library(BufferUploads-${metal_name})
    target_compile_definitions(BufferUploads-${metal_name} PUBLIC ${metal_macro})
    target_link_libraries(BufferUploads-${metal_name} PRIVATE RenderCoreAssets RenderCore_${metal_name} RenderCore Utility ForeignMisc)

endforeach()
