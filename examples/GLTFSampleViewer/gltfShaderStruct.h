#pragma once
#define TINYGLTF_NO_STB_IMAGE_WRITE
#ifdef VK_USE_PLATFORM_ANDROID_KHR
#define TINYGLTF_ANDROID_LOAD_FROM_ASSETS
#endif
#include "tiny_gltf.h"
#include "vulkanexamplebase.h"


using mat4 = glm::mat4;
using mat3 = glm::mat3x4;
using vec4 = glm::vec4;
using vec3 = glm::vec3;
using vec2 = glm::vec2;
using ivec2 = glm::ivec2;
using uchar4 = glm::vec<4, unsigned char>;
static_assert(sizeof(uchar4) == 4, "");

#define SAFE_ASSERT(x) { auto res = x; assert(res); }

// vertex shader
#pragma pack(push, 16)
struct PushConsts {
	mat4 u_ModelMatrix;
};
static_assert(sizeof(PushConsts) == 16 * 4, "");
#pragma pack(pop)

// fragment shader
#pragma pack(push, 16)
#define TONEMAP_KHR_PBR_NEUTRAL 0
#define TONEMAP_ACES_HILL_EXPOSURE_BOOST 1
#define TONEMAP_ACES_NARKOWICZ 2
#define TONEMAP_ACES_HILL 3
#define TONEMAP_LINEAR 4

#define DEBUG_NONE 0
#define DEBUG_DIFFUSE 1
#define DEBUG_UV_0 10
#define DEBUG_UV_1 11

#define DEBUG_NORMAL_TEXTURE 20
#define DEBUG_GEOMETRY_NORMAL 21
#define DEBUG_GEOMETRY_TANGENT 22
#define DEBUG_GEOMETRY_BITANGENT 23
#define DEBUG_SHADING_NORMAL 24

#define DEBUG_ALPHA 30
#define DEBUG_OCCLUSION 31
#define DEBUG_EMISSIVE 32

#define DEBUG_METALLIC 40
#define DEBUG_ROUGHNESS 41
#define DEBUG_BASE_COLOR 42

#define DEBUG_CLEARCOAT_FACTOR 50
#define DEBUG_CLEARCOAT_ROUGHNESS 51
#define DEBUG_CLEARCOAT_NORMAL 52

#define DEBUG_SHEEN_COLOR 60
#define DEBUG_SHEEN_ROUGHNESS 61

#define DEBUG_TRANSMISSION_FACTOR 70
#define DEBUG_VOLUME_THICKNESS 71

#define DEBUG_IRIDESCENCE_FACTOR 80
#define DEBUG_IRIDESCENCE_THICKNESS 81

#define DEBUG_ANISOTROPIC_STRENGTH 90
#define DEBUG_ANISOTROPIC_DIRECTION 91

#define DEBUG_DIFFUSE_TRANSMISSION_FACTOR 100
#define DEBUG_DIFFUSE_TRANSMISSION_COLOR_FACTOR 101

#define DEBUG_IBL_DIFFUSE 110
#define DEBUG_IBL_SPECULAR_TRANSMISSION 111
#define DEBUG_IBL_SPECULAR_METAL 112
#define DEBUG_IBL_SPECULAR_DIELECTRIC 113
#define DEBUG_IBL_BRDF_METAL 114
#define DEBUG_IBL_BRDF_DIELECTRIC 115
#define DEBUG_IBL_BRDF_CLEARCOAT 116
#define DEBUG_IBL_SHEEN 117
#define DEBUG_IBL_SHEEN_LIGHT 118
#define DEBUG_IBL_SHEEN_BRDF_POINT 119
#define DEBUG_IBL_SHEEN_BRDF 120

#define DEBUG_VECTOR_V 130
#define DEBUG_VECTOR_L 131

#define FIRST_SET 0
#define ENVIROMENT_SET 0
#define CAMERA_SET 1
#define LIGHT_SET 2
#define MATERIAL_SET 3
#define MODEL_SET 4
#define LAST_SET 4
#define SET_COUNT (LAST_SET - FIRST_SET + 1)

#define MATERIAL_BINDING 0
#define MATERIAL_TEXTURE_FIRST_BINDING 8
#define MTL_TEX_BASE_COLOR_BINDING 8
#define MTL_TEX_NORMAL_BINDING 9
#define MTL_TEX_METALLIC_ROUGHNESS_BINDING 10
#define MTL_TEX_SHEEN_COLOR_BINDING 11
#define MTL_TEX_SHEEN_ROUGHNESS_BINDING 12
#define MTL_TEX_CLEARCOAT_BINDING 13
#define MTL_TEX_CLEARCOAT_ROUGHNESS_BINDING 14
#define MTL_TEX_CLEARCOAT_NORMAL_BINDING 15
#define MTL_TEX_OCCLUSION_BINDING 16
#define MTL_TEX_EMISSIVE_BINDING 17
#define MTL_TEX_TRANSMISSION_BINDING 18
#define MTL_TEX_THICKNESS_BINDING 19
#define MTL_TEX_IRIDESCENE_BINDING 20
#define MTL_TEX_IRIDESCENE_THICKNESS_BINDING 21
#define MTL_TEX_DIFFUSE_TRANSIMISSION_BINDING 22
#define MTL_TEX_DIFFUSE_TRANSIMISSION_COLOR_BINDING 23
#define MTL_TEX_ANISOTROPY_BINDING 24
#define MATERIAL_TEXTURE_LAST_BINDING 24
#define MATERIAL_TEXTURE_COUNT (MATERIAL_TEXTURE_LAST_BINDING - MATERIAL_TEXTURE_FIRST_BINDING + 1)

#define SKELETON_BINDING 0
#define CAMERA_BINDING 0
#define LIGHT_BINDING 0

#define ENVIROMENT_BINDING 0
#define ENVIROMENT_TEXTURE_FIRST_BINDING 8
#define ENV_TEX_GGX_ENV_BIDING 8
#define ENV_TEX_GGX_LUT_BIDING 9
#define ENV_TEX_LAMBERT_ENV_BIDING 10
#define ENV_TEX_CHARLIE_ENV_BIDING 11
#define ENV_TEX_CHARLIE_LUT_BIDING 12
#define ENV_TEX_SHEEN_ELUT_BIDING 13
#define ENV_TEX_TRANSMISSION_FRAMEBUFFER_BIDING 14
#define ENVIROMENT_TEXTURE_LAST_BINDING 14
#define ENVIROMENT_TEXTURE_COUNT (ENVIROMENT_TEXTURE_LAST_BINDING - ENVIROMENT_TEXTURE_FIRST_BINDING + 1)

#define HAS_BASE_COLOR_BIT 						(1<<0)
#define HAS_NORMAL_BIT 							(1<<1)
#define HAS_METALLIC_ROUGHNESS_BIT				(1<<2)
#define HAS_SHEEN_COLOR_BIT				 		(1<<3)
#define HAS_SHEEN_ROUGHNESS_BIT 				(1<<4)
#define HAS_CLEARCOAT_BIT                      	(1<<5)
#define HAS_CLEARCOAT_ROUGHNESS_BIT            	(1<<6)
#define HAS_CLEARCOAT_NORMAL_BIT               	(1<<7)
#define HAS_OCCLUSION_BIT                      	(1<<8)
#define HAS_EMISSIVE_BIT                       	(1<<9)
#define HAS_TRANSMISSION_BIT                   	(1<<10)
#define HAS_THICKNESS_BIT                      	(1<<11)
#define HAS_IRIDESCENCE_BIT                    	(1<<12)
#define HAS_IRIDESCENCE_THICKNESS_BIT          	(1<<13)
#define HAS_DIFFUSE_TRANSMISSION_BIT           	(1<<14)
#define HAS_DIFFUSE_TRANSMISSION_COLOR_BIT     	(1<<15)
#define HAS_ANISOTROPY_BIT                     	(1<<16)

#define MATERIAL_SHEEN_BIT                     	(1<<17)
#define MATERIAL_CLEARCOAT_BIT                 	(1<<18)
#define MATERIAL_TRANSMISSION_BIT              	(1<<19)
#define MATERIAL_VOLUME_BIT                    	(1<<20)
#define MATERIAL_IRIDESCENCE_BIT               	(1<<21)
#define MATERIAL_DIFFUSE_TRANSMISSION_BIT      	(1<<22)
#define MATERIAL_ANISOTROPY_BIT                	(1<<23)
#define MATERIAL_DISPERSION_BIT                	(1<<24)
#define MATERIAL_EMISSIVE_STRENGTH_BIT         	(1<<25)
#define MATERIAL_IOR_BIT						(1<<26)

#define ALPHAMODE_OPAQUE 0
#define ALPHAMODE_MASK 1
#define ALPHAMODE_BLEND 2

#define LightType_Directional 0
#define LightType_Point 1
#define LightType_Spot 2

struct PbrMaterialUniforms
{
	int u_BaseColorUVSet;
	int u_NormalUVSet;
	int u_MetallicRoughnessUVSet;
	int u_SheenColorUVSet;
	int u_SheenRoughnessUVSet;
	int u_ClearcoatUVSet;
	int u_ClearcoatRoughnessUVSet;
	int u_ClearcoatNormalUVSet;
	int u_OcclusionUVSet;
	int u_EmissiveUVSet;
	int u_TransmissionUVSet;
	int u_ThicknessUVSet;
	int u_IridescenceUVSet;
	int u_IridescenceThicknessUVSet;
	int u_DiffuseTransmissionUVSet;
	int u_DiffuseTransmissionColorUVSet;
	int u_AnisotropyUVSet;
	float u_Ior;
	float u_AlphaCutoff;
	float u_NormalScale;

	mat3 u_BaseColorUVTransform;
	mat3 u_NormalUVTransform;
	mat3 u_MetallicRoughnessUVTransform;
	mat3 u_SheenColorUVTransform;
	mat3 u_SheenRoughnessUVTransform;
	mat3 u_ClearcoatUVTransform;
	mat3 u_ClearcoatRoughnessUVTransform;
	mat3 u_ClearcoatNormalUVTransform;
	mat3 u_OcclusionUVTransform;
	mat3 u_EmissiveUVTransform;
	mat3 u_TransmissionUVTransform;
	mat3 u_ThicknessUVTransform;
	mat3 u_IridescenceUVTransform;
	mat3 u_IridescenceThicknessUVTransform;
	mat3 u_DiffuseTransmissionUVTransform;
	mat3 u_DiffuseTransmissionColorUVTransform;
	mat3 u_AnisotropyUVTransform;

	vec4 u_BaseColorFactor;

	vec3 u_Anisotropy;
	float u_Dispersion;

	vec3 u_DiffuseTransmissionColorFactor;
	float u_DiffuseTransmissionFactor;

	float u_IridescenceIor;
	float u_IridescenceThicknessMinimum;
	float u_IridescenceThicknessMaximum;
	float u_IridescenceFactor;

	vec3 u_AttenuationColor;
	float u_AttenuationDistance;

	float u_ThicknessFactor;
	float u_TransmissionFactor;
	float u_RoughnessFactor;
	float u_MetallicFactor;

	vec3 u_EmissiveFactor;
	float u_EmissiveStrength;

	float u_OcclusionStrength;
	float u_ClearcoatNormalScale;
	float u_ClearcoatRoughnessFactor;
	float u_ClearcoatFactor;

	vec3 u_SheenColorFactor;
	float u_SheenRoughnessFactor;

	int	u_AlphaMode;
	int u_mtl_padding_0;
	int u_mtl_padding_1;
	int u_mtl_padding_2;

	mat3& getSamplerUVTransform(int bindingIdx) { return (&u_BaseColorUVTransform)[bindingIdx]; }
	int& getSamplerUVSet(int bindingIdx) { return (&u_BaseColorUVSet)[bindingIdx]; }
	int getSamplerUVSet(int bindingIdx) const { return (&u_BaseColorUVSet)[bindingIdx]; }
	bool isFeatureEnabled(int binding) const { return (&u_BaseColorUVSet)[binding - MATERIAL_TEXTURE_FIRST_BINDING] >= -1; }
};
static_assert(sizeof(PbrMaterialUniforms) == 16 * 66, "");

struct CameraUniforms 
{
	mat4 u_ViewMatrix;
	mat4 u_ProjectionMatrix;
	vec3 u_Camera;
	float u_Exposure;
};
static_assert(sizeof(CameraUniforms) == 16 * 9, "");

struct Light
{
	vec3 direction;
	float range;

	vec3 color;
	float intensity;

	vec3 position;
	float innerConeCos;

	float outerConeCos;
	int type;
	int padding_0;
	int padding_1;
};
#if !defined LIGHT_COUNT
#define LIGHT_COUNT 4
#endif
struct LightUniforms 
{
	Light u_Lights[LIGHT_COUNT];
	int u_LightCount;
	int u_lgt_padding_0;
	int u_lgt_padding_1;
	int u_lgt_padding_2;
};
static_assert(sizeof(Light) == 16 * 4, "");
static_assert(sizeof(LightUniforms) % 16 == 0, "");

struct EnviromentUniforms 
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
static_assert(sizeof(EnviromentUniforms) % 16 == 0, "");
#pragma pack(pop)