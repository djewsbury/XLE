
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
        SimpleVisualization.cpp)
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
    target_link_libraries(RenderOverlays-${metal_name} PRIVATE RenderCoreTechniques-${metal_name} ForeignMisc freetype)
    target_link_libraries(RenderOverlays-${metal_name} PRIVATE BufferUploads-${metal_name})

endforeach()
