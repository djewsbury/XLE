
foreach (metal_macro metal_name IN ZIP_LISTS MetalSelectMacros MetalSelectName)

	add_library(
		ToolsRig-${metal_name} STATIC
		BasicManipulators.cpp
		DivergentAsset.cpp
		#GenerateAO.cpp
		ManipulatorsRender.cpp
		MaterialVisualisation.cpp
		ModelVisualisation.cpp
		ObjectPlaceholders.cpp
		PlacementsManipulators.cpp
		PreviewSceneRegistry.cpp
		#TerrainConversion.cpp
		#TerrainManipulators.cpp
		#TerrainManipulatorsCommon.cpp
		TerrainManipulatorsInterface.cpp
		#TerrainOp.cpp
		#TerrainShadowOp.cpp
		#VisualisationGeo.cpp		(in techniques for now)
		VisualisationUtils.cpp
		DrawablesWriter.cpp
		ToolsRigServices.cpp
		ShaderLab.cpp
		ShaderLab_LightingEngine.cpp
		SampleUtils.cpp
		MiscUtils.cpp
		../EntityInterface/EntityInterface.cpp
		../EntityInterface/FormatterAdapters.cpp
		../EntityInterface/RetainedEntities.cpp
		../EntityInterface/TweakableEntityDocument.cpp
		../EntityInterface/TweakableWidgets.cpp
		#../EntityInterface/GameObjects.cpp
		../EntityInterface/PlacementEntities.cpp
		#../EntityInterface/EnvironmentSettings.cpp
		../EntityInterface/LightingEngineEntityDocument.cpp
		)
	xle_configure_library(ToolsRig-${metal_name})
	target_link_libraries(ToolsRig-${metal_name} PRIVATE RenderCoreTechniques-${metal_name})
	target_link_libraries(ToolsRig-${metal_name} PRIVATE yogacore)

endforeach()
