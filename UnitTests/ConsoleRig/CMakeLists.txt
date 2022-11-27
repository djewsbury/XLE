
####################################################################################################
    ## ToolsRigProtoDLL ##
####################################################################################################

# not enabled currently -- but useful tool for generating a dll containing just the native code that
# we link to GUILayer. This can be used to isolate problems in that dll

foreach (metal_macro metal_name IN ZIP_LISTS MetalSelectMacros MetalSelectName)

    add_library(
        ToolsRigProtoDLL-${metal_name} SHARED
        ToolsRigProtoDLL.cpp
        )

    xle_configure_library(ToolsRigProtoDLL-${metal_name})
    target_compile_definitions(ToolsRigProtoDLL-${metal_name} PUBLIC ${metal_macro})
    target_link_libraries(ToolsRigProtoDLL-${metal_name} PRIVATE Catch2WithMain)
    target_link_libraries(ToolsRigProtoDLL-${metal_name} PRIVATE ShaderParser RenderCoreAssets)
    target_link_libraries(ToolsRigProtoDLL-${metal_name} PRIVATE RenderCore)
    target_link_libraries(ToolsRigProtoDLL-${metal_name} PRIVATE RenderCore_${metal_name})
    target_link_libraries(ToolsRigProtoDLL-${metal_name} PRIVATE RenderCoreTechniques-${metal_name})
    target_link_libraries(ToolsRigProtoDLL-${metal_name} PRIVATE RenderCoreLightingEngine-${metal_name})
    target_link_libraries(ToolsRigProtoDLL-${metal_name} PRIVATE BufferUploads-${metal_name})
    target_link_libraries(ToolsRigProtoDLL-${metal_name} PRIVATE RenderOverlays-${metal_name})
    target_link_libraries(ToolsRigProtoDLL-${metal_name} PRIVATE PlatformRig-${metal_name})
    target_link_libraries(ToolsRigProtoDLL-${metal_name} PRIVATE ToolsRig-${metal_name})
    xle_executable_post_build(ToolsRigProtoDLL-${metal_name})

    if (${metal_name} STREQUAL "Vulkan")
        add_custom_command(
            TARGET ToolsRigProtoDLL-${metal_name} POST_BUILD 
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "$<TARGET_FILE:dxil>"
            "$<TARGET_FILE_DIR:ToolsRigProtoDLL-${metal_name}>/$<TARGET_FILE_NAME:dxil>")

        add_custom_command(
            TARGET ToolsRigProtoDLL-${metal_name} POST_BUILD 
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "$<TARGET_FILE:dxcompiler>"
            "$<TARGET_FILE_DIR:ToolsRigProtoDLL-${metal_name}>/$<TARGET_FILE_NAME:dxcompiler>")
    endif ()

endforeach()
