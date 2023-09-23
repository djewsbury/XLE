
#pragma once

#include "../Assets/InitializerPack.h"

using namespace Assets::Literals;

#define ILLUM_TECH "xleres/Config/Illum.tech"_initializer
#define ILLUM_LEGACY_TECH "xleres/Config/IllumLegacy.tech"_initializer

#define MAIN_PIPELINE "xleres/Config/main.pipeline"_initializer
#define LIGHTING_OPERATOR_PIPELINE "xleres/TechniqueLibrary/LightingEngine/light-resolve-operator.pipeline"_initializer
#define GENERAL_OPERATOR_PIPELINE "xleres/TechniqueLibrary/LightingEngine/general-operator.pipeline"_initializer
#define SHADOW_TEMPLATE_DS "xleres/Config/shadow-template.pipeline"_initializer
#define SEQUENCER_DS "xleres/Config/sequencer.pipeline"_initializer
#define NOPATCHES_MATERIAL_DS "xleres/Objects/default.material.pipeline"_initializer

#define BASIC2D_VERTEX_HLSL "xleres/TechniqueLibrary/Basic/basic2D.vertex.hlsl"
#define BASIC3D_VERTEX_HLSL "xleres/TechniqueLibrary/Basic/basic3D.vertex.hlsl"

#define BASIC_COMPUTE_HLSL "xleres/TechniqueLibrary/Basic/basic.compute.hlsl"
#define BASIC_GEO_HLSL "xleres/TechniqueLibrary/Basic/basic.geo.hlsl"
#define BASIC_PIXEL_HLSL "xleres/TechniqueLibrary/Basic/basic.pixel.hlsl"

#define NO_PATCHES_VERTEX_HLSL "xleres/TechniqueLibrary/Standard/nopatches.vertex.hlsl"
#define NO_PATCHES_PIXEL_HLSL "xleres/TechniqueLibrary/Standard/nopatches.pixel.hlsl"

#define DEFERRED_LIGHT_OPERATOR_VERTEX_HLSL "xleres/Deferred/light-resolve-operator.vertex.hlsl"
#define DEFERRED_LIGHT_OPERATOR_PIXEL_HLSL "xleres/Deferred/light-resolve-operator.pixel.hlsl"

#define FORWARD_PIPELINE "xleres/Config/forward.pipeline"_initializer

#define CASCADE_VIS_HLSL "xleres/Deferred/debugging/cascadevis.pixel.hlsl"
#define HIGHLIGHT_VIS_PIXEL_HLSL "xleres/ToolsRig/Vis/HighlightVis.pixel.hlsl"
#define OUTLINE_VIS_PIXEL_HLSL "xleres/ToolsRig/Vis/outlinehighlight.pixel.hlsl"
#define VIS_PIPELINE "xleres/ToolsRig/Vis/vis.pipeline"
#define SOLID_WIREFRAME_GEO_HLSL "xleres/ToolsRig/Vis/solidwireframe.geo.hlsl"
#define SOLID_WIREFRAME_PIXEL_HLSL "xleres/ToolsRig/Vis/solidwireframe.pixel.hlsl"

#define METRICS_RENDER_VERTEX_HLSL "xleres/TechniqueLibrary/Profiling/metricsrender.vertex.hlsl"
#define METRICS_RENDER_GEO_HLSL "xleres/TechniqueLibrary/Profiling/metricsrender.geo.hlsl"
#define METRICS_RENDER_PIXEL_HLSL "xleres/TechniqueLibrary/Profiling/metricsrender.pixel.hlsl"

#define RENDEROVERLAYS_SHAPES_HLSL "xleres/RenderOverlays/ShapesFrameworkEntry.hlsl"
#define RENDEROVERLAYS_SHAPES_MATERIAL "xleres/RenderOverlays/DebuggingShapes.material"
#define RENDEROVERLAYS_SEL_PRECONFIG "xleres/RenderOverlays/Preconfiguration.hlsl"
#define RENDEROVERLAYS_SEPARABLE_FILTER "xleres/RenderOverlays/separable-filter.compute.hlsl"
#define RENDEROVERLAYS_SHAPES_PIPELINE "xleres/RenderOverlays/ShapesFramework.pipeline"
#define RENDEROVERLAYS_HIERARCHICAL_BLUR_HLSL "xleres/RenderOverlays/hierarchical-blur.compute.hlsl"
#define FONTS_DAT "xleres/DefaultResources/fonts/fonts.dat"

