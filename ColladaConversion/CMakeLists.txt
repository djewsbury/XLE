
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
xle_configure_binary_output(ColladaConversion)