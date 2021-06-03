
add_library(
    ColladaConversion SHARED
    ColladaConversion.cpp
    ColladaDocument.cpp
    ConversionConfig.cpp
    DLLInterface.cpp
    SAnimation.cpp
    ScaffoldParsingUtil.cpp
    SCommandStream.cpp
    SEffect.cpp
    SRawGeometry.cpp)
xle_configure_dll(ColladaConversion)
target_link_libraries(ColladaConversion PRIVATE RenderCoreAssets ConsoleRig Assets OSServices Utility ForeignMisc)
set_target_properties(ColladaConversion PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${XLE_BINARY_OUTPUT_DIR})
