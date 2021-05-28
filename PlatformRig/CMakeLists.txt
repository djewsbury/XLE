
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
        DebugScreensOverlay.cpp)
    set(Headers 
        AllocationProfiler.h
        CameraManager.h
        DebugHotKeys.h
        FrameRig.h
        InputListener.h
        InputTranslator.h
        MainInputHandler.h
        OverlappedWindow.h
        OverlaySystem.h
        PlatformApparatuses.h
        PlatformRigUtil.h
        Screenshot.h
        UnitCamera.h
        DebugScreensOverlay.h)

    set(DebuggingDisplaysSrc 
        #InitDebugDisplays.cpp
        #DebuggingDisplays/BufferUploadDisplay.cpp
        DebuggingDisplays/ConsoleDisplay.cpp
        DebuggingDisplays/CPUProfileDisplay.cpp
        #DebuggingDisplays/DynamicImpostersDisplay.cpp
        DebuggingDisplays/GPUProfileDisplay.cpp
        DebuggingDisplays/HierarchicalSpikesDisplay.cpp
        DebuggingDisplays/HistoricalProfilerDisplay.cpp
        #DebuggingDisplays/PlacementsDisplay.cpp
        #DebuggingDisplays/TestDisplays.cpp
        DebuggingDisplays/InvalidAssetDisplay.cpp
        )
    set(DebuggingDisplaysHeaders 
        DebuggingDisplays/BufferUploadDisplay.h
        DebuggingDisplays/ConsoleDisplay.h
        DebuggingDisplays/CPUProfileDisplay.h
        DebuggingDisplays/DynamicImpostersDisplay.h
        DebuggingDisplays/GPUProfileDisplay.h
        DebuggingDisplays/HierarchicalSpikesDisplay.h
        DebuggingDisplays/HistoricalProfilerDisplay.h
        DebuggingDisplays/PlacementsDisplay.h
        DebuggingDisplays/TestDisplays.h)

    set(MiniSceneEngineSrc
        ../SceneEngine/RayVsModel.cpp
        ../SceneEngine/IntersectionTest.cpp
        ../SceneEngine/PlacementsManager.cpp
        ../SceneEngine/GenericQuadTree.cpp
        ../SceneEngine/DynamicImposters.cpp
        ../SceneEngine/BasicLightingStateDelegate.cpp
        ../SceneEngine/ExecuteScene.cpp
        )

    if (WIN32)
        set(WinSrc WinAPI/AllocationProfiler.cpp WinAPI/InputTranslator.cpp WinAPI/OverlappedWindow.cpp WinAPI/RunLoop_WinAPI.cpp)
        set(WinHeaders WinAPI/RunLoop_WinAPI.h)
    endif()

    add_library(PlatformRig-${metal_name} STATIC ${Src} ${WinSrc} ${Headers} ${WinHeaders} ${DebuggingDisplaysSrc} ${DebuggingDisplaysHeaders}  ${MiniSceneEngineSrc})
    xle_configure_library(PlatformRig-${metal_name})
    target_link_libraries(PlatformRig-${metal_name} PRIVATE RenderCoreLightingEngine-${metal_name} RenderCoreTechniques-${metal_name})

endforeach()
