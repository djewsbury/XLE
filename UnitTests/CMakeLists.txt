
####################################################################################################
    ## UnitTests-Core ##
####################################################################################################
add_executable(
    UnitTests-Core
    Utility/Utilities.cpp
    Utility/StreamFormatterTests.cpp
    Utility/ConversionPatterns.cpp
    Utility/ClassAccessorsTests.cpp
    Utility/PreprocessorInterpreterTests.cpp
    Math/BasicMaths.cpp
    Math/MathSerialization.cpp
    OSServices/OSServicesAsync.cpp
    ConsoleRig/DynLibraryBinding.cpp
    Assets/MountingTreeTests.cpp
    Assets/AssetCompilerTests.cpp
    Assets/ArchiveCacheTests.cpp
    Formatters/BinaryFormatterTests.cpp
    )

xle_configure_executable(UnitTests-Core)
target_link_libraries(UnitTests-Core PRIVATE Catch2WithMain)
target_link_libraries(UnitTests-Core PRIVATE Formatters ConsoleRig Assets OSServices Math Utility tiny-process-library)

#
# Also add a dynamic library as a dependency. This will be loaded at runtime by some of the unit tests
# it's a dependency, but not used with target_link_libraries(). So it should compile, but won't
# appear on the linker command line
#
add_library(UnitTestDynLibrary SHARED ConsoleRig/UnitTestDynLibrary.cpp)
xle_configure_dll(UnitTestDynLibrary)
target_link_libraries(UnitTestDynLibrary PRIVATE ConsoleRig Assets OSServices Utility ForeignMisc)

add_dependencies(UnitTests-Core UnitTestDynLibrary)

####################################################################################################
    ## UnitTests-RenderCore ##
####################################################################################################

foreach (metal_macro metal_name IN ZIP_LISTS MetalSelectMacros MetalSelectName)

    add_executable(
        UnitTests-RenderCore-${metal_name}
        RenderCore/Metal/CoordinateSpaces.cpp
        RenderCore/Metal/InputLayout.cpp
        RenderCore/Metal/PipelineTests.cpp
        RenderCore/Metal/MetalTestHelper.cpp
        #RenderCore/Metal/QueryPool.cpp
        RenderCore/Metal/ResourceUpdateAndReadBack.cpp
        RenderCore/Metal/StreamOutputTests.cpp
        RenderCore/Metal/ThreadedRendering.cpp
        )

    xle_configure_executable(UnitTests-RenderCore-${metal_name})
    target_compile_definitions(UnitTests-RenderCore-${metal_name} PUBLIC ${metal_macro})
    target_link_libraries(UnitTests-RenderCore-${metal_name} PRIVATE Catch2WithMain)
    target_link_libraries(UnitTests-RenderCore-${metal_name} PRIVATE RenderCore)
    target_link_libraries(UnitTests-RenderCore-${metal_name} PRIVATE RenderCore_${metal_name})
    xle_executable_post_build(UnitTests-RenderCore-${metal_name})

endforeach()

####################################################################################################
    ## UnitTests-RenderCoreAssetsTechniques ##
####################################################################################################

