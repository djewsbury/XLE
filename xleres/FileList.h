
#pragma once

#define SKINNING_VERTEX_HLSL "xleres/TechniqueLibrary/Core/Animation/skinning.vertex.hlsl"
#define SKINNING_GEO_HLSL "xleres/TechniqueLibrary/Core/Animation/skinning.geo.hlsl"
#define SKINNING_VIA_TBUFFER "xleres/TechniqueLibrary/Core/Animation/skinning_viatbuffer.hlsl"

#define ILLUM_FORWARD_VERTEX_HLSL "xleres/Forward/illum.vertex.hlsl"
#define ILLUM_FORWARD_PIXEL_HLSL "xleres/Forward/illum.pixel.hlsl"
#define ILLUM_FORWARD_UNLIT_PIXEL_HLSL "xleres/TechniqueLibrary/Standard/unlit.pixel.hlsl"

#define ILLUM_TECH "xleres/TechniqueLibrary/Config/Illum.tech"
#define UNLIT_TECH "xleres/TechniqueLibrary/Config/Legacy/Unlit.tech"
#define ILLUM_LEGACY_TECH "xleres/TechniqueLibrary/Config/Legacy/IllumLegacy.tech"
#define ILLUM_LEGACY_DS "xleres/TechniqueLibrary/Config/Legacy/IllumLegacy.ds"

#define MAIN_PIPELINE "xleres/TechniqueLibrary/Config/main.pipeline"
#define IMMEDIATE_PIPELINE "xleres/TechniqueLibrary/Config/immediate.pipeline"
#define LIGHTING_OPERATOR_PIPELINE "xleres/TechniqueLibrary/LightingEngine/light-resolve-operator.pipeline"
#define GENERAL_OPERATOR_PIPELINE "xleres/TechniqueLibrary/LightingEngine/general-operator.pipeline"
#define SHADOW_TEMPLATE_DS "xleres/TechniqueLibrary/Config/shadow-template.pipeline"
#define SEQUENCER_DS "xleres/TechniqueLibrary/Config/sequencer.pipeline"
#define MATERIAL_DS "xleres/TechniqueLibrary/Config/material.pipeline"

#define ROOT_SIGNATURE_CFG "xleres/TechniqueLibrary/System/RootSignature.cfg"
#define ROOT_SIGNATURE_COMPUTE_CFG "xleres/TechniqueLibrary/System/RootSignatureCS.cfg"

#define BASIC2D_VERTEX_HLSL "xleres/TechniqueLibrary/Basic/basic2D.vertex.hlsl"
#define BASIC3D_VERTEX_HLSL "xleres/TechniqueLibrary/Basic/basic3D.vertex.hlsl"

#define BASIC_COMPUTE_HLSL "xleres/TechniqueLibrary/Basic/basic.compute.hlsl"
#define BASIC_GEO_HLSL "xleres/TechniqueLibrary/Basic/basic.geo.hlsl"
#define BASIC_PIXEL_HLSL "xleres/TechniqueLibrary/Basic/basic.pixel.hlsl"

#define NO_PATCHES_VERTEX_HLSL "xleres/TechniqueLibrary/Standard/nopatches.vertex.hlsl"
#define NO_PATCHES_PIXEL_HLSL "xleres/TechniqueLibrary/Standard/nopatches.pixel.hlsl"

#define DEFERRED_LIGHT_OPERATOR_VERTEX_HLSL "xleres/Deferred/light-resolve-operator.vertex.hlsl"
#define DEFERRED_LIGHT_OPERATOR_PIXEL_HLSL "xleres/Deferred/light-resolve-operator.pixel.hlsl"

#define FORWARD_PIPELINE "xleres/TechniqueLibrary/Config/forward.pipeline"

#define CASCADE_VIS_HLSL "xleres/Deferred/debugging/cascadevis.pixel.hlsl"
#define HIGHLIGHT_VIS_PIXEL_HLSL "xleres/TechniqueLibrary/ToolsRig/Vis/HighlightVis.pixel.hlsl"
#define OUTLINE_VIS_PIXEL_HLSL "xleres/TechniqueLibrary/ToolsRig/Vis/outlinehighlight.pixel.hlsl"
#define SOLID_WIREFRAME_GEO_HLSL "xleres/TechniqueLibrary/ToolsRig/Vis/solidwireframe.geo.hlsl"
#define SOLID_WIREFRAME_PIXEL_HLSL "xleres/TechniqueLibrary/ToolsRig/Vis/solidwireframe.pixel.hlsl"

#define METRICS_RENDER_VERTEX_HLSL "xleres/TechniqueLibrary/Profiling/metricsrender.vertex.hlsl"
#define METRICS_RENDER_GEO_HLSL "xleres/TechniqueLibrary/Profiling/metricsrender.geo.hlsl"
#define METRICS_RENDER_PIXEL_HLSL "xleres/TechniqueLibrary/Profiling/metricsrender.pixel.hlsl"

#define AO_COMPUTE_HLSL "xleres/TechniqueLibrary/LightingEngine/ao.compute.hlsl"

#define RENDEROVERLAYS_SHAPES_HLSL "xleres/TechniqueLibrary/RenderOverlays/dd/ShapesFrameworkEntry.hlsl"
#define RENDEROVERLAYS_SHAPES_MATERIAL "xleres/TechniqueLibrary/RenderOverlays/dd/DebuggingShapes.material"
#define RENDEROVERLAYS_SEL_PRECONFIG "xleres/TechniqueLibrary/RenderOverlays/dd/Preconfiguration.hlsl"

#define SCENE_ENGINE_RES "xleres/TechniqueLibrary/SceneEngine"

#define AREA_LIGHT_TECH "xleres/TechniqueLibrary/ToolsRig/arealight.material"
#define MESH_MARKER_TECH "xleres/TechniqueLibrary/Config/Legacy/MeshMarker.tech"

#define NORMALS_FITTING_TEXTURE "xleres/DefaultResources/normalsfitting.dds:LT"
#define DISTINCT_COLORS_TEXTURE "xleres/DefaultResources/distinctcolors.dds:T"
#define BALANCED_NOISE_TEXTURE "xleres/DefaultResources/balanced_noise.dds:LT"

#define SSR_CLASSIFY_TILES_HLSL "xleres/TechniqueLibrary/LightingEngine/ssr-classify-tiles.compute.hlsl"
#define SSR_INTERSECT_HLSL "xleres/TechniqueLibrary/LightingEngine/ssr-intersect.compute.hlsl"
#define SSR_RESOLVE_SPATIAL_HLSL "xleres/TechniqueLibrary/LightingEngine/ssr-resolve-spatial.compute.hlsl"
#define SSR_RESOLVE_TEMPORAL_HLSL "xleres/TechniqueLibrary/LightingEngine/ssr-resolve-temporal.compute.hlsl"
#define SSR_REFLECTIONS_BLUR_HLSL "xleres/TechniqueLibrary/LightingEngine/ssr-reflections-blur.compute.hlsl"
#define SSR_PIPELINE "xleres/TechniqueLibrary/LightingEngine/ssr.pipeline"

#define HIERARCHICAL_DEPTHS_HLSL "xleres/TechniqueLibrary/LightingEngine/hierarchical-depths.compute.hlsl"

#define TILED_LIGHTING_PREPARE_HLSL "xleres/TechniqueLibrary/LightingEngine/tiledlighting-prepare.pixel.hlsl"
#define TILED_LIGHTING_PREPARE_PIPELINE "xleres/TechniqueLibrary/LightingEngine/tiledlighting-prepare.pipeline"

#define SKY_PIXEL_HLSL "xleres/TechniqueLibrary/SceneEngine/Effects/Sky.pixel.hlsl"

#define VISUALIZE_ATTACHMENT_PIXEL_HLSL "xleres/ToolsHelper/visualize-attachment.pixel.hlsl"

#define IBL_PREFILTER_HLSL "xleres/ToolsHelper/IBLPrefilter.hlsl"
#define EQUIRECTANGULAR_TO_CUBE_HLSL "xleres/ToolsHelper/EquirectangularToCube.hlsl"
#define TOOLSHELPER_OPERATORS_PIPELINE "xleres/ToolsHelper/operators.pipeline"

#define SKIN_COMPUTE_HLSL "xleres/Deform/skin.compute.hlsl"
#define SKIN_PIPELINE "xleres/Deform/skin.pipeline"
#define DEFORM_ENTRY_HLSL "xleres/Deform/deform-entry.compute.hlsl"
