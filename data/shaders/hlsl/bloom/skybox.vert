// Copyright 2020 Google LLC

struct UBO
{
	float4x4 projection;
	float4x4 view;
	float4x4 model;
};

cbuffer ubo : register(b0, space0) { UBO ubo; }

struct VSOutput
{
	float4 Pos : SV_POSITION;
	[[vk::location(0)]] float3 UVW : NORMAL0;
};

VSOutput main([[vk::location(0)]] float3 inPos : POSITION0)
{
	VSOutput output = (VSOutput)0;
	output.UVW = inPos;
	// Cancel out the translation part of the modelview matrix, as the skybox needs to stay centered
	float4x4 viewCentered = ubo.view;
	viewCentered[0][3] = 0.0;
	viewCentered[1][3] = 0.0;
	viewCentered[2][3] = 0.0;
	viewCentered[3][3] = 1.0;
	output.Pos = mul(ubo.projection, mul(viewCentered, float4(inPos.xyz, 1.0)));
	return output;
}
