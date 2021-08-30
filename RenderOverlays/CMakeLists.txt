
foreach (metal_macro metal_name IN ZIP_LISTS MetalSelectMacros MetalSelectName)

    set(Src
        AnimationVisualization.cpp
        DebuggingDisplay.cpp
        Font.cpp
        FontRectanglePacking.cpp
        FontRendering.cpp
        FT_Font.cpp
        HighlightEffects.cpp
        OverlayContext.cpp
        OverlayPrimitives.cpp
        OverlayUtils.cpp
        SimpleVisualization.cpp)
    set(Headers
        AnimationVisualization.h
        DebuggingDisplay.h
        Font.h
        FontRendering.h
        FontRectanglePacking.h
        FT_Font.h
        HighlightEffects.h
        IOverlayContext.h
        OverlayContext.h
        OverlayPrimitives.h
        OverlayUtils.h)
    set(OverlaysSrc
        Overlays/Browser.cpp
        Overlays/OceanSettings.cpp
        Overlays/ShadowFrustumDebugger.cpp
        Overlays/TestMaterialSettings.cpp
        Overlays/ToneMapSettings.cpp
        Overlays/VolFogSettings.cpp)
    set(OverlaysHeaders 
        Overlays/Browser.h
        Overlays/OceanSettings.h
        Overlays/ShadowFrustumDebugger.h
        Overlays/TestMaterialSettings.h
        Overlays/ToneMapSettings.h
        Overlays/VolFogSettings.h)

    add_library(RenderOverlays-${metal_name} STATIC ${Src} ${Headers})
    xle_configure_library(RenderOverlays-${metal_name})
    target_link_libraries(RenderOverlays-${metal_name} PRIVATE RenderCoreTechniques-${metal_name} ForeignMisc freetype)
    target_link_libraries(RenderOverlays-${metal_name} PRIVATE BufferUploads-${metal_name})

endforeach()
