
if (NOT ${XLE_MAIN_CMAKE_DIR})
endif ()

add_subdirectory(${XLE_MAIN_CMAKE_DIR}/../../Utility Utility)
add_subdirectory(${XLE_MAIN_CMAKE_DIR}/../../OSServices OSServices)
add_subdirectory(${XLE_MAIN_CMAKE_DIR}/../../Math Math)
add_subdirectory(${XLE_MAIN_CMAKE_DIR}/../../Assets Assets)
add_subdirectory(${XLE_MAIN_CMAKE_DIR}/../../ConsoleRig ConsoleRig)
add_subdirectory(${XLE_MAIN_CMAKE_DIR}/../../RenderCore RenderCore)
add_subdirectory(${XLE_MAIN_CMAKE_DIR}/../../RenderCore/Metal RenderCoreMetal)
add_subdirectory(${XLE_MAIN_CMAKE_DIR}/../../RenderCore/Techniques RenderCoreTechniques)
add_subdirectory(${XLE_MAIN_CMAKE_DIR}/../../RenderCore/LightingEngine RenderCoreLightingEngine)
add_subdirectory(${XLE_MAIN_CMAKE_DIR}/../../RenderCore/Assets RenderCoreAssets)
add_subdirectory(${XLE_MAIN_CMAKE_DIR}/../../RenderCore/BufferUploads BufferUploads)
add_subdirectory(${XLE_MAIN_CMAKE_DIR}/../../ShaderParser ShaderParser)
add_subdirectory(${XLE_MAIN_CMAKE_DIR}/../../Formatters Formatters)

add_subdirectory(${XLE_MAIN_CMAKE_DIR}/../../PlatformRig PlatformRig)
add_subdirectory(${XLE_MAIN_CMAKE_DIR}/../../RenderOverlays RenderOverlays)
#add_subdirectory(${XLE_MAIN_CMAKE_DIR}/../../SceneEngine SceneEngine)
#add_subdirectory(${XLE_MAIN_CMAKE_DIR}/../../FixedFunctionModel FixedFunctionModel)

add_subdirectory(${XLE_MAIN_CMAKE_DIR}/../../Tools/ToolsRig ToolsRig)

add_subdirectory(${XLE_MAIN_CMAKE_DIR}/../../ColladaConversion ColladaConversion)

add_subdirectory(${XLE_MAIN_CMAKE_DIR}/../../Foreign ForeignMisc)