#define SCENE_ENGINE_RES "xleres/TechniqueLibrary/SceneEngine"

#define AREA_LIGHT_TECH "xleres/ToolsRig/arealight.material"_initializer
#define MESH_MARKER_TECH "xleres/Config/Legacy/MeshMarker.tech"_initializer

#define NORMALS_FITTING_TEXTURE "xleres/DefaultResources/normalsfitting.dds:LT"_initializer
#define DISTINCT_COLORS_TEXTURE "xleres/DefaultResources/distinctcolors.dds:T"_initializer
#define BALANCED_NOISE_TEXTURE "xleres/DefaultResources/balanced_noise.dds:LT"_initializer

#define SSR_CLASSIFY_TILES_HLSL "xleres/TechniqueLibrary/LightingEngine/ssr-classify-tiles.compute.hlsl"
#define SSR_INTERSECT_HLSL "xleres/TechniqueLibrary/LightingEngine/ssr-intersect.compute.hlsl"
#define SSR_RESOLVE_SPATIAL_HLSL "xleres/TechniqueLibrary/LightingEngine/ssr-resolve-spatial.compute.hlsl"
#define SSR_RESOLVE_TEMPORAL_HLSL "xleres/TechniqueLibrary/LightingEngine/ssr-resolve-temporal.compute.hlsl"
#define SSR_REFLECTIONS_BLUR_HLSL "xleres/TechniqueLibrary/LightingEngine/ssr-reflections-blur.compute.hlsl"
#define SSR_PIPELINE "xleres/TechniqueLibrary/LightingEngine/ssr.pipeline"_initializer

#define HIERARCHICAL_DEPTHS_HLSL "xleres/TechniqueLibrary/LightingEngine/hierarchical-depths.compute.hlsl"

#define TILED_LIGHTING_PREPARE_HLSL "xleres/TechniqueLibrary/LightingEngine/tiledlighting-prepare.pixel.hlsl"
#define TILED_LIGHTING_PREPARE_PIPELINE "xleres/TechniqueLibrary/LightingEngine/tiledlighting-prepare.pipeline"

#define AO_COMPUTE_HLSL "xleres/TechniqueLibrary/LightingEngine/ao.compute.hlsl"
#define TONEMAP_ACES_COMPUTE_HLSL "xleres/TechniqueLibrary/LightingEngine/tonemap-aces.compute.hlsl"
#define BLOOM_COMPUTE_HLSL "xleres/TechniqueLibrary/LightingEngine/bloom.compute.hlsl"
#define BLOOM_PIPELINE "xleres/TechniqueLibrary/LightingEngine/bloom.pipeline"_initializer
#define BLOOM_FILTER_COMPUTE_HLSL "xleres/TechniqueLibrary/LightingEngine/bloom-filter.compute.hlsl"
#define FAST_MIP_CHAIN_COMPUTE_HLSL "xleres/TechniqueLibrary/LightingEngine/fast-mip-chain.compute.hlsl"
#define TAA_COMPUTE_HLSL "xleres/TechniqueLibrary/LightingEngine/taa.compute.hlsl"

#define SKY_PIXEL_HLSL "xleres/TechniqueLibrary/LightingEngine/sky.pixel.hlsl"

#define VISUALIZE_ATTACHMENT_PIXEL_HLSL "xleres/ToolsHelper/visualize-attachment.pixel.hlsl"

#define IBL_PREFILTER_HLSL "xleres/ToolsHelper/IBLPrefilter.hlsl"
#define EQUIRECTANGULAR_TO_CUBE_HLSL "xleres/ToolsHelper/EquirectangularToCube.hlsl"
#define EQUIRECTANGULAR_TO_CUBE_BOKEH_HLSL "xleres/ToolsHelper/EquirectangularToCubeBokeh.hlsl"
#define TOOLSHELPER_OPERATORS_PIPELINE "xleres/ToolsHelper/operators.pipeline"_initializer

#define SKIN_COMPUTE_HLSL "xleres/Deform/skin.compute.hlsl"
#define SKIN_PIPELINE "xleres/Deform/skin.pipeline"_initializer
#define DEFORM_ENTRY_HLSL "xleres/Deform/deform-entry.compute.hlsl"

#define GLOSS_LUT_TEXTURE "xleres/TechniqueLibrary/LightingEngine/glosslut.texture"_initializer

#define PREFIX_HLSL "xleres/TechniqueLibrary/Framework/Prefix.hlsl"
