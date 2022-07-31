
foreach (metal_macro metal_name IN ZIP_LISTS MetalSelectMacros MetalSelectName)

    set(Src
        Apparatuses.cpp
        CommonResources.cpp
        CommonUtils.cpp
        CompiledShaderPatchCollection.cpp
        DeferredShaderResource.cpp
        DescriptorSetAccelerator.cpp
        Drawables.cpp
        ImmediateDrawables.cpp
        ParsingContext.cpp
        PipelineAccelerator.cpp
        PipelineOperators.cpp
        RenderPass.cpp
        RenderPassUtils.cpp
        ShaderVariationSet.cpp
        DeformerConstruction.cpp
        SimpleModelRenderer.cpp
        SkinDeformer.cpp
        SystemUniformsDelegate.cpp
        TechniqueDelegates.cpp
        Techniques.cpp
        TechniqueUtils.cpp
        Services.cpp
        ModelCache.cpp
        UniformDelegateManager.cpp
        PipelineCollection.cpp
        TextureCompilerUtil.cpp
        DeformAccelerator.cpp
        DeformGeoInternal.cpp
        DeformGeometryInfrastructure.cpp
        DeformUniformsInfrastructure.cpp
        DrawableConstructor.cpp
        CompiledLayoutPool.cpp
        ModelRendererConstruction.cpp
        LightWeightBuildDrawables.cpp
        ManualDrawables.cpp
        ResourceConstructionContext.cpp
        SubFrameUtil.cpp
        ../../Tools/ToolsRig/VisualisationGeo.cpp
        )

    add_library(RenderCoreTechniques-${metal_name} STATIC ${Src})
    xle_configure_library(RenderCoreTechniques-${metal_name})
    target_compile_definitions(RenderCoreTechniques-${metal_name} PUBLIC ${metal_macro})
    target_link_libraries(RenderCoreTechniques-${metal_name} PUBLIC RenderCoreAssets RenderCore_${metal_name} RenderCore Utility ForeignMisc)

endforeach()
