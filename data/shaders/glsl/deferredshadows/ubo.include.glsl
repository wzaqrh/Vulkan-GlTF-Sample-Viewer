// Copyright 2021 Sascha Willems

#define LIGHT_COUNT 3
#define SHADOW_FACTOR 0.25
#define AMBIENT_LIGHT 0.1
#define USE_PCF

struct Light 
{
	vec4 position;
	vec4 target;
	vec4 color;
	mat4 viewMatrix;
};

layout (set = 0, binding = 0) uniform UBO 
{
	mat4 projection;
	mat4 model;
	mat4 view;
	vec4 instancePos[3];
	vec4 viewPos;
	Light lights[LIGHT_COUNT];
	int useShadows;
	int debugDisplayTarget;
} ubo;