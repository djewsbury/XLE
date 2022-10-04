
set(Src 
    Metrics.cpp
    DataPacket.cpp
    ResourceSource.cpp)

add_library(BufferUploads-Shared STATIC ${Src})
xle_configure_library(BufferUploads-Shared)
target_link_libraries(BufferUploads-Shared PRIVATE RenderCore Utility ForeignMisc)

foreach (metal_macro metal_name IN ZIP_LISTS MetalSelectMacros MetalSelectName)

    set(PlatformSrc
        BufferUploads_Manager.cpp
        ResourceUploadHelper.cpp
        BatchedResources.cpp
        DX11/PlatformInterfaceDX11.cpp
        OpenGL/PlatformInterfaceOpenGL.cpp)

    add_library(BufferUploads-${metal_name} STATIC ${PlatformSrc})
    xle_configure_library(BufferUploads-${metal_name})
    target_compile_definitions(BufferUploads-${metal_name} PUBLIC ${metal_macro})
    target_link_libraries(BufferUploads-${metal_name} PRIVATE BufferUploads-Shared RenderCoreAssets RenderCore_${metal_name})

endforeach()
