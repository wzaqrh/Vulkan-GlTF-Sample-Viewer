// Copyright 2020 Google LLC
// Copyright 2021 Sascha Willems

#include "ubo.include.hlsl"

#define LIGHT_COUNT 3

struct VSOutput
{
	float4 Pos : SV_POSITION;
[[vk::location(0)]] int InstanceIndex : TEXCOORD0;
};

struct GSOutput
{
	float4 Pos : SV_POSITION;
	int Layer : SV_RenderTargetArrayIndex;
};

[maxvertexcount(3)]
[instance(3)]
void main(triangle VSOutput input[3], uint InvocationID : SV_GSInstanceID, inout TriangleStream<GSOutput> outStream)
{
	float4 instancedPos = ubo.instancePos[input[0].InstanceIndex];
	for (int i = 0; i < 3; i++)
	{
		float4 tmpPos = input[i].Pos + instancedPos;
		GSOutput output = (GSOutput)0;
		output.Pos = mul(ubo.lights[InvocationID].viewMatrix, tmpPos);
		output.Layer = InvocationID;
		outStream.Append( output );
	}
	outStream.RestartStrip();
}