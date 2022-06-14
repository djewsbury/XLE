
foreach (metal_macro metal_name IN ZIP_LISTS MetalSelectMacros MetalSelectName)

    set(Src
        CameraManager.cpp
        DebugHotKeys.cpp
        FrameRig.cpp
        InputListener.cpp
        MainInputHandler.cpp
        OverlaySystem.cpp
        PlatformApparatuses.cpp
        PlatformRigUtil.cpp
        Screenshot.cpp
        UnitCamera.cpp
        DebugScreensOverlay.cpp
        DebugScreenRegistry.cpp)

    set(DebuggingDisplaysSrc 
        #InitDebugDisplays.cpp
        DebuggingDisplays/BufferUploadDisplay.cpp
        DebuggingDisplays/ConsoleDisplay.cpp
        DebuggingDisplays/CPUProfileDisplay.cpp
        DebuggingDisplays/DeformAcceleratorDisplay.cpp
        #DebuggingDisplays/DynamicImpostersDisplay.cpp
        DebuggingDisplays/GPUProfileDisplay.cpp
        DebuggingDisplays/HierarchicalSpikesDisplay.cpp
        DebuggingDisplays/HistoricalProfilerDisplay.cpp
        #DebuggingDisplays/PlacementsDisplay.cpp
        #DebuggingDisplays/TestDisplays.cpp
        DebuggingDisplays/InvalidAssetDisplay.cpp
        DebuggingDisplays/QuickMetricsDisplay.cpp
        DebuggingDisplays/PipelineAcceleratorDisplay.cpp
        DebuggingDisplays/ModelCacheDisplay.cpp
        )

    if (${metal_macro} STREQUAL "SELECT_VULKAN")
        list(APPEND DebuggingDisplaysSrc DebuggingDisplays/VulkanMemoryDisplay.cpp)
    endif()

    set(MiniSceneEngineSrc
        ../SceneEngine/RayVsModel.cpp
        ../SceneEngine/IntersectionTest.cpp
        ../SceneEngine/PlacementsManager.cpp
        ../SceneEngine/GenericQuadTree.cpp
        ../SceneEngine/DynamicImposters.cpp
        ../SceneEngine/BasicLightingStateDelegate.cpp
        ../SceneEngine/ExecuteScene.cpp
        ../SceneEngine/Noise.cpp
        )

    if (WIN32)
        set(WinSrc WinAPI/AllocationProfiler.cpp WinAPI/InputTranslator.cpp WinAPI/OverlappedWindow.cpp WinAPI/RunLoop_WinAPI.cpp)
    endif()

    add_library(PlatformRig-${metal_name} STATIC ${Src} ${WinSrc} ${DebuggingDisplaysSrc} ${MiniSceneEngineSrc})
    xle_configure_library(PlatformRig-${metal_name})
    target_link_libraries(PlatformRig-${metal_name} PRIVATE ToolsRig-${metal_name} RenderCoreLightingEngine-${metal_name} RenderCoreTechniques-${metal_name})

endforeach()
