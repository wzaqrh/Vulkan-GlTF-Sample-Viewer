// Copyright 2021 Sascha Willems

#define LIGHT_COUNT 3
#define SHADOW_FACTOR 0.25
#define AMBIENT_LIGHT 0.1
#define USE_PCF

struct Light 
{
	float4 position;
	float4 target;
	float4 color;
	float4x4 viewMatrix;
};

struct UBO 
{
	float4x4 projection;
	float4x4 model;
	float4x4 view;
	float4 instancePos[3];
	float4 viewPos;
	Light lights[LIGHT_COUNT];
	int useShadows;
	int debugDisplayTarget;
};

cbuffer ubo : register(b0) { UBO ubo; }