
foreach (metal_macro metal_name IN ZIP_LISTS MetalSelectMacros MetalSelectName)

    set(Src
        NativeModelViewer.cpp
        ../Shared/SampleRig.cpp)

    if (WIN32)
        list(APPEND Src WinAPI/WindowStartup.cpp)
    endif()

    # we need the "WIN32" flag in add_executable here to ensure that cmake adds the /subsystem:windows flag to the linker command line (otherwise we get a console app) 
    add_executable(NativeModelViewer-${metal_name} WIN32 "${Src};${Headers}")
    xle_configure_executable(NativeModelViewer-${metal_name})
    xle_configure_binary_output(NativeModelViewer-${metal_name})

    target_link_libraries(NativeModelViewer-${metal_name} PRIVATE RenderCore_${metal_name} RenderCoreTechniques-${metal_name} BufferUploads-${metal_name})
    target_link_libraries(NativeModelViewer-${metal_name} PRIVATE RenderOverlays-${metal_name} PlatformRig-${metal_name} RenderCoreLightingEngine-${metal_name})
    target_link_libraries(NativeModelViewer-${metal_name} PRIVATE ToolsRig-${metal_name})

endforeach()
