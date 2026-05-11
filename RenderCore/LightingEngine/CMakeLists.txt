
foreach (metal_macro metal_name IN ZIP_LISTS MetalSelectMacros MetalSelectName)

	add_library(
		RenderCoreLightingEngine-${metal_name} STATIC 
		AAOperators.cpp
		BlueNoiseGenerator.cpp
		DeferredLightingDelegate.cpp
		DeferredLightingResolve.cpp
		ForwardLightingDelegate.cpp
		ForwardPlusLightScene.cpp
		GBufferOperator.cpp
		HierarchicalDepths.cpp
		LightingDelegateUtil.cpp
		LightingEngine.cpp
		LightingEngineApparatus.cpp
		LightTiler.cpp
		LightUniforms.cpp
		PostProcessOperators.cpp
		RenderStepFragments.cpp
		ScreenSpaceReflections.cpp
		ShadowPreparer.cpp
		ShadowProbes.cpp
		ShadowUniforms.cpp
		SHCoefficients.cpp
		SimpleOperators.cpp
		SkyOperator.cpp
		SSAOOperator.cpp
		StandardLightScene.cpp
		StencilingGeometry.cpp
		SunSourceConfiguration.cpp
		TextureCompilerUtil.cpp
		ToneMapOperator.cpp
		UtilityLightingDelegate.cpp
		)

	xle_configure_library(RenderCoreLightingEngine-${metal_name})
	target_link_libraries(RenderCoreLightingEngine-${metal_name} PUBLIC RenderCoreTechniques-${metal_name})

endforeach()
