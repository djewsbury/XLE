
#define R32G32B32A32_FLOAT 2
#define R32G32B32_FLOAT 6
#define R16G16B16A16_FLOAT 10
#define R32G32_FLOAT 16
#define R32_FLOAT 41
#define R32_UINT 42
#define R16_UINT 57
#define R8G8B8A8_UNORM 28
#define R8G8B8A8_SNORM 31
#define R8G8B8_UNORM 1001
#define R8G8B8_SNORM 1004
#define R10G10B10A2_UNORM 24
#define R16G16B16A16_SNORM 13

float3 LoadAsFloat3(ByteAddressBuffer buffer, uint format, uint byteOffset);
float4 LoadAsFloat4(ByteAddressBuffer buffer, uint format, uint byteOffset);
float  LoadAsFloat1(RWByteAddressBuffer buffer, uint format, uint byteOffset);
float2 LoadAsFloat2(RWByteAddressBuffer buffer, uint format, uint byteOffset);
float3 LoadAsFloat3(RWByteAddressBuffer buffer, uint format, uint byteOffset);
float4 LoadAsFloat4(RWByteAddressBuffer buffer, uint format, uint byteOffset);
void StoreFloat1(float  value, RWByteAddressBuffer buffer, uint format, uint byteOffset);
void StoreFloat2(float2 value, RWByteAddressBuffer buffer, uint format, uint byteOffset);
void StoreFloat3(float3 value, RWByteAddressBuffer buffer, uint format, uint byteOffset);
void StoreFloat4(float4 value, RWByteAddressBuffer buffer, uint format, uint byteOffset);


// For the SNorm formats here, we're assuming that both -1 and +1 can be represented -- in effect there are 2 representations of -1
// UNorm is mapped onto -1 -> 1; since this just tends to be a more useful mapping than 0 -> 1
int SignExtend8(uint byteValue) { return byteValue + (byteValue >> 7) * 0xffffff00; }
int SignExtend16(uint shortValue) { return shortValue + (shortValue >> 15) * 0xffff0000; }

float UNorm8ToFloat(uint x) { return x * (2.0 / float(0xff)) - 1.0; }
uint FloatToUNorm8(float x) { return (uint)clamp((x + 1.0) * float(0xff) / 2.0, 0.0, float(0xff)); }

float SNorm8ToFloat(uint x) { return clamp(SignExtend8(x) / float(0x7f), -1.0, 1.0); }
uint FloatToSNorm8(float x) { return int(clamp(x, -1.0, 1.0) * float(0x7f)) & 0xff; }

float UNorm10ToFloat(uint x) { return x * (2.0 / float(0x3ff)) - 1.0; }
uint FloatToUNorm10(float x) { return (uint)clamp((x + 1.0) * float(0x3ff) / 2.0, 0.0, float(0x3ff)); }

float SNorm16ToFloat(uint x) { return clamp(SignExtend16(x) / float(0x7fff), -1.0, 1.0); }
uint FloatToSNorm16(float x) { return int(clamp(x, -1.0, 1.0) * float(0x7fff)) & 0xffff; }


