// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#if !defined(PREFIX_H)
#define PREFIX_H

	//
	//		Basic functions required for all shaders
	//		This file will be included as the first include
	//		in all shaders!
	//

float 	DefaultValue_float()	{ return 0.f; }
float2 	DefaultValue_float2() 	{ return 0.0.xx; }
float3 	DefaultValue_float3() 	{ return 0.0.xxx; }
float4 	DefaultValue_float4() 	{ return float4(0.0.xxx, 1.0f); }

uint 	DefaultValue_uint()		{ return 0; }
uint2 	DefaultValue_uint2() 	{ return uint2(0,0); }
uint3 	DefaultValue_uint3() 	{ return uint3(0,0,0); }
uint4 	DefaultValue_uint4() 	{ return uint4(0,0,0,1); }

int 	DefaultValue_int()		{ return 0; }
int2 	DefaultValue_int2() 	{ return int2(0,0); }
int3 	DefaultValue_int3() 	{ return int3(0,0,0); }
int4 	DefaultValue_int4() 	{ return int4(0,0,0,1); }

float4 	Cast_float_to_float4(float input)		{ return float4(input.x, 0, 0, 1); }
float4 	Cast_float2_to_float4(float2 input)		{ return float4(input.xy, 0, 1); }
float4 	Cast_float3_to_float4(float3 input)		{ return float4(input.xyz, 1); }

float3 	Cast_float_to_float3(float input)		{ return float3(input.x, 0, 0); }
float3 	Cast_float2_to_float3(float2 input)		{ return float3(input.xy, 0); }
float3 	Cast_float4_to_float3(float4 input)		{ return float3(input.xyz); }

float2 	Cast_float_to_float2(float input)		{ return float2(input.x, 0); }
float2 	Cast_float3_to_float2(float3 input)		{ return float2(input.xy); }
float2 	Cast_float4_to_float2(float4 input)		{ return float2(input.xy); }

float 	Cast_float2_to_float(float2 input)		{ return input.x; }
float 	Cast_float3_to_float(float3 input)		{ return input.x; }
float 	Cast_float4_to_float(float4 input)		{ return input.x; }

uint 	Cast_float_to_uint(float input)		    { return uint(input.x); }
uint 	Cast_float2_to_uint(float2 input)		{ return uint(input.x); }
uint 	Cast_float3_to_uint(float3 input)		{ return uint(input.x); }
uint 	Cast_float4_to_uint(float4 input)		{ return uint(input.x); }

uint2 	Cast_float_to_uint2(float input)		{ return uint2(input.x, 0); }
uint2 	Cast_float2_to_uint2(float2 input)		{ return uint2(input.xy); }
uint2 	Cast_float3_to_uint2(float3 input)		{ return uint2(input.xy); }
uint2 	Cast_float4_to_uint2(float4 input)		{ return uint2(input.xy); }

uint3 	Cast_float_to_uint3(float input)		{ return uint3(input.x, 0, 0); }
uint3 	Cast_float2_to_uint3(float2 input)		{ return uint3(input.xy, 0); }
uint3 	Cast_float3_to_uint3(float3 input)		{ return uint3(input.xyz); }
uint3 	Cast_float4_to_uint3(float4 input)		{ return uint3(input.xyz); }

uint4 	Cast_float_to_uint4(float input)		{ return uint4(input.x, 0, 0, 1); }
uint4 	Cast_float2_to_uint4(float2 input)		{ return uint4(input.xy, 0, 1); }
uint4 	Cast_float3_to_uint4(float3 input)		{ return uint4(input.xyz, 1); }
uint4 	Cast_float4_to_uint4(float4 input)		{ return uint4(input.xyzw); }

float4 AsFloat4(float input)	{ return Cast_float_to_float4(input); }
float4 AsFloat4(float2 input)   { return Cast_float2_to_float4(input); }
float4 AsFloat4(float3 input)   { return Cast_float3_to_float4(input); }
float4 AsFloat4(float4 input)   { return input; }

float4 AsFloat4(int input)	  { return Cast_float_to_float4(float(input)); }
float4 AsFloat4(int2 input)	 { return Cast_float2_to_float4(float2(input)); }
float4 AsFloat4(int3 input)	 { return Cast_float3_to_float4(float3(input)); }
float4 AsFloat4(int4 input)	 { return float4(input); }

float4 AsFloat4(uint input)	 { return Cast_float_to_float4(float(input)); }
float4 AsFloat4(uint2 input)	{ return Cast_float2_to_float4(float2(input)); }
float4 AsFloat4(uint3 input)	{ return Cast_float3_to_float4(float3(input)); }
float4 AsFloat4(uint4 input)	{ return float4(input); }

#endif
