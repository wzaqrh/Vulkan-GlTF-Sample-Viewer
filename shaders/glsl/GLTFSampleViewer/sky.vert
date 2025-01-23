#version 450

layout (location = 0) in vec3 inPos;
layout (location = 1) in vec3 inNormal;
layout (location = 2) in vec4 inColor;
layout (location = 3) in vec2 inUV0;
layout (location = 4) in vec2 inUV1;
layout (location = 5) in vec4 inTangent;

layout(push_constant) uniform PushConsts {
	mat4 u_ModelMatrix;
} pushConstants;

#define CAMERA_SET 1
#define CAMERA_BINDING 0
layout(std140, set = CAMERA_SET, binding = CAMERA_BINDING) uniform CameraUniforms 
{
    mat4 u_ViewMatrix;
    mat4 u_ProjectionMatrix;
    vec3 u_Camera;
    float u_Exposure;
};

#define ENVIROMENT_SET 0
#define ENVIROMENT_BINDING 0
layout(std140, set = ENVIROMENT_SET, binding = ENVIROMENT_BINDING) uniform EnviromentUniforms 
{
    ivec2 u_TransmissionFramebufferSize;
    int u_MipCount;
    float u_EnvIntensity;
    
	float u_EnvBlurNormalized;
	float u_env_padding_1;
	float u_env_padding_2;
	float u_env_padding_3;

	mat3 u_EnvRotation;
};

layout (location = 0) out vec3 outUVW;

void main()
{
	outUVW = u_EnvRotation * inPos;

	mat4 mat = u_ProjectionMatrix * u_ViewMatrix;
	mat[3] = vec4(0,0,0,1);
	vec4 pos = mat * vec4(inPos, 1.0);
	gl_Position = pos.xyww;
}