#version 450
#define HAS_COLOR_0_VEC4
#define HAS_NORMAL_VEC3
#define HAS_TANGENT_VEC4

layout(constant_id = 4) const int USE_SKELETON = 0;

layout (location = 0) in vec4 inPos;
#ifdef HAS_NORMAL_VEC3
layout (location = 1) in vec3 inNormal;
#endif
#ifdef HAS_COLOR_0_VEC4
layout (location = 2) in vec4 inColor;
#endif
layout (location = 3) in vec2 inUV0;
layout (location = 4) in vec2 inUV1;
#ifdef HAS_TANGENT_VEC4
layout (location = 5) in vec4 inTangent;
#endif
layout (location = 6) in uvec4 inJointIndices;
layout (location = 7) in vec4 inJointWeights;

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

#define MODEL_SET 4
#define SKELETON_BINDING 0
layout(std140, set = MODEL_SET, binding = SKELETON_BINDING) readonly buffer JointMatrices 
{
	mat4 jointMatrices[];
};

layout (location = 0) out vec3 outWorldPos;
layout (location = 1) out vec2 outUV0;
layout (location = 2) out vec2 outUV1;
#ifdef HAS_COLOR_0_VEC4
layout (location = 3) out vec4 outColor;
#endif
#ifdef HAS_NORMAL_VEC3
#ifdef HAS_TANGENT_VEC4
layout (location = 4) out mat3 outTBN;
#else
layout (location = 4) out vec3 outNormal;
#endif
#endif

void main() 
{
#ifdef HAS_COLOR_0_VEC4
	outColor = inColor;
#endif
	outUV0 = inUV0;
	outUV1 = inUV1;
	
	mat4 modelMatrix;
	if (USE_SKELETON != 0)
	{
		mat4 skinMat = 
			inJointWeights.x * jointMatrices[int(inJointIndices.x)] +
			inJointWeights.y * jointMatrices[int(inJointIndices.y)] +
			inJointWeights.z * jointMatrices[int(inJointIndices.z)] +
			inJointWeights.w * jointMatrices[int(inJointIndices.w)];		
		modelMatrix = pushConstants.u_ModelMatrix * skinMat;
	}
	else 
	{
		modelMatrix = pushConstants.u_ModelMatrix;
	}

#ifdef HAS_NORMAL_VEC3
#ifdef HAS_TANGENT_VEC4
	mat3 m3 = mat3(modelMatrix);
	vec3 normal = m3 * inNormal;
	vec3 tangent = m3 * inTangent.xyz;
	vec3 bitangent = cross(normal, tangent) * inTangent.w;
	outTBN = mat3(tangent, bitangent, normal);
#else
	outNormal = mat3(modelMatrix) * inNormal;
#endif
#endif

	vec4 worldPos = modelMatrix * inPos;
	outWorldPos = worldPos.xyz;
	gl_Position = u_ProjectionMatrix * u_ViewMatrix * worldPos;
}