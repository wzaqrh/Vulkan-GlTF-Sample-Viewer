// Copyright 2020 Google LLC

struct VSInput
{
[[vk::location(0)]] float2 Pos : POSITION0;
[[vk::location(1)]] float4 GradientPos : POSITION1;
};

struct UBO
{
	float deltaT;
	float destX;
	float destY;
	int particleCount;
    float2 screenDim;
};

cbuffer ubo : register(b2) { UBO ubo; }

struct VSOutput
{
  float4 Pos : SV_POSITION;
[[vk::builtin("PointSize")]] float PSize : PSIZE;
[[vk::location(0)]] float4 Color : COLOR0;
[[vk::location(1)]] float GradientPos : POSITION0;
[[vk::location(2)]] float2 CenterPos : POSITION1;
[[vk::location(3)]] float PointSize : TEXCOORD0;
};

VSOutput main (VSInput input)
{
  VSOutput output = (VSOutput)0;
  output.PSize = output.PointSize = 16.0;
  output.Color = float4(0.035, 0.035, 0.035, 0.035);
  output.GradientPos = input.GradientPos.x;
  output.Pos = float4(input.Pos.xy, 1.0, 1.0);
  output.CenterPos = ((output.Pos.xy / output.Pos.w) + 1.0) * 0.5 * ubo.screenDim;
  return output;
}