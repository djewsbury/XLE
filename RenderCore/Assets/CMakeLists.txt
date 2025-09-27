add_library(
	RenderCoreAssets
	STATIC
	AssetUtils.cpp  
	MaterialCompiler.cpp
	CompiledMaterialSet.cpp
	ModelScaffold.cpp
	PredefinedCBLayout.cpp
	PredefinedDescriptorSetLayout.cpp
	PredefinedPipelineLayout.cpp
	RawAnimationCurve.cpp
	RawMaterial.cpp
	ShaderPatchCollection.cpp
	SkinningScaffold.cpp
	TransformationCommands.cpp
	PipelineConfigurationUtils.cpp
	TextureCompiler.cpp
	TextureLoaders.cpp
	MergedAnimationSetCompiler.cpp
	AnimationBindings.cpp
	AnimationSet.cpp
	SkeletonMachine.cpp
	ModelRendererConstruction.cpp
	CompoundObject.cpp
	ModelCompilationConfiguration.cpp
	../GeoProc/GeometryAlgorithm.cpp
	../GeoProc/MeshDatabase.cpp
	../GeoProc/NascentAnimController.cpp
	../GeoProc/NascentCommandStream.cpp
	../GeoProc/NascentMaterialTable.cpp
	../GeoProc/NascentModel.cpp
	../GeoProc/NascentObjectsSerialize.cpp
	../GeoProc/NascentRawGeometry.cpp
	../GeoProc/NascentSkeletonHelper.cpp
	../GeoProc/DequantAnalysisTools.cpp)

xle_configure_library(RenderCoreAssets)
target_link_libraries(RenderCoreAssets PUBLIC BufferUploads-Shared ShaderParser RenderCore ConsoleRig Assets Math OSServices Utility ForeignMisc)

if (WIN32)
	target_link_libraries(RenderCoreAssets PRIVATE DirectXTexMinimal)
	target_compile_definitions(RenderCoreAssets PRIVATE ENABLE_DXTEX=1)
endif ()

if (XLE_COMPRESSONATOR_ENABLE)

	include(${FOREIGN_DIR}/Compressonator/FindCompressonator.cmake)
	find_compressonator()
	target_link_libraries(RenderCoreAssets PRIVATE compressonator)

	# we need to use /ignore:4099 because compressonator includes a reference to vc140.pdb on a build drive
	target_link_options(RenderCoreAssets PUBLIC "LINKER:/ignore:4099")

	target_compile_definitions(RenderCoreAssets PRIVATE -DXLE_COMPRESSONATOR_ENABLE=1)

endif ()