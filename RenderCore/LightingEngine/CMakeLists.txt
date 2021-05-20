
foreach (metal_macro metal_name IN ZIP_LISTS MetalSelectMacros MetalSelectName)

	add_library(
		RenderCoreLightingEngine-${metal_name} STATIC 
		LightingEngine.cpp
		LightUniforms.cpp
		RenderStepFragments.cpp
		ShadowPreparer.cpp
		ShadowUniforms.cpp
		LightingEngineApparatus.cpp
		ForwardLightingDelegate.cpp
		DeferredLightingDelegate.cpp
		DeferredLightingResolve.cpp
		StandardLightScene.cpp
		)

	xle_configure_library(RenderCoreLightingEngine-${metal_name})
	target_link_libraries(RenderCoreLightingEngine-${metal_name} PUBLIC RenderCoreTechniques-${metal_name})

endforeach()