float3 LoadAsFloat3(ByteAddressBuffer buffer, uint format, uint byteOffset)
{
	// requires that byteOffset be multiple of 4
	if (format == R32_FLOAT) {
		return float3(asfloat(buffer.Load(byteOffset)), 0, 0);
	} else if (format == R32G32_FLOAT) {
		return float3(asfloat(buffer.Load2(byteOffset)), 0);
	} else if (format == R32G32B32_FLOAT || format == R32G32B32A32_FLOAT) {
		return asfloat(buffer.Load3(byteOffset));
	} else if (format == R16G16B16A16_FLOAT) {
		uint2 A = buffer.Load2(byteOffset);
		return f16tof32(uint3(A.x&0xffff, A.x>>16, A.y&0xffff));
	} else if (format == R16G16B16A16_SNORM) {
		uint2 A = buffer.Load2(byteOffset);
		return float3(SNorm16ToFloat(A.x&0xffff), SNorm16ToFloat(A.x>>16), SNorm16ToFloat(A.y&0xffff));
	} else if (format == R8G8B8A8_UNORM || format == R8G8B8_UNORM) {
		uint A = buffer.Load(byteOffset);
		return float3(UNorm8ToFloat(A & 0xff), UNorm8ToFloat((A>>8) & 0xff), UNorm8ToFloat((A>>16) & 0xff));
	} else if (format == R8G8B8A8_SNORM || format == R8G8B8_SNORM) {
		uint A = buffer.Load(byteOffset);
		return float3(SNorm8ToFloat(A & 0xff), SNorm8ToFloat((A>>8) & 0xff), SNorm8ToFloat((A>>16) & 0xff));
	} else if (format == R10G10B10A2_UNORM) {
		uint A = buffer.Load(byteOffset);
		return float3(UNorm10ToFloat(A & 0x3ff), UNorm10ToFloat((A>>10) & 0x3ff), UNorm10ToFloat((A>>20) & 0x3ff));
	} else {
		return 0;	// trouble
	}
}

float4 LoadAsFloat4(ByteAddressBuffer buffer, uint format, uint byteOffset)
{
	// requires that byteOffset be multiple of 4
	if (format == R32_FLOAT) {
		return float4(asfloat(buffer.Load(byteOffset)), 0, 0, 1);
	} else if (format == R32G32_FLOAT) {
		return float4(asfloat(buffer.Load2(byteOffset)), 0, 1);
	} else if (format == R32G32B32_FLOAT) {
		return float4(asfloat(buffer.Load3(byteOffset)), 1);
	} else if (format == R32G32B32A32_FLOAT) {
		return asfloat(buffer.Load4(byteOffset));
	} else if (format == R16G16B16A16_FLOAT) {
		uint2 A = buffer.Load2(byteOffset);
		return f16tof32(uint4(A.x&0xffff, A.x>>16, A.y&0xffff, A.y>>16));
	} else if (format == R16G16B16A16_SNORM) {
		uint2 A = buffer.Load2(byteOffset);
		return float4(SNorm16ToFloat(A.x&0xffff), SNorm16ToFloat(A.x>>16), SNorm16ToFloat(A.y&0xffff), SNorm16ToFloat(A.y>>16));
	} else if (format == R8G8B8A8_UNORM || format == R8G8B8_UNORM) {
		uint A = buffer.Load(byteOffset);
		return float4(UNorm8ToFloat(A & 0xff), UNorm8ToFloat((A>>8) & 0xff), UNorm8ToFloat((A>>16) & 0xff), UNorm8ToFloat((A>>24) & 0xff));
	} else if (format == R8G8B8A8_SNORM || format == R8G8B8_SNORM) {
		uint A = buffer.Load(byteOffset);
		return float4(SNorm8ToFloat(A & 0xff), SNorm8ToFloat((A>>8) & 0xff), SNorm8ToFloat((A>>16) & 0xff), SNorm8ToFloat((A>>24) & 0xff));
	} else if (format == R10G10B10A2_UNORM) {
		uint A = buffer.Load(byteOffset);
		return float4(UNorm10ToFloat(A & 0x3ff), UNorm10ToFloat((A>>10) & 0x3ff), UNorm10ToFloat((A>>20) & 0x3ff), A>>30);
	} else {
		return 0;	// trouble
	}
}

float LoadAsFloat1(RWByteAddressBuffer buffer, uint format, uint byteOffset)
{
	// requires that byteOffset be multiple of 4
	if (format == R32_FLOAT || format == R32G32_FLOAT || format == R32G32B32_FLOAT || format == R32G32B32A32_FLOAT) {
		return asfloat(buffer.Load(byteOffset));
	} else if (format == R16G16B16A16_FLOAT) {
		uint A = buffer.Load(byteOffset);
		return f16tof32(uint(A.x&0xffff));
	} else if (format == R16G16B16A16_SNORM) {
		uint A = buffer.Load(byteOffset);
		return float(SNorm16ToFloat(A&0xffff));
	} else if (format == R8G8B8A8_UNORM || format == R8G8B8_UNORM) {
		uint A = buffer.Load(byteOffset);
		return float(UNorm8ToFloat(A & 0xff));
	} else if (format == R8G8B8A8_SNORM || format == R8G8B8_SNORM) {
		uint A = buffer.Load(byteOffset);
		return float(SNorm8ToFloat(A & 0xff));
	} else if (format == R10G10B10A2_UNORM) {
		uint A = buffer.Load(byteOffset);
		return float(UNorm10ToFloat(A & 0x3ff));
	} else {
		return 0;	// trouble
	}
}

float2 LoadAsFloat2(RWByteAddressBuffer buffer, uint format, uint byteOffset)
{
	// requires that byteOffset be multiple of 4
	if (format == R32_FLOAT) {
		return float2(asfloat(buffer.Load(byteOffset)), 0);
	} else if (format == R32G32_FLOAT || format == R32G32B32_FLOAT || format == R32G32B32A32_FLOAT) {
		return asfloat(buffer.Load2(byteOffset));
	} else if (format == R16G16B16A16_FLOAT) {
		uint A = buffer.Load(byteOffset);
		return f16tof32(uint2(A&0xffff, A>>16));
	} else if (format == R16G16B16A16_SNORM) {
		uint A = buffer.Load(byteOffset);
		return float2(SNorm16ToFloat(A&0xffff), SNorm16ToFloat(A>>16));
	} else if (format == R8G8B8A8_UNORM || format == R8G8B8_UNORM) {
		uint A = buffer.Load(byteOffset);
		return float2(UNorm8ToFloat(A & 0xff), UNorm8ToFloat((A>>8) & 0xff));
	} else if (format == R8G8B8A8_SNORM || format == R8G8B8_SNORM) {
		uint A = buffer.Load(byteOffset);
		return float2(SNorm8ToFloat(A & 0xff), SNorm8ToFloat((A>>8) & 0xff));
	} else if (format == R10G10B10A2_UNORM) {
		uint A = buffer.Load(byteOffset);
		return float2(UNorm10ToFloat(A & 0x3ff), UNorm10ToFloat((A>>10) & 0x3ff));
	} else {
		return 0;	// trouble
	}
}

float3 LoadAsFloat3(RWByteAddressBuffer buffer, uint format, uint byteOffset)
{
	// requires that byteOffset be multiple of 4
	if (format == R32_FLOAT) {
		return float3(asfloat(buffer.Load(byteOffset)), 0, 0);
	} else if (format == R32G32_FLOAT) {
		return float3(asfloat(buffer.Load2(byteOffset)), 0);
	} else if (format == R32G32B32_FLOAT || format == R32G32B32A32_FLOAT) {
		return asfloat(buffer.Load3(byteOffset));
	} else if (format == R16G16B16A16_FLOAT) {
		uint2 A = buffer.Load2(byteOffset);
		return f16tof32(uint3(A.x&0xffff, A.x>>16, A.y&0xffff));
	} else if (format == R16G16B16A16_SNORM) {
		uint2 A = buffer.Load2(byteOffset);
		return float3(SNorm16ToFloat(A.x&0xffff), SNorm16ToFloat(A.x>>16), SNorm16ToFloat(A.y&0xffff));
	} else if (format == R8G8B8A8_UNORM || format == R8G8B8_UNORM) {
		uint A = buffer.Load(byteOffset);
		return float3(UNorm8ToFloat(A & 0xff), UNorm8ToFloat((A>>8) & 0xff), UNorm8ToFloat((A>>16) & 0xff));
	} else if (format == R8G8B8A8_SNORM || format == R8G8B8_SNORM) {
		uint A = buffer.Load(byteOffset);
		return float3(SNorm8ToFloat(A & 0xff), SNorm8ToFloat((A>>8) & 0xff), SNorm8ToFloat((A>>16) & 0xff));
	} else if (format == R10G10B10A2_UNORM) {
		uint A = buffer.Load(byteOffset);
		return float3(UNorm10ToFloat(A & 0x3ff), UNorm10ToFloat((A>>10) & 0x3ff), UNorm10ToFloat((A>>20) & 0x3ff));
	} else {
		return 0;	// trouble
	}
}

float4 LoadAsFloat4(RWByteAddressBuffer buffer, uint format, uint byteOffset)
{
	// requires that byteOffset be multiple of 4
	if (format == R32_FLOAT) {
		return float4(asfloat(buffer.Load(byteOffset)), 0, 0, 1);
	} else if (format == R32G32_FLOAT) {
		return float4(asfloat(buffer.Load2(byteOffset)), 0, 1);
	} else if (format == R32G32B32_FLOAT) {
		return float4(asfloat(buffer.Load3(byteOffset)), 1);
	} else if (format == R32G32B32A32_FLOAT) {
		return asfloat(buffer.Load4(byteOffset));
	} else if (format == R16G16B16A16_FLOAT) {
		uint2 A = buffer.Load2(byteOffset);
		return f16tof32(uint4(A.x&0xffff, A.x>>16, A.y&0xffff, A.y>>16));
	} else if (format == R16G16B16A16_SNORM) {
		uint2 A = buffer.Load2(byteOffset);
		return float4(SNorm16ToFloat(A.x&0xffff), SNorm16ToFloat(A.x>>16), SNorm16ToFloat(A.y&0xffff), SNorm16ToFloat(A.y>>16));
	} else if (format == R8G8B8A8_UNORM || format == R8G8B8_UNORM) {
		uint A = buffer.Load(byteOffset);
		return float4(UNorm8ToFloat(A & 0xff), UNorm8ToFloat((A>>8) & 0xff), UNorm8ToFloat((A>>16) & 0xff), UNorm8ToFloat((A>>24) & 0xff));
	} else if (format == R8G8B8A8_SNORM || format == R8G8B8_SNORM) {
		uint A = buffer.Load(byteOffset);
		return float4(SNorm8ToFloat(A & 0xff), SNorm8ToFloat((A>>8) & 0xff), SNorm8ToFloat((A>>16) & 0xff), SNorm8ToFloat((A>>24) & 0xff));
	} else if (format == R10G10B10A2_UNORM) {
		uint A = buffer.Load(byteOffset);
		return float4(UNorm10ToFloat(A & 0x3ff), UNorm10ToFloat((A>>10) & 0x3ff), UNorm10ToFloat((A>>20) & 0x3ff), A>>30);
	} else {
		return 0;	// trouble
	}
}

uint LoadAsUInt(ByteAddressBuffer buffer, uint format, uint byteOffset)
{
	if (format == R32_UINT) {
		return buffer.Load(byteOffset);
	} else if (format == R16_UINT) {
		uint index = buffer.Load(byteOffset & ~3);
		if (byteOffset & 3) return index >> 16;
		else return index & 0xffff;
	} else {
		return 0;
	}
}

uint3 LoadAsUInt3(ByteAddressBuffer buffer, uint format, uint byteOffset)
{
	if (format == R32_UINT) {
		return buffer.Load3(byteOffset);
	} else if (format == R16_UINT) {
		uint2 raw = buffer.Load2(byteOffset & ~3);
		if (byteOffset & 3) return uint3(raw.x >> 16, raw.y & 0xffff, raw.y >> 16);
		else return uint3(raw.x & 0xffff, raw.x >> 16, raw.y & 0xffff);
	} else {
		return 0;
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////

void StoreFloat1(float value, RWByteAddressBuffer buffer, uint format, uint byteOffset)
{
	if (format == R32_FLOAT) {
		buffer.Store(byteOffset, asuint(value));
	} else if (format == R32G32_FLOAT) {
		buffer.Store2(byteOffset, asuint(float2(value, 0)));
	} else if (format == R32G32B32_FLOAT) {
		buffer.Store3(byteOffset, asuint(float3(value, 0, 0)));
	} else if (format == R32G32B32A32_FLOAT) {
		buffer.Store4(byteOffset, asuint(float4(value, 0, 0, 1)));
	} else if (format == R16G16B16A16_FLOAT) {
		uint4 A = f32tof16(float4(value, 0, 0, 1));
		buffer.Store2(byteOffset, uint2((A.x&0xffff)|(A.y<<16), (A.z&0xffff)|(A.w<<16)));
	} else if (format == R16G16B16A16_SNORM) {
		buffer.Store2(byteOffset, uint2(FloatToSNorm16(value)|(FloatToSNorm16(0)<<16), FloatToSNorm16(0)|(FloatToSNorm16(1)<<16)));
	} else if (format == R8G8B8A8_UNORM) {
		buffer.Store(byteOffset, FloatToUNorm8(value)|(FloatToUNorm8(0) << 8u)|(FloatToUNorm8(0) << 16u)|(FloatToUNorm8(1) << 24u));
	} else if (format == R8G8B8A8_SNORM) {
		buffer.Store(byteOffset, FloatToSNorm8(value)|(FloatToSNorm8(0) << 8u)|(FloatToSNorm8(0) << 16u)|(FloatToSNorm8(1) << 24u));
	} else if (format == R10G10B10A2_UNORM) {
		buffer.Store(byteOffset, FloatToUNorm10(value)|(FloatToUNorm10(0) << 10u)|(FloatToUNorm10(0) << 20u)|(uint(1) << 30u));
	} else {
		// trouble
	}
}

void StoreFloat2(float2 value, RWByteAddressBuffer buffer, uint format, uint byteOffset)
{
	if (format == R32_FLOAT) {
		buffer.Store(byteOffset, asuint(value.x));
	} else if (format == R32G32_FLOAT) {
		buffer.Store2(byteOffset, asuint(value));
	} else if (format == R32G32B32_FLOAT) {
		buffer.Store3(byteOffset, asuint(float3(value.xy, 0)));
	} else if (format == R32G32B32A32_FLOAT) {
		buffer.Store4(byteOffset, asuint(float4(value.xy, 0, 1)));
	} else if (format == R16G16B16A16_FLOAT) {
		uint4 A = f32tof16(float4(value.xy, 0, 1));
		buffer.Store2(byteOffset, uint2((A.x&0xffff)|(A.y<<16), (A.z&0xffff)|(A.w<<16)));
	} else if (format == R16G16B16A16_SNORM) {
		buffer.Store2(byteOffset, uint2(FloatToSNorm16(value.x)|(FloatToSNorm16(value.y)<<16), FloatToSNorm16(0)|(FloatToSNorm16(1)<<16)));
	} else if (format == R8G8B8A8_UNORM) {
		buffer.Store(byteOffset, FloatToUNorm8(value.x)|(FloatToUNorm8(value.y) << 8u)|(FloatToUNorm8(0) << 16u)|(FloatToUNorm8(1) << 24u));
	} else if (format == R8G8B8A8_SNORM) {
		buffer.Store(byteOffset, FloatToSNorm8(value.x)|(FloatToSNorm8(value.y) << 8u)|(FloatToSNorm8(0) << 16u)|(FloatToSNorm8(1) << 24u));
	} else if (format == R10G10B10A2_UNORM) {
		buffer.Store(byteOffset, FloatToUNorm10(value.x)|(FloatToUNorm10(value.y) << 10u)|(FloatToUNorm10(0) << 20u)|(uint(1) << 30u));
	} else {
		// trouble
	}
}

void StoreFloat3(float3 value, RWByteAddressBuffer buffer, uint format, uint byteOffset)
{
	if (format == R32_FLOAT) {
		buffer.Store(byteOffset, asuint(value.x));
	} else if (format == R32G32_FLOAT) {
		buffer.Store2(byteOffset, asuint(value.xy));
	} else if (format == R32G32B32_FLOAT) {
		buffer.Store3(byteOffset, asuint(value));
	} else if (format == R32G32B32A32_FLOAT) {
		buffer.Store4(byteOffset, asuint(float4(value, 1)));
	} else if (format == R16G16B16A16_FLOAT) {
		uint4 A = f32tof16(float4(value, 1));
		buffer.Store2(byteOffset, uint2((A.x&0xffff)|(A.y<<16), (A.z&0xffff)|(A.w<<16)));
	} else if (format == R16G16B16A16_SNORM) {
		buffer.Store2(byteOffset, uint2(FloatToSNorm16(value.x)|(FloatToSNorm16(value.y)<<16), FloatToSNorm16(value.z)|(FloatToSNorm16(1)<<16)));
	} else if (format == R8G8B8A8_UNORM) {
		buffer.Store(byteOffset, FloatToUNorm8(value.x)|(FloatToUNorm8(value.y) << 8u)|(FloatToUNorm8(value.z) << 16u)|(FloatToUNorm8(1) << 24u));
	} else if (format == R8G8B8A8_SNORM) {
		buffer.Store(byteOffset, FloatToSNorm8(value.x)|(FloatToSNorm8(value.y) << 8u)|(FloatToSNorm8(value.z) << 16u)|(FloatToSNorm8(1) << 24u));
	} else if (format == R10G10B10A2_UNORM) {
		buffer.Store(byteOffset, FloatToUNorm10(value.x)|(FloatToUNorm10(value.y) << 10u)|(FloatToUNorm10(value.z) << 20u)|(uint(1) << 30u));
	} else {
		// trouble
	}
}

void StoreFloat4(float4 value, RWByteAddressBuffer buffer, uint format, uint byteOffset)
{
	if (format == R32_FLOAT) {
		buffer.Store(byteOffset, asuint(value.x));
	} else if (format == R32G32_FLOAT) {
		buffer.Store2(byteOffset, asuint(value.xy));
	} else if (format == R32G32B32_FLOAT) {
		buffer.Store3(byteOffset, asuint(value.xyz));
	} else if (format == R32G32B32A32_FLOAT) {
		buffer.Store4(byteOffset, asuint(value));
	} else if (format == R16G16B16A16_FLOAT) {
		uint4 A = f32tof16(value);
		buffer.Store2(byteOffset, uint2((A.x&0xffff)|(A.y<<16), (A.z&0xffff)|(A.w<<16)));
	} else if (format == R16G16B16A16_SNORM) {
		buffer.Store2(byteOffset, uint2(FloatToSNorm16(value.x)|(FloatToSNorm16(value.y)<<16), FloatToSNorm16(value.z)|(FloatToSNorm16(value.w)<<16)));
	} else if (format == R8G8B8A8_UNORM) {
		buffer.Store(byteOffset, FloatToUNorm8(value.x)|(FloatToUNorm8(value.y) << 8u)|(FloatToUNorm8(value.z) << 16u)|(FloatToUNorm8(value.w) << 24u));
	} else if (format == R8G8B8A8_SNORM) {
		buffer.Store(byteOffset, FloatToSNorm8(value.x)|(FloatToSNorm8(value.y) << 8u)|(FloatToSNorm8(value.z) << 16u)|(FloatToSNorm8(value.w) << 24u));
	} else if (format == R10G10B10A2_UNORM) {
		buffer.Store(byteOffset, FloatToUNorm10(value.x)|(FloatToUNorm10(value.y) << 10u)|(FloatToUNorm10(value.z) << 20u)|(uint(value.w) << 30u));
	} else {
		// trouble
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////

