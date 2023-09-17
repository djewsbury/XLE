
cbuffer ExtendedTransforms
{
	row_major float4x4 ClipToView;      // g_inv_proj
	row_major float4x4 ClipToWorld;     // g_inv_view_proj
	row_major float4x4 WorldToView;     // g_view
	row_major float4x4 ViewToWorld;     // g_inv_view
	row_major float4x4 ViewToProj;      // g_proj
	row_major float4x4 PrevWorldToClip; // g_prev_view_proj
	float2 SSRNegativeReciprocalScreenSize;
};

cbuffer FrameIdBuffer
{
	uint FrameId;
};

cbuffer SSRConfiguration
{
	uint g_most_detailed_mip;
	uint g_min_traversal_occupancy;
	uint g_max_traversal_intersections;

	float g_depth_buffer_thickness;

	uint g_temporal_variance_guided_tracing_enabled;
	uint g_samples_per_quad;

	float g_temporal_stability_factor;
	float g_temporal_variance_threshold;

	float g_depth_sigma;
	float g_roughness_sigma_min;
	float g_roughness_sigma_max;
}

