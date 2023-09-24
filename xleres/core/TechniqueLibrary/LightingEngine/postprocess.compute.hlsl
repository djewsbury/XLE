

RWTexture2D<float4> Output;
Texture2D<float4> Input;

[numthreads(8, 8, 1)]
	void main(uint3 groupThreadId : SV_GroupThreadID, uint3 groupId : SV_GroupID, uint3 dispatchThreadId : SV_DispatchThreadID)
{
	Output[dispatchThreadId.xy] = Input[dispatchThreadId.xy];
}
