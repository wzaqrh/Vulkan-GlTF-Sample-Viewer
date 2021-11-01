// Copyright 2021 Sascha Willems

#version 450
#extension GL_GOOGLE_include_directive : require

#include "ubo.include.glsl"

layout (triangles, invocations = LIGHT_COUNT) in;
layout (triangle_strip, max_vertices = 3) out;

layout (location = 0) in int inInstanceIndex[];

void main() 
{
	vec4 instancedPos = ubo.instancePos[inInstanceIndex[0]]; 
	for (int i = 0; i < gl_in.length(); i++)
	{
		gl_Layer = gl_InvocationID;
		vec4 tmpPos = gl_in[i].gl_Position + instancedPos;
		gl_Position = ubo.lights[gl_InvocationID].viewMatrix * tmpPos;
		EmitVertex();
	}
	EndPrimitive();
}
