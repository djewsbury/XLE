~ps_main; item=<:(
NE_{{GraphName}}_Output ps_main(NE_PSInput input, SystemInputs sys)
{
	NE_{{GraphName}}_Output functionResult;
	{{GraphName}}({{ParametersToMainFunctionCall}});
	return functionResult;
}
):>

~ps_main_explicit; item=<:(
float4 ps_main(NE_PSInput input, SystemInputs sys) : SV_Target0
{
	NE_{{GraphName}}_Output functionResult;
	{{GraphName}}({{ParametersToMainFunctionCall}});
	return AsFloat4(functionResult.{{PreviewOutput}});
}
):>

~ps_main_chart; item=<:(

//////////////////////////////////////////////////////////////////
		// Graphs //

float4 BackgroundPattern(float4 position)
{
	int2 c = int2(position.xy / 16.f);
	float4 colours[] = { float4(0.125f, 0.125f, 0.125f, 1.f), float4(0.025f, 0.025f, 0.025f, 1.f) };
	return colours[(c.x+c.y)%2];
}

float4 FilledGraphPattern(float4 position)
{
	int2 c = int2(position.xy / 8.f);
	float4 colours[] = { float4(0.35f, 0.35f, 0.35f, 1.f), float4(0.65f, 0.65f, 0.65f, 1.f) };
	return colours[(c.x+c.y)%2];
}

float NodeEditor_GraphEdgeFactor(float value)
{
	value = abs(value);
    float d = fwidth(value);
    return 1.f - smoothstep(0.f, 2.f * d, value);
}

float NodeEditor_IsGraphEdge(float functionResult, float comparisonValue)
{
	float distance = functionResult - comparisonValue;
	return NodeEditor_GraphEdgeFactor(distance);
}

float4 NodeEditor_GraphEdgeColour(int index)
{
	float4 colours[] = { float4(1, 0, 0, 1), float4(0, 1, 0, 1), float4(0, 0, 1, 1), float4(0, 1, 1, 1) };
	return colours[min(3, index)];
}

//////////////////////////////////////////////////////////////////

float4 ps_main(NE_PSInput input, float2 chartCoords : CHARTCOORDS, float4 position : SV_Position, SystemInputs sys) : SV_Target0
{
	NE_{{GraphName}}_Output functionResult;
	{{GraphName}}({{ParametersToMainFunctionCall}});

	const uint chartLineCount = {{ChartLineCount}};
	const float chartLines[] =
	{
{{#ChartLines}}
		functionResult.{{Item}},
{{/ChartLines}}
	};

	float chartY = chartCoords.y; // 1.f - position.y / NodeEditor_GetOutputDimensions().y;

	uint filled = 0;
	for (uint c=0; c<chartLineCount; c++)
		if (chartY < chartLines[c])
			++filled;

	float3 chartOutput = lerp(BackgroundPattern(position), FilledGraphPattern(position), filled/float(chartLineCount));
	for (uint c2=0; c2<chartLineCount; c2++)
		chartOutput = lerp(chartOutput, NodeEditor_GraphEdgeColour(c2).rgb, NodeEditor_IsGraphEdge(chartLines[c2], chartY));

	return float4(chartOutput, 1.f);
}
):>

~vs_main; item=<:(
void vs_main(uint vertexId : SV_VertexID, VSIN vsInput, out NE_PSInput OUT, out float2 chartCoords : CHARTCOORDS)
{
	{{#InitGeo}}OUT.geo = BuildInterpolator_VSOutput(vsInput);{{/InitGeo}}
	float3 worldPosition = BuildInterpolator_WORLDPOSITION(vsInput);
	float3 localPosition = VSIN_GetLocalPosition(vsInput);
{{VaryingInitialization}}
	chartCoords = .5.xx + 0.5f * localPosition.xy;
}
):>
