
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
		SunSourceConfiguration.cpp
		SSAOOperator.cpp
		SHCoefficients.cpp
		HierarchicalDepths.cpp
		ScreenSpaceReflections.cpp
		BlueNoiseGenerator.cpp
		LightTiler.cpp
		SkyOperator.cpp
		ShadowProbes.cpp
		LightingDelegateUtil.cpp
		ForwardPlusLightScene.cpp
		ToneMapOperator.cpp
		TextureCompilerUtil.cpp
		GBufferOperator.cpp
		UtilityLightingDelegate.cpp
		AAOperators.cpp
		PostProcessOperators.cpp
		../Assets/TextureCompiler.cpp           # moved here while figuring out the architecture -- due to dependencies with TextureCompilerUtil.cpp
		)

	xle_configure_library(RenderCoreLightingEngine-${metal_name})
	target_link_libraries(RenderCoreLightingEngine-${metal_name} PUBLIC RenderCoreTechniques-${metal_name})

	include(${FOREIGN_DIR}/Compressonator/FindCompressonator.cmake)
	find_compressonator()
	target_link_libraries(RenderCoreLightingEngine-${metal_name} PRIVATE compressonator)

endforeach()