foreach (metal_macro metal_name IN ZIP_LISTS MetalSelectMacros MetalSelectName)

    add_executable(
        UnitTests-RenderCoreAssetsTechniques-${metal_name}
        RenderCore/Assets/ShaderParserTests.cpp
        RenderCore/Assets/ShaderPatchCollectionTests.cpp
        RenderCore/Assets/TransformationMachineOpt.cpp
        RenderCore/Assets/RenderCoreCompilerTests.cpp
        RenderCore/Assets/FakeModelCompiler.cpp
		RenderCore/Assets/ShaderCompilationTests.cpp
        RenderCore/Assets/TechniqueDelegateTests.cpp
        RenderCore/Assets/TechniqueFileTests.cpp
        RenderCore/Assets/NodeGraphInstantiationTests.cpp
        RenderCore/Assets/RenderMiscTests.cpp
        RenderCore/Assets/BufferUploadsTests.cpp
        RenderCore/Assets/PipelineAcceleratorTests.cpp
        RenderCore/Assets/DrawablesTests.cpp
        RenderCore/Assets/ColladaModelConversion.cpp
        RenderCore/Assets/PipelineConfigurationTests.cpp
        RenderCore/Assets/ImmediateDrawablesTests.cpp
        RenderCore/Assets/RenderPassManagementTests.cpp
        RenderCore/Assets/FrustumCullingTests.cpp
        RenderCore/LightingEngine/LightingEngineTests.cpp
        RenderCore/LightingEngine/LightingEngineTestHelper.cpp
        RenderCore/LightingEngine/GBufferConstructionTests.cpp
        RenderCore/LightingEngine/ShadowPrecisionTests.cpp
        RenderCore/LightingEngine/LightShapeStencilTests.cpp
        RenderCore/LightingEngine/TextureOperatorPerformanceTests.cpp
        RenderCore/Metal/MetalTestHelper.cpp
        EmbeddedRes.cpp
        ../Tools/ToolsRig/VisualisationGeo.cpp
        ../Tools/ToolsRig/DrawablesWriter.cpp
        )

    xle_configure_executable(UnitTests-RenderCoreAssetsTechniques-${metal_name})
    target_compile_definitions(UnitTests-RenderCoreAssetsTechniques-${metal_name} PUBLIC ${metal_macro})
    target_link_libraries(UnitTests-RenderCoreAssetsTechniques-${metal_name} PRIVATE Catch2WithMain)
    target_link_libraries(UnitTests-RenderCoreAssetsTechniques-${metal_name} PRIVATE ShaderParser RenderCoreAssets)
    target_link_libraries(UnitTests-RenderCoreAssetsTechniques-${metal_name} PRIVATE RenderCore)
    target_link_libraries(UnitTests-RenderCoreAssetsTechniques-${metal_name} PRIVATE RenderCore_${metal_name})
    target_link_libraries(UnitTests-RenderCoreAssetsTechniques-${metal_name} PRIVATE RenderCoreTechniques-${metal_name})
    target_link_libraries(UnitTests-RenderCoreAssetsTechniques-${metal_name} PRIVATE RenderCoreLightingEngine-${metal_name})
    target_link_libraries(UnitTests-RenderCoreAssetsTechniques-${metal_name} PRIVATE BufferUploads-${metal_name})
    target_link_libraries(UnitTests-RenderCoreAssetsTechniques-${metal_name} PRIVATE RenderOverlays-${metal_name})
    target_link_libraries(UnitTests-RenderCoreAssetsTechniques-${metal_name} PRIVATE PlatformRig-${metal_name})
    xle_executable_post_build(UnitTests-RenderCoreAssetsTechniques-${metal_name})

    # copy the collada dll into the same folder as the output executable
    add_dependencies(UnitTests-RenderCoreAssetsTechniques-${metal_name} ColladaConversion)
    add_custom_command(
        TARGET UnitTests-RenderCoreAssetsTechniques-${metal_name} POST_BUILD 
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "$<TARGET_FILE:ColladaConversion>"
        "$<TARGET_FILE_DIR:UnitTests-RenderCoreAssetsTechniques-${metal_name}>/$<TARGET_FILE_NAME:ColladaConversion>")

    if (${metal_name} STREQUAL "Vulkan")
        add_custom_command(
            TARGET UnitTests-RenderCoreAssetsTechniques-${metal_name} POST_BUILD 
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "$<TARGET_FILE:dxil>"
            "$<TARGET_FILE_DIR:UnitTests-RenderCoreAssetsTechniques-${metal_name}>/$<TARGET_FILE_NAME:dxil>")

        add_custom_command(
            TARGET UnitTests-RenderCoreAssetsTechniques-${metal_name} POST_BUILD 
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "$<TARGET_FILE:dxcompiler>"
            "$<TARGET_FILE_DIR:UnitTests-RenderCoreAssetsTechniques-${metal_name}>/$<TARGET_FILE_NAME:dxcompiler>")
    endif ()

endforeach()

####################################################################################################
    ## UnitTests-Interactive ##
####################################################################################################

foreach (metal_macro metal_name IN ZIP_LISTS MetalSelectMacros MetalSelectName)

    add_executable(
        UnitTests-Interactive-${metal_name}
        Interactive/InteractiveTestHelper.cpp
        Interactive/VolumeClipTests.cpp
        Interactive/StraightSkeletonTests.cpp
        EmbeddedRes.cpp
        RenderCore/Metal/MetalTestHelper.cpp
        ../Tools/ToolsRig/VisualisationGeo.cpp
        ../Tools/ToolsRig/DrawablesWriter.cpp
        )

    xle_configure_executable(UnitTests-Interactive-${metal_name})
    target_compile_definitions(UnitTests-Interactive-${metal_name} PUBLIC ${metal_macro})
    target_link_libraries(UnitTests-Interactive-${metal_name} PRIVATE Catch2WithMain)
    target_link_libraries(UnitTests-Interactive-${metal_name} PRIVATE ShaderParser RenderCoreAssets)
    target_link_libraries(UnitTests-Interactive-${metal_name} PRIVATE RenderCore)
    target_link_libraries(UnitTests-Interactive-${metal_name} PRIVATE RenderCore_${metal_name})
    target_link_libraries(UnitTests-Interactive-${metal_name} PRIVATE RenderCoreTechniques-${metal_name})
    target_link_libraries(UnitTests-Interactive-${metal_name} PRIVATE RenderCoreLightingEngine-${metal_name})
    target_link_libraries(UnitTests-Interactive-${metal_name} PRIVATE BufferUploads-${metal_name})
    target_link_libraries(UnitTests-Interactive-${metal_name} PRIVATE RenderOverlays-${metal_name})
    target_link_libraries(UnitTests-Interactive-${metal_name} PRIVATE PlatformRig-${metal_name})
    xle_executable_post_build(UnitTests-Interactive-${metal_name})

endforeach()
