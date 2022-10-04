add_library(
    RenderCoreAssets
    STATIC
    AssetUtils.cpp  
    MaterialCompiler.cpp
    MaterialScaffold.cpp
    ModelScaffold.cpp
    PredefinedCBLayout.cpp
    PredefinedDescriptorSetLayout.cpp
    PredefinedPipelineLayout.cpp
    RawAnimationCurve.cpp
    RawMaterial.cpp
    ShaderPatchCollection.cpp
    SkinningScaffold.cpp
    TransformationCommands.cpp
    PipelineConfigurationUtils.cpp
    #TextureCompiler.cpp        moved into the Techniques project for now, due to dependencies on that project (& bufferuploads)
    TextureLoaders.cpp
    MergedAnimationSetCompiler.cpp
    AnimationBindings.cpp
    AnimationSet.cpp
    SkeletonMachine.cpp
    ModelRendererConstruction.cpp
    CompoundObject.cpp
    ../GeoProc/GeometryAlgorithm.cpp
    ../GeoProc/GeoProcUtil.cpp
    ../GeoProc/MeshDatabase.cpp
    ../GeoProc/NascentAnimController.cpp
    ../GeoProc/NascentCommandStream.cpp
    ../GeoProc/NascentModel.cpp
    ../GeoProc/NascentObjectsSerialize.cpp
    ../GeoProc/NascentRawGeometry.cpp
    ../GeoProc/NascentSkeletonHelper.cpp
	../GeoProc/DequantAnalysisTools.cpp)

xle_configure_library(RenderCoreAssets)
target_link_libraries(RenderCoreAssets PUBLIC BufferUploads-Shared ShaderParser RenderCore ConsoleRig Assets Math OSServices Utility ForeignMisc)

if (WIN32)
    target_link_libraries(RenderCoreAssets PRIVATE DirectXTex)
    target_compile_definitions(RenderCoreAssets PRIVATE ENABLE_DXTEX=1)
endif ()
