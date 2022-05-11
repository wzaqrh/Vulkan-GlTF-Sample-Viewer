#version 450

layout (location = 0) in vec3 inPos;

layout (set = 0, binding = 0) uniform UBO 
{
	mat4 projection;
	mat4 view;
	mat4 model;
} ubo;

layout (location = 0) out vec3 outUVW;

void main() 
{
	outUVW = inPos;
	// mat4(mat3()) cancels out the translation part of the view matrix
	gl_Position = ubo.projection * mat4(mat3(ubo.view)) * vec4(inPos.xyz, 1.0);
}
