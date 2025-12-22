
foreach (metal_macro metal_name IN ZIP_LISTS MetalSelectMacros MetalSelectName)

    set(Src
        Apparatuses.cpp
        CommonResources.cpp
        CommonUtils.cpp
        DataDrivenAccelerators.cpp
        DeferredShaderResource.cpp
        DeformAccelerator.cpp
        DeformerConstruction.cpp
        DeformGeoInternal.cpp
        DeformGeometryInfrastructure.cpp
        DeformUniformsInfrastructure.cpp
        DescriptorSetAccelerator.cpp
        DrawableConstructor.cpp
        Drawables.cpp
        ImmediateDrawables.cpp
        LightWeightBuildDrawables.cpp
        ManualDrawables.cpp
        ParsingContext.cpp
        PipelineAccelerator.cpp
        PipelineCollection.cpp
        PipelineLayoutDelegate.cpp
        PipelineOperators.cpp
        RenderPass.cpp
        RenderPassUtils.cpp
        ResourceConstructionContext.cpp
        Services.cpp
        ShaderPatchInstantiationUtil.cpp
        ShaderVariationSet.cpp
        SimpleModelRenderer.cpp
        SkinDeformer.cpp
        SpriteTechnique.cpp
        SubFrameUtil.cpp
        SystemUniformsDelegate.cpp
        TechniqueDelegates.cpp
        Techniques.cpp
        TechniqueUtils.cpp
        UniformDelegateManager.cpp
        ../../Tools/ToolsRig/VisualisationGeo.cpp
    )

    add_library(RenderCoreTechniques-${metal_name} STATIC ${Src})
    xle_configure_library(RenderCoreTechniques-${metal_name})
    target_compile_definitions(RenderCoreTechniques-${metal_name} PUBLIC ${metal_macro})
    target_link_libraries(RenderCoreTechniques-${metal_name} PUBLIC BufferUploads-${metal_name} RenderCoreAssets RenderCore_${metal_name} RenderCore Utility ForeignMisc)

endforeach()
