
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
        RenderStateResolver.cpp
        ShaderVariationSet.cpp
        SimpleModelDeform.cpp
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
        ../../Tools/ToolsRig/VisualisationGeo.cpp
        )
    set(Headers
        Apparatuses.h
        CommonBindings.h
        CommonResources.h
        CommonUtils.h
        CompiledRenderStateSet.h
        CompiledShaderPatchCollection.h
        DeferredShaderResource.h
        DrawableDelegates.h
        Drawables.h
        ImmediateDrawables.h
        ParsingContext.h
        PipelineAccelerator.h
        RenderPass.h
        RenderPassUtils.h
        RenderStateResolver.h
        ShaderVariationSet.h
        SimpleModelDeform.h
        SimpleModelRenderer.h
        SkinDeformer.h
        SubFrameEvents.h
        TechniqueDelegates.h
        TechniqueMaterial.h
        Techniques.h
        TechniqueUtils.h
        Services.h
        ModelCache.h
        )

    add_library(RenderCoreTechniques-${metal_name} STATIC ${Src} ${Headers})
    xle_configure_library(RenderCoreTechniques-${metal_name})
    target_compile_definitions(RenderCoreTechniques-${metal_name} PUBLIC ${metal_macro})
    target_link_libraries(RenderCoreTechniques-${metal_name} PUBLIC RenderCoreAssets RenderCore_${metal_name} RenderCore Utility ForeignMisc)

endforeach()
