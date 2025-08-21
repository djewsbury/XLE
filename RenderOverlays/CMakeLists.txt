
option (XLE_FREETYPE_ENABLE "Enables font handling via FreeType" ON)

foreach (metal_macro metal_name IN ZIP_LISTS MetalSelectMacros MetalSelectName)

    set(Src
        AnimationVisualization.cpp
        DebuggingDisplay.cpp
        Font.cpp
        FontRendering.cpp
        FT_Font.cpp
        HighlightEffects.cpp
        OverlayContext.cpp
        OverlayPrimitives.cpp
        CommonWidgets.cpp
        SimpleVisualization.cpp
        LayoutEngine.cpp
        OverlayEffects.cpp
        ShapesRendering.cpp
        OverlayApparatus.cpp)
    set(OverlaysSrc
        #Overlays/Browser.cpp
        #Overlays/OceanSettings.cpp
        #Overlays/ShadowFrustumDebugger.cpp
        #Overlays/TestMaterialSettings.cpp
        Overlays/ToneMapSettings.cpp
        #Overlays/VolFogSettings.cpp
        )

    add_library(RenderOverlays-${metal_name} STATIC ${Src} ${OverlaysSrc})
    xle_configure_library(RenderOverlays-${metal_name})
    target_link_libraries(RenderOverlays-${metal_name} PRIVATE RenderCoreTechniques-${metal_name} ForeignMisc)
    target_link_libraries(RenderOverlays-${metal_name} PRIVATE BufferUploads-${metal_name})
    target_link_libraries(RenderOverlays-${metal_name} PRIVATE yogacore)

    if (XLE_FREETYPE_ENABLE)
        target_link_libraries(RenderOverlays-${metal_name} PRIVATE freetype)
        target_compile_definitions(RenderOverlays-${metal_name} PRIVATE -DXLE_FREETYPE_ENABLE=1)
    endif()

endforeach()
