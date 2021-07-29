set(Src 
    AssetUtils.cpp  
    MaterialCompiler.cpp
    MaterialScaffold.cpp
    ModelScaffold.cpp
    PredefinedCBLayout.cpp
    PredefinedDescriptorSetLayout.cpp
    PredefinedPipelineLayout.cpp
    RawAnimationCurve.cpp
    RawMaterial.cpp
    # Services.cpp  (*1 some files not really required for processing dlls -- eg model conversion dlls -- so it's convenient to remove them here)
    ShaderPatchCollection.cpp
    SkinningScaffold.cpp
    TransformationCommands.cpp
    PipelineConfigurationUtils.cpp
    TextureCompiler.cpp
    TextureLoaders.cpp
    ../GeoProc/GeometryAlgorithm.cpp
    ../GeoProc/GeoProcUtil.cpp
    ../GeoProc/MeshDatabase.cpp
    ../GeoProc/NascentAnimController.cpp
    ../GeoProc/NascentCommandStream.cpp
    ../GeoProc/NascentModel.cpp
    ../GeoProc/NascentObjectsSerialize.cpp
    ../GeoProc/NascentRawGeometry.cpp
    ../GeoProc/NascentSkeletonMachine.cpp
	../GeoProc/DequantAnalysisTools.cpp)
set(Headers
    AssetUtils.h
    MaterialCompiler.h
    MaterialScaffold.h
    ModelImmutableData.h
    ModelScaffold.h
    PredefinedCBLayout.h
    PredefinedDescriptorSetLayout.h
    PredefinedPipelineLayout.h
    RawAnimationCurve.h
    RawMaterial.h
    Services.h
    ShaderPatchCollection.h
    SkeletonScaffoldInternal.h
    TransformationCommands.h
    PipelineConfigurationUtils.h
    ../GeoProc/GeometryAlgorithm.h
    ../GeoProc/GeoProcUtil.h
    ../GeoProc/MeshDatabase.h
    ../GeoProc/NascentAnimController.h
    ../GeoProc/NascentCommandStream.h
    ../GeoProc/NascentModel.h
    ../GeoProc/NascentObjectGuid.h
    ../GeoProc/NascentObjectsSerialize.h
    ../GeoProc/NascentRawGeometry.h
    ../GeoProc/NascentSkeletonMachine.h)

add_library(RenderCoreAssets STATIC ${Src} ${Headers})
xle_configure_library(RenderCoreAssets)
target_link_libraries(RenderCoreAssets PUBLIC ShaderParser RenderCore ConsoleRig Assets Math OSServices Utility ForeignMisc)

add_library(RenderCoreAssetsServices STATIC "Services.cpp")
xle_configure_library(RenderCoreAssetsServices)
target_link_libraries(RenderCoreAssetsServices PUBLIC RenderCore ConsoleRig Assets Math OSServices Utility ForeignMisc)

include(${FOREIGN_DIR}/Compressonator/FindCompressonator.cmake)
find_compressonator()
target_link_libraries(RenderCoreAssets PRIVATE compressonator)

if (WIN32)
    target_link_libraries(RenderCoreAssets PRIVATE DirectXTex)
    target_compile_definitions(RenderCoreAssets PRIVATE ENABLE_DXTEX=1)
endif ()
