#version 450

layout(constant_id = 0) const int TONEMAP = 1;
#define TONEMAP_KHR_PBR_NEUTRAL 0
#define TONEMAP_ACES_HILL_EXPOSURE_BOOST 1
#define TONEMAP_ACES_NARKOWICZ 2
#define TONEMAP_ACES_HILL 3
#define TONEMAP_LINEAR 4

const float GAMMA = 2.2;
const float INV_GAMMA = 1.0 / GAMMA;
// sRGB => XYZ => D65_2_D60 => AP1 => RRT_SAT
const mat3 ACESInputMat = mat3
(
    0.59719, 0.07600, 0.02840,
    0.35458, 0.90834, 0.13383,
    0.04823, 0.01566, 0.83777
);
// ODT_SAT => XYZ => D60_2_D65 => sRGB
const mat3 ACESOutputMat = mat3
(
    1.60475, -0.10208, -0.00327,
    -0.53108,  1.10813, -0.07276,
    -0.07367, -0.00605,  1.07602
);
// linear to sRGB approximation
// see http://chilliant.blogspot.com/2012/08/srgb-approximations-for-hlsl.html
vec3 linearTosRGB(vec3 color)
{
    return pow(color, vec3(INV_GAMMA));
}
// sRGB to linear approximation
// see http://chilliant.blogspot.com/2012/08/srgb-approximations-for-hlsl.html
vec3 sRGBToLinear(vec3 srgbIn)
{
    return vec3(pow(srgbIn.xyz, vec3(GAMMA)));
}
vec4 sRGBToLinear(vec4 srgbIn)
{
    return vec4(sRGBToLinear(srgbIn.xyz), srgbIn.w);
}
// ACES tone map (faster approximation)
// see: https://knarkowicz.wordpress.com/2016/01/06/aces-filmic-tone-mapping-curve/
vec3 toneMapACES_Narkowicz(vec3 color)
{
    const float A = 2.51;
    const float B = 0.03;
    const float C = 2.43;
    const float D = 0.59;
    const float E = 0.14;
    return clamp((color * (A * color + B)) / (color * (C * color + D) + E), 0.0, 1.0);
}
// ACES filmic tone map approximation
// see https://github.com/TheRealMJP/BakingLab/blob/master/BakingLab/ACES.hlsl
vec3 RRTAndODTFit(vec3 color)
{
    vec3 a = color * (color + 0.0245786) - 0.000090537;
    vec3 b = color * (0.983729 * color + 0.4329510) + 0.238081;
    return a / b;
}
// tone mapping
vec3 toneMapACES_Hill(vec3 color)
{
    color = ACESInputMat * color;

    // Apply RRT and ODT
    color = RRTAndODTFit(color);

    color = ACESOutputMat * color;

    // Clamp to [0, 1]
    color = clamp(color, 0.0, 1.0);

    return color;
}
// Khronos PBR neutral tone mapping
#ifdef TONEMAP_KHR_PBR_NEUTRAL
vec3 toneMap_KhronosPbrNeutral( vec3 color )
{
    const float startCompression = 0.8 - 0.04;
    const float desaturation = 0.15;

    float x = min(color.r, min(color.g, color.b));
    float offset = x < 0.08 ? x - 6.25 * x * x : 0.04;
    color -= offset;

    float peak = max(color.r, max(color.g, color.b));
    if (peak < startCompression) return color;

    const float d = 1. - startCompression;
    float newPeak = 1. - d * d / (peak + d - startCompression);
    color *= newPeak / peak;

    float g = 1. - 1. / (desaturation * (peak - newPeak) + 1.);
    return mix(color, newPeak * vec3(1, 1, 1), g);
}
#endif
vec3 toneMap(vec3 color)
{
	if (TONEMAP == TONEMAP_ACES_NARKOWICZ)
	{
		color = toneMapACES_Narkowicz(color);
	}
	else if (TONEMAP == TONEMAP_ACES_HILL)
	{
		color = toneMapACES_Hill(color);
	}
	else if (TONEMAP == TONEMAP_ACES_HILL_EXPOSURE_BOOST)
	{
		// boost exposure as discussed in https://github.com/mrdoob/three.js/pull/19621
		// this factor is based on the exposure correction of Krzysztof Narkowicz in his
		// implemetation of ACES tone mapping
		color /= 0.6;
		color = toneMapACES_Hill(color);
	}
	else if (TONEMAP == TONEMAP_KHR_PBR_NEUTRAL)
	{
		color = toneMap_KhronosPbrNeutral(color);
	}
    return linearTosRGB(color);
}

float clampedDot(vec3 x, vec3 y)
{
	return clamp(dot(x, y), 0.0, 1.0);
}
float max3(vec3 v)
{
	return max(max(v.x, v.y), v.z);
}
float sq(float t)
{
    return t * t;
}
vec2 sq(vec2 t)
{
    return t * t;
}
vec3 sq(vec3 t)
{
    return t * t;
}
vec3 rgb_mix(vec3 base, vec3 layer, vec3 rgb_alpha)
{
    float rgb_alpha_max = max(rgb_alpha.r, max(rgb_alpha.g, rgb_alpha.b));
    return (1.0 - rgb_alpha_max) * base + rgb_alpha * layer;
}
#define M_PI 3.141592653589793

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

#define ALPHAMODE_OPAQUE 0
#define ALPHAMODE_MASK 1
#define ALPHAMODE_BLEND 2

#define LightType_Directional 0
#define LightType_Point 1
#define LightType_Spot 2

//#define HAS_COLOR_0_VEC3
#define HAS_COLOR_0_VEC4
#define HAS_NORMAL_VEC3
#define HAS_TANGENT_VEC4

/****** constants *****/
layout(constant_id = 1) const int USE_IBL = 1;
layout(constant_id = 2) const int USE_PUNCTUAL = 1;
layout(constant_id = 3) const int DEBUG = 0;

/****** attributes *****/
layout (location = 0) in vec3 v_Position;
layout (location = 1) in vec2 v_texcoord_0;
layout (location = 2) in vec2 v_texcoord_1;
#ifdef HAS_COLOR_0_VEC4
layout (location = 3) in vec4 v_Color;
#endif
#ifdef HAS_NORMAL_VEC3
#ifdef HAS_TANGENT_VEC4
layout (location = 4) in mat3 v_TBN;
#else
layout (location = 4) in vec3 v_Normal;
#endif
#endif

layout(std140, set = MATERIAL_SET, binding = MATERIAL_BINDING) uniform PbrMaterialUniforms
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
    
    int u_AlphaMode;
    int u_mtl_padding_0;
    int u_mtl_padding_1;
    int u_mtl_padding_2;
};
layout(set = MATERIAL_SET, binding = MTL_TEX_BASE_COLOR_BINDING) uniform sampler2D u_BaseColorSampler; 
layout(set = MATERIAL_SET, binding = MTL_TEX_NORMAL_BINDING) uniform sampler2D u_NormalSampler;
layout(set = MATERIAL_SET, binding = MTL_TEX_METALLIC_ROUGHNESS_BINDING) uniform sampler2D u_MetallicRoughnessSampler; 
layout(set = MATERIAL_SET, binding = MTL_TEX_SHEEN_COLOR_BINDING) uniform sampler2D u_SheenColorSampler; 
layout(set = MATERIAL_SET, binding = MTL_TEX_SHEEN_ROUGHNESS_BINDING) uniform sampler2D u_SheenRoughnessSampler;
layout(set = MATERIAL_SET, binding = MTL_TEX_CLEARCOAT_BINDING) uniform sampler2D u_ClearcoatSampler;
layout(set = MATERIAL_SET, binding = MTL_TEX_CLEARCOAT_ROUGHNESS_BINDING) uniform sampler2D u_ClearcoatRoughnessSampler; 
layout(set = MATERIAL_SET, binding = MTL_TEX_CLEARCOAT_NORMAL_BINDING) uniform sampler2D u_ClearcoatNormalSampler;
layout(set = MATERIAL_SET, binding = MTL_TEX_OCCLUSION_BINDING) uniform sampler2D u_OcclusionSampler;
layout(set = MATERIAL_SET, binding = MTL_TEX_EMISSIVE_BINDING) uniform sampler2D u_EmissiveSampler;
layout(set = MATERIAL_SET, binding = MTL_TEX_TRANSMISSION_BINDING) uniform sampler2D u_TransmissionSampler;
layout(set = MATERIAL_SET, binding = MTL_TEX_THICKNESS_BINDING) uniform sampler2D u_ThicknessSampler;
layout(set = MATERIAL_SET, binding = MTL_TEX_IRIDESCENE_BINDING) uniform sampler2D u_IridescenceSampler;
layout(set = MATERIAL_SET, binding = MTL_TEX_IRIDESCENE_THICKNESS_BINDING) uniform sampler2D u_IridescenceThicknessSampler;
layout(set = MATERIAL_SET, binding = MTL_TEX_DIFFUSE_TRANSIMISSION_BINDING) uniform sampler2D u_DiffuseTransmissionSampler;
layout(set = MATERIAL_SET, binding = MTL_TEX_DIFFUSE_TRANSIMISSION_COLOR_BINDING) uniform sampler2D u_DiffuseTransmissionColorSampler;
layout(set = MATERIAL_SET, binding = MTL_TEX_ANISOTROPY_BINDING) uniform sampler2D u_AnisotropySampler;

//#define HAS_DIFFUSE_MAP
//#define HAS_SPECULAR_MAP
//#define HAS_SPECULAR_COLOR_MAP
//#define HAS_SPECULAR_GLOSSINESS_MAP
#define HAS_BASE_COLOR_MAP (u_BaseColorUVSet >= 0)
#define HAS_NORMAL_MAP (u_NormalUVSet >= 0)
#define HAS_METALLIC_ROUGHNESS_MAP (u_MetallicRoughnessUVSet >= 0)
#define HAS_SHEEN_COLOR_MAP (u_SheenColorUVSet >= 0)
#define HAS_SHEEN_ROUGHNESS_MAP (u_SheenRoughnessUVSet >= 0)
#define HAS_CLEARCOAT_MAP (u_ClearcoatUVSet >= 0)
#define HAS_CLEARCOAT_ROUGHNESS_MAP (u_ClearcoatRoughnessUVSet >= 0)
#define HAS_CLEARCOAT_NORMAL_MAP (u_ClearcoatNormalUVSet >= 0)
#define HAS_OCCLUSION_MAP (u_OcclusionUVSet >= 0)
#define HAS_EMISSIVE_MAP (u_EmissiveUVSet >= 0)
#define HAS_TRANSMISSION_MAP (u_TransmissionUVSet >= 0)
#define HAS_THICKNESS_MAP (u_ThicknessUVSet >= 0)
#define HAS_IRIDESCENCE_MAP (u_IridescenceUVSet >= 0)
#define HAS_IRIDESCENCE_THICKNESS_MAP (u_IridescenceThicknessUVSet >= 0)
#define HAS_DIFFUSE_TRANSMISSION_MAP (u_DiffuseTransmissionUVSet >= 0)
#define HAS_DIFFUSE_TRANSMISSION_COLOR_MAP (u_DiffuseTransmissionColorUVSet >= 0)
#define HAS_ANISOTROPY_MAP (u_AnisotropyUVSet >= 0)

//#define MATERIAL_UNLIT
//#define MATERIAL_SPECULARGLOSSINESS
//#define MATERIAL_SPECULAR
#define MATERIAL_IOR
#define MATERIAL_METALLICROUGHNESS (u_MetallicRoughnessUVSet >= -1)
#define MATERIAL_SHEEN (u_SheenColorUVSet >= -1)
#define MATERIAL_CLEARCOAT (u_ClearcoatUVSet >= -1)
#define MATERIAL_TRANSMISSION (u_TransmissionUVSet >= -1)
#define MATERIAL_VOLUME (u_ThicknessUVSet >= -1)
#define MATERIAL_IRIDESCENCE (u_IridescenceUVSet >= -1)
#define MATERIAL_DIFFUSE_TRANSMISSION (u_DiffuseTransmissionUVSet >= -1)
#define MATERIAL_ANISOTROPY (u_AnisotropyUVSet >= -1)
#define MATERIAL_DISPERSION (u_Dispersion != 0.0)
#define MATERIAL_EMISSIVE_STRENGTH (u_EmissiveStrength != 1.0)

//#define HAS_DIFFUSE_UV_TRANSFORM
//#define HAS_SPECULAR_UV_TRANSFORM
//#define HAS_SPECULARCOLOR_UV_TRANSFORM
#define HAS_BASECOLOR_UV_TRANSFORM
#define HAS_NORMAL_UV_TRANSFORM
#define HAS_METALLICROUGHNESS_UV_TRANSFORM
#define HAS_SHEENCOLOR_UV_TRANSFORM
#define HAS_SHEENROUGHNESS_UV_TRANSFORM
#define HAS_CLEARCOAT_UV_TRANSFORM
#define HAS_CLEARCOATROUGHNESS_UV_TRANSFORM
#define HAS_CLEARCOATNORMAL_UV_TRANSFORM
#define HAS_TRANSMISSION_UV_TRANSFORM
#define HAS_THICKNESS_UV_TRANSFORM
#define HAS_OCCLUSION_UV_TRANSFORM
#define HAS_EMISSIVE_UV_TRANSFORM
#define HAS_IRIDESCENCE_UV_TRANSFORM
#define HAS_IRIDESCENCETHICKNESS_UV_TRANSFORM
#define HAS_DIFFUSETRANSMISSION_UV_TRANSFORM
#define HAS_DIFFUSETRANSMISSIONCOLOR_UV_TRANSFORM
#define HAS_ANISOTROPY_UV_TRANSFORM

layout(push_constant) uniform PushConsts {
	mat4 u_ModelMatrix;
} pushConstants;

layout(std140, set = CAMERA_SET, binding = CAMERA_BINDING) uniform CameraUniforms 
{
    mat4 u_ViewMatrix;
    mat4 u_ProjectionMatrix;
    vec3 u_Camera;
    float u_Exposure;
};

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
layout(std140, set = LIGHT_SET, binding = LIGHT_BINDING) uniform LightUniforms 
{
	Light u_Lights[LIGHT_COUNT];
	int u_LightCount;
};

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
layout(set = ENVIROMENT_SET, binding = ENV_TEX_GGX_ENV_BIDING) uniform samplerCube u_GGXEnvSampler;
layout(set = ENVIROMENT_SET, binding = ENV_TEX_GGX_LUT_BIDING) uniform sampler2D u_GGXLUT;
layout(set = ENVIROMENT_SET, binding = ENV_TEX_LAMBERT_ENV_BIDING) uniform samplerCube u_LambertianEnvSampler;
layout(set = ENVIROMENT_SET, binding = ENV_TEX_CHARLIE_ENV_BIDING) uniform samplerCube u_CharlieEnvSampler;
layout(set = ENVIROMENT_SET, binding = ENV_TEX_CHARLIE_LUT_BIDING) uniform sampler2D u_CharlieLUT;
layout(set = ENVIROMENT_SET, binding = ENV_TEX_SHEEN_ELUT_BIDING) uniform sampler2D u_SheenELUT;
layout(set = ENVIROMENT_SET, binding = ENV_TEX_TRANSMISSION_FRAMEBUFFER_BIDING) uniform sampler2D u_TransmissionFramebufferSampler;

/***** output *****/
layout(location = 0) out vec4 g_finalColor;

/***** functions *****/
struct MaterialInfo
{
    float ior;
    float perceptualRoughness;      // roughness value, as authored by the model creator (input to shader)
    vec3 f0_dielectric;

    float alphaRoughness;           // roughness mapped to a more linear change in the roughness (proposed by [2])

    float fresnel_w;

    vec3 f90;                       // reflectance color at grazing angle
    vec3 f90_dielectric;
    float metallic;

    vec3 baseColor;

    float sheenRoughnessFactor;
    vec3 sheenColorFactor;

    vec3 clearcoatF0;
    vec3 clearcoatF90;
    float clearcoatFactor;
    vec3 clearcoatNormal;
    float clearcoatRoughness;

    // KHR_materials_specular 
    float specularWeight; // product of specularFactor and specularTexture.a

    float transmissionFactor;

    float thickness;
    vec3 attenuationColor;
    float attenuationDistance;

    // KHR_materials_iridescence 
    float iridescenceFactor;
    float iridescenceIor;
    float iridescenceThickness;

    float diffuseTransmissionFactor;
    vec3 diffuseTransmissionColorFactor;

    // KHR_materials_anisotropy
    vec3 anisotropicT;
    vec3 anisotropicB;
    float anisotropyStrength;

    // KHR_materials_dispersion
    float dispersion;
};
#if 0
MaterialInfo initMaterialInfo()
{
	MaterialInfo m;
    m.ior = 1.5;
    m.perceptualRoughness = 0;      // roughness value, as authored by the model creator (input to shader)
    m.f0_dielectric = vec3(0);

    m.alphaRoughness = 0;           // roughness mapped to a more linear change in the roughness (proposed by [2])

    m.fresnel_w = 0;

    m.f90 = vec3(1);                       // reflectance color at grazing angle
    m.f90_dielectric = vec3(1);
    m.metallic = 0;

    m.baseColor = vec3(1);

    m.sheenRoughnessFactor = 0;
    m.sheenColorFactor = vec3(0);

    m.clearcoatF0 = vec3(1);
    m.clearcoatF90 = vec3(1);
    m.clearcoatFactor = 0;
    m.clearcoatNormal = vec3(0);
    m.clearcoatRoughness = 0;

    //KHR_materials_specular 
    m.specularWeight = 0; // product of specularFactor and specularTexture.a

    m.transmissionFactor = 0;

    m.thickness = 0;
    m.attenuationColor = vec3(1);
    m.attenuationDistance = 0;

    //KHR_materials_iridescence
    m.iridescenceFactor = 0;
    m.iridescenceIor = 0;
    m.iridescenceThickness = 0;

    m.diffuseTransmissionFactor = 0;
    m.diffuseTransmissionColorFactor = vec3(1);

    //KHR_materials_anisotropy
    m.anisotropicT = vec3(0);
    m.anisotropicB = vec3(0);
    m.anisotropyStrength = 0;

    //KHR_materials_dispersion
    m.dispersion = 0;
	return m;
}
#endif

/*  BaseColor  */
vec4 getVertexColor()
{
   vec4 color = vec4(1.0);
#ifdef HAS_COLOR_0_VEC3
    color.rgb = v_Color.rgb;
#endif
#ifdef HAS_COLOR_0_VEC4
    color = v_Color;
#endif
   return color;
}
vec2 getBaseColorUV()
{
    vec3 uv = vec3(u_BaseColorUVSet < 1 ? v_texcoord_0 : v_texcoord_1, 1.0);
#ifdef HAS_BASECOLOR_UV_TRANSFORM
    uv = u_BaseColorUVTransform * uv;
#endif
    return uv.xy;
}
vec4 getBaseColor()
{
    vec4 baseColor = vec4(1);
#if defined(MATERIAL_SPECULARGLOSSINESS)
    baseColor = u_DiffuseFactor;
#elif defined(MATERIAL_METALLICROUGHNESS)
    baseColor = u_BaseColorFactor;
#endif

#if defined(MATERIAL_SPECULARGLOSSINESS) && defined(HAS_DIFFUSE_MAP)
    baseColor *= texture(u_DiffuseSampler, getDiffuseUV());
#elif defined(MATERIAL_METALLICROUGHNESS)
    if (HAS_BASE_COLOR_MAP) baseColor *= texture(u_BaseColorSampler, getBaseColorUV());
#endif
    return baseColor * getVertexColor();
}

/*  Normal  */
vec2 getNormalUV()
{
    vec3 uv = vec3(u_NormalUVSet < 1 ? v_texcoord_0 : v_texcoord_1, 1.0);
#ifdef HAS_NORMAL_UV_TRANSFORM
    uv = u_NormalUVTransform * uv;
#endif
    return uv.xy;
}
struct NormalInfo 
{
    vec3 ng;   // Geometry normal
    vec3 t;    // Geometry tangent
    vec3 b;    // Geometry bitangent
    vec3 n;    // Shading normal
    vec3 ntex; // Normal from texture, scaling is accounted for.
};
NormalInfo getNormalInfo(vec3 v)
{
    vec2 UV = getNormalUV();
#if !defined(HAS_NORMAL_VEC3) || !defined(HAS_TANGENT_VEC4)
	/*
	https://learnopengl.com/Advanced-Lighting/Normal-Mapping
	|dP1| = |dU1 dV1| * |T|
	|dP2|   |dU2 dV2|   |B|
	=>
	|T| = | dV2 -dV1| * |dP1|
	|B|   |-dU2  dU1|   |dP2|  / (dU1*dV2 - dV1*dU2)
	*/
#if 0
	vec3 dUV1 = dFdx(vec3(UV, 0.0));
	vec3 dUV2 = dFdy(vec3(UV, 0.0));
	float dV1 = dUV1.y;
	float dV2 = dUV2.y;

	vec3 dP1 = dFdx(v_Position);
	vec3 dP2 = dFdy(v_Position);

	vec3 t_ = (dV2 * dP1 - dV1 * dP2) / (dU1 * dV2 - dV1 * dU2);
	// t_为从UV空间到世界空间转换矩阵基的X分量
#else   
    vec2 uv_dx = dFdx(UV);
    vec2 uv_dy = -dFdy(UV);
    if (length(uv_dx) <= 1e-2) uv_dx = vec2(1.0, 0.0);
    if (length(uv_dy) <= 1e-2) uv_dy = vec2(0.0, 1.0);
    vec3 t_ = (uv_dy.t * dFdx(v_Position) - uv_dx.t * -dFdy(v_Position)) / (uv_dx.s * uv_dy.t - uv_dy.s * uv_dx.t);
#endif
#endif
    
    vec3 n, t, b, ng;
    // Compute geometrical TBN:
#ifdef HAS_NORMAL_VEC3
#ifdef HAS_TANGENT_VEC4
    // Trivial TBN computation, present as vertex attribute.
    // Normalize eigenvectors as matrix is linearly interpolated.
    t = normalize(v_TBN[0]);
    b = normalize(v_TBN[1]);
    ng = normalize(v_TBN[2]);
#else
    // Normals are either present as vertex attributes or approximated.
    ng = normalize(v_Normal);
    t = normalize(t_ - ng * dot(ng, t_));
    b = cross(ng, t);
#endif
#else
    ng = normalize(cross(dFdx(v_Position), dFdy(v_Position)));
    t = normalize(t_ - ng * dot(ng, t_));
    b = cross(ng, t);
#endif

#ifndef NOT_TRIANGLE
    // For a back-facing surface, the tangential basis vectors are negated.
    if (gl_FrontFacing == false)
    {
        t *= -1.0;
        b *= -1.0;
        ng *= -1.0;
    }
#endif

    // Compute normals:
    NormalInfo info;
    info.ng = ng;
	if (HAS_NORMAL_MAP)
	{
		info.ntex = texture(u_NormalSampler, UV).rgb * 2.0 - vec3(1.0);
		info.ntex *= vec3(u_NormalScale, u_NormalScale, 1.0);
		info.ntex = normalize(info.ntex);
		info.n = normalize(mat3(t, b, ng) * info.ntex);
	}
	else
	{
		info.n = ng;
	}
    info.t = t;
    info.b = b;
    return info;
}

/*  Ior  */
#ifdef MATERIAL_IOR
MaterialInfo getIorInfo(MaterialInfo info)
{
    info.f0_dielectric = vec3(pow(( u_Ior - 1.0) /  (u_Ior + 1.0), 2.0));
    info.ior = u_Ior;
    return info;
}
#endif

/*  Metallic Roughness  */
#ifdef MATERIAL_METALLICROUGHNESS
vec2 getMetallicRoughnessUV()
{
    vec3 uv = vec3(u_MetallicRoughnessUVSet < 1 ? v_texcoord_0 : v_texcoord_1, 1.0);
#ifdef HAS_METALLICROUGHNESS_UV_TRANSFORM
    uv = u_MetallicRoughnessUVTransform * uv;
#endif
    return uv.xy;
}
MaterialInfo getMetallicRoughnessInfo(MaterialInfo info)
{
    info.metallic = u_MetallicFactor;
    info.perceptualRoughness = u_RoughnessFactor;
	if (HAS_METALLIC_ROUGHNESS_MAP)
	{
		// Roughness is stored in the 'g' channel, metallic is stored in the 'b' channel.
		// This layout intentionally reserves the 'r' channel for (optional) occlusion map data
		vec4 mrSample = texture(u_MetallicRoughnessSampler, getMetallicRoughnessUV());
		info.perceptualRoughness *= mrSample.g;
		info.metallic *= mrSample.b;
	}
    return info;
}
#endif

/*  Sheen  */
#ifdef MATERIAL_SHEEN
vec2 getSheenColorUV()
{
    vec3 uv = vec3(u_SheenColorUVSet < 1 ? v_texcoord_0 : v_texcoord_1, 1.0);
#ifdef HAS_SHEENCOLOR_UV_TRANSFORM
    uv = u_SheenColorUVTransform * uv;
#endif
    return uv.xy;
}
vec2 getSheenRoughnessUV()
{
    vec3 uv = vec3(u_SheenRoughnessUVSet < 1 ? v_texcoord_0 : v_texcoord_1, 1.0);
#ifdef HAS_SHEENROUGHNESS_UV_TRANSFORM
    uv = u_SheenRoughnessUVTransform * uv;
#endif
    return uv.xy;
}
MaterialInfo getSheenInfo(MaterialInfo info)
{
    info.sheenColorFactor = u_SheenColorFactor;
	if (HAS_SHEEN_COLOR_MAP)
	{
		vec4 sheenColorSample = texture(u_SheenColorSampler, getSheenColorUV());
		info.sheenColorFactor *= sheenColorSample.rgb;
	}

    info.sheenRoughnessFactor = u_SheenRoughnessFactor;
	if (HAS_SHEEN_ROUGHNESS_MAP)
	{
		vec4 sheenRoughnessSample = texture(u_SheenRoughnessSampler, getSheenRoughnessUV());
		info.sheenRoughnessFactor *= sheenRoughnessSample.a;
	}
    return info;
}
#endif

/*  ClearCoat  */
#ifdef MATERIAL_CLEARCOAT
vec2 getClearcoatUV()
{
    vec3 uv = vec3(u_ClearcoatUVSet < 1 ? v_texcoord_0 : v_texcoord_1, 1.0);
#ifdef HAS_CLEARCOAT_UV_TRANSFORM
    uv = u_ClearcoatUVTransform * uv;
#endif
    return uv.xy;
}
vec2 getClearcoatRoughnessUV()
{
    vec3 uv = vec3(u_ClearcoatRoughnessUVSet < 1 ? v_texcoord_0 : v_texcoord_1, 1.0);
#ifdef HAS_CLEARCOATROUGHNESS_UV_TRANSFORM
    uv = u_ClearcoatRoughnessUVTransform * uv;
#endif
    return uv.xy;
}
vec2 getClearcoatNormalUV()
{
    vec3 uv = vec3(u_ClearcoatNormalUVSet < 1 ? v_texcoord_0 : v_texcoord_1, 1.0);
#ifdef HAS_CLEARCOATNORMAL_UV_TRANSFORM
    uv = u_ClearcoatNormalUVTransform * uv;
#endif
    return uv.xy;
}
vec3 getClearcoatNormal(NormalInfo normalInfo)
{
	if (HAS_CLEARCOAT_NORMAL_MAP)
	{
		vec3 n = texture(u_ClearcoatNormalSampler, getClearcoatNormalUV()).rgb * 2.0 - vec3(1.0);
		n *= vec3(u_ClearcoatNormalScale, u_ClearcoatNormalScale, 1.0);
		n = mat3(normalInfo.t, normalInfo.b, normalInfo.ng) * normalize(n);
		return n;
	}
	else
	{    
		return normalInfo.ng;
	}
}
MaterialInfo getClearCoatInfo(MaterialInfo info, NormalInfo normalInfo)
{
    info.clearcoatFactor = u_ClearcoatFactor;
    info.clearcoatRoughness = u_ClearcoatRoughnessFactor;
    info.clearcoatF0 = vec3(pow((info.ior - 1.0) / (info.ior + 1.0), 2.0));
    info.clearcoatF90 = vec3(1.0);
	if (HAS_CLEARCOAT_MAP)
	{
		vec4 clearcoatSample = texture(u_ClearcoatSampler, getClearcoatUV());
		info.clearcoatFactor *= clearcoatSample.r;
	}
	if (HAS_CLEARCOAT_ROUGHNESS_MAP)
	{   
		vec4 clearcoatSampleRoughness = texture(u_ClearcoatRoughnessSampler, getClearcoatRoughnessUV());
		info.clearcoatRoughness *= clearcoatSampleRoughness.g;
	}
    info.clearcoatNormal = getClearcoatNormal(normalInfo);
    info.clearcoatRoughness = clamp(info.clearcoatRoughness, 0.0, 1.0);
    return info;
}
#endif

/*  Occlusion  */
vec2 getOcclusionUV()
{
    vec3 uv = vec3(u_OcclusionUVSet < 1 ? v_texcoord_0 : v_texcoord_1, 1.0);
#ifdef HAS_OCCLUSION_UV_TRANSFORM
    uv = u_OcclusionUVTransform * uv;
#endif
    return uv.xy;
}
float getOcclusion()
{
    float ao = 1.0;
    ao = texture(u_OcclusionSampler,  getOcclusionUV()).r;
    return (1.0 + u_OcclusionStrength * (ao - 1.0)); 
}

/* Emissive */
vec2 getEmissiveUV()
{
    vec3 uv = vec3(u_EmissiveUVSet < 1 ? v_texcoord_0 : v_texcoord_1, 1.0);
#ifdef HAS_EMISSIVE_UV_TRANSFORM
    uv = u_EmissiveUVTransform * uv;
#endif
    return uv.xy;
}
vec3 getEmissive()
{
    vec3 f_emissive = u_EmissiveFactor;
#ifdef MATERIAL_EMISSIVE_STRENGTH
    f_emissive *= u_EmissiveStrength;
#endif
	if (HAS_EMISSIVE_MAP)
		f_emissive *= texture(u_EmissiveSampler, getEmissiveUV()).rgb;
    return f_emissive;
}

/*  Transmission Dispersion  */
#ifdef MATERIAL_TRANSMISSION
vec2 getTransmissionUV()
{
    vec3 uv = vec3(u_TransmissionUVSet < 1 ? v_texcoord_0 : v_texcoord_1, 1.0);
#ifdef HAS_TRANSMISSION_UV_TRANSFORM
    uv = u_TransmissionUVTransform * uv;
#endif
    return uv.xy;
}
MaterialInfo getTransmissionInfo(MaterialInfo info)
{
    info.transmissionFactor = u_TransmissionFactor;
	if (HAS_TRANSMISSION_MAP)
	{
		vec4 transmissionSample = texture(u_TransmissionSampler, getTransmissionUV());
		info.transmissionFactor *= transmissionSample.r;
	}

#ifdef MATERIAL_DISPERSION
    info.dispersion = u_Dispersion;
#else
    info.dispersion = 0.0;
#endif
    return info;
}
#endif

/*  Volume  */
#ifdef MATERIAL_VOLUME
vec2 getThicknessUV()
{
    vec3 uv = vec3(u_ThicknessUVSet < 1 ? v_texcoord_0 : v_texcoord_1, 1.0);
#ifdef HAS_THICKNESS_UV_TRANSFORM
    uv = u_ThicknessUVTransform * uv;
#endif
    return uv.xy;
}
MaterialInfo getVolumeInfo(MaterialInfo info)
{
    info.thickness = u_ThicknessFactor;
	if (HAS_THICKNESS_MAP)
	{
		vec4 thicknessSample = texture(u_ThicknessSampler, getThicknessUV());
		info.thickness *= thicknessSample.g;
	}
    info.attenuationColor = u_AttenuationColor;
    info.attenuationDistance = u_AttenuationDistance;
    return info;
}
#endif

/* Iridescence */
#ifdef MATERIAL_IRIDESCENCE
vec2 getIridescenceUV()
{
    vec3 uv = vec3(u_IridescenceUVSet < 1 ? v_texcoord_0 : v_texcoord_1, 1.0);
#ifdef HAS_IRIDESCENCE_UV_TRANSFORM
    uv = u_IridescenceUVTransform * uv;
#endif
    return uv.xy;
}
vec2 getIridescenceThicknessUV()
{
    vec3 uv = vec3(u_IridescenceThicknessUVSet < 1 ? v_texcoord_0 : v_texcoord_1, 1.0);
#ifdef HAS_IRIDESCENCETHICKNESS_UV_TRANSFORM
    uv = u_IridescenceThicknessUVTransform * uv;
#endif
    return uv.xy;
}
MaterialInfo getIridescenceInfo(MaterialInfo info)
{
    info.iridescenceFactor = u_IridescenceFactor;
	if (HAS_IRIDESCENCE_MAP)
	{
		info.iridescenceFactor *= texture(u_IridescenceSampler, getIridescenceUV()).r;
	}

    info.iridescenceThickness = u_IridescenceThicknessMaximum;
	if (HAS_IRIDESCENCE_THICKNESS_MAP)
	{
		float thicknessSampled = texture(u_IridescenceThicknessSampler, getIridescenceThicknessUV()).g;
		float thickness = mix(u_IridescenceThicknessMinimum, u_IridescenceThicknessMaximum, thicknessSampled);
		info.iridescenceThickness = thickness;
	}

    info.iridescenceIor = u_IridescenceIor;
    return info;
}
#endif

/* DiffuseTransmission */
#ifdef MATERIAL_DIFFUSE_TRANSMISSION
vec2 getDiffuseTransmissionUV()
{
    vec3 uv = vec3(u_DiffuseTransmissionUVSet < 1 ? v_texcoord_0 : v_texcoord_1, 1.0);
#ifdef HAS_DIFFUSETRANSMISSION_UV_TRANSFORM
    uv = u_DiffuseTransmissionUVTransform * uv;
#endif
    return uv.xy;
}
vec2 getDiffuseTransmissionColorUV()
{
    vec3 uv = vec3(u_DiffuseTransmissionColorUVSet < 1 ? v_texcoord_0 : v_texcoord_1, 1.0);
#ifdef HAS_DIFFUSETRANSMISSIONCOLOR_UV_TRANSFORM
    uv = u_DiffuseTransmissionColorUVTransform * uv;
#endif
    return uv.xy;
}
MaterialInfo getDiffuseTransmissionInfo(MaterialInfo info)
{
    info.diffuseTransmissionFactor = u_DiffuseTransmissionFactor;
	if (HAS_DIFFUSE_TRANSMISSION_MAP)
		info.diffuseTransmissionFactor *= texture(u_DiffuseTransmissionSampler, getDiffuseTransmissionUV()).a;

    info.diffuseTransmissionColorFactor = u_DiffuseTransmissionColorFactor;
	if (HAS_DIFFUSE_TRANSMISSION_COLOR_MAP)
		info.diffuseTransmissionColorFactor *= texture(u_DiffuseTransmissionColorSampler, getDiffuseTransmissionColorUV()).rgb;
    return info;
}
#endif

/* Anisotropy */
#ifdef MATERIAL_ANISOTROPY
vec2 getAnisotropyUV()
{
    vec3 uv = vec3(u_AnisotropyUVSet < 1 ? v_texcoord_0 : v_texcoord_1, 1.0);
#ifdef HAS_ANISOTROPY_UV_TRANSFORM
    uv = u_AnisotropyUVTransform * uv;
#endif
    return uv.xy;
}
MaterialInfo getAnisotropyInfo(MaterialInfo info, NormalInfo normalInfo)
{
    vec2 direction = vec2(1.0, 0.0);
    float strengthFactor = 1.0;
	if (HAS_ANISOTROPY_MAP)
    {
		vec3 anisotropySample = texture(u_AnisotropySampler, getAnisotropyUV()).xyz;
		direction = anisotropySample.xy * 2.0 - vec2(1.0);
		strengthFactor = anisotropySample.z;
	}
    vec2 directionRotation = u_Anisotropy.xy; // cos(theta), sin(theta)
    mat2 rotationMatrix = mat2(directionRotation.x, directionRotation.y, -directionRotation.y, directionRotation.x);
    direction = rotationMatrix * direction.xy;

    info.anisotropicT = mat3(normalInfo.t, normalInfo.b, normalInfo.n) * normalize(vec3(direction, 0.0));
    info.anisotropicB = cross(normalInfo.ng, info.anisotropicT);
    info.anisotropyStrength = clamp(u_Anisotropy.z * strengthFactor, 0.0, 1.0);
    return info;
}
#endif

/* Iridescence */
float F_Schlick(float f0, float f90, float VdotH)
{
    float x = clamp(1.0 - VdotH, 0.0, 1.0);
    float x2 = x * x;
    float x5 = x * x2 * x2;
    return f0 + (f90 - f0) * x5;
}
float F_Schlick(float f0, float VdotH)
{
    float f90 = 1.0; //clamp(50.0 * f0, 0.0, 1.0);
    return F_Schlick(f0, f90, VdotH);
}
vec3 F_Schlick(vec3 f0, vec3 f90, float VdotH) 
{
    return f0 + (f90 - f0) * pow(clamp(1.0 - VdotH, 0.0, 1.0), 5.0);
}
vec3 F_Schlick(vec3 f0, float f90, float VdotH)
{
    float x = clamp(1.0 - VdotH, 0.0, 1.0);
    float x2 = x * x;
    float x5 = x * x2 * x2;
    return f0 + (f90 - f0) * x5;
}
vec3 F_Schlick(vec3 f0, float VdotH)
{
    float f90 = 1.0; //clamp(dot(f0, vec3(50.0 * 0.33)), 0.0, 1.0);
    return F_Schlick(f0, f90, VdotH);
}
// ior is a value between 1.0 and 3.0. 1.0 is air interface
float IorToFresnel0(float transmittedIor, float incidentIor) {
    return sq((transmittedIor - incidentIor) / (transmittedIor + incidentIor));
}
vec3 IorToFresnel0(vec3 transmittedIor, float incidentIor) {
    return sq((transmittedIor - vec3(incidentIor)) / (transmittedIor + vec3(incidentIor)));
}
// Assume air interface for top
// Note: We don't handle the case fresnel0 == 1
vec3 Fresnel0ToIor(vec3 fresnel0) {
    vec3 sqrtF0 = sqrt(fresnel0);
    return (vec3(1.0) + sqrtF0) / (vec3(1.0) - sqrtF0);
}
// Fresnel equations for dielectric/dielectric interfaces.
// Ref: https://belcour.github.io/blog/research/2017/05/01/brdf-thin-film.html
// Evaluation XYZ sensitivity curves in Fourier space
const mat3 XYZ_TO_REC709 = mat3(
     3.2404542, -0.9692660,  0.0556434,
    -1.5371385,  1.8760108, -0.2040259,
    -0.4985314,  0.0415560,  1.0572252
);
vec3 evalSensitivity(float OPD, vec3 shift) 
{
    float phase = 2.0 * M_PI * OPD * 1.0e-9;
    vec3 val = vec3(5.4856e-13, 4.4201e-13, 5.2481e-13);
    vec3 pos = vec3(1.6810e+06, 1.7953e+06, 2.2084e+06);
    vec3 var = vec3(4.3278e+09, 9.3046e+09, 6.6121e+09);

    vec3 xyz = val * sqrt(2.0 * M_PI * var) * cos(pos * phase + shift) * exp(-sq(phase) * var);
    xyz.x += 9.7470e-14 * sqrt(2.0 * M_PI * 4.5282e+09) * cos(2.2399e+06 * phase + shift[0]) * exp(-4.5282e+09 * sq(phase));
    xyz /= 1.0685e-7;

    vec3 srgb = XYZ_TO_REC709 * xyz;
    return srgb;
}
vec3 evalIridescence(float outsideIOR, float eta2, float cosTheta1, float thinFilmThickness, vec3 baseF0) 
{
    vec3 I;

    // Force iridescenceIor -> outsideIOR when thinFilmThickness -> 0.0
    float iridescenceIor = mix(outsideIOR, eta2, smoothstep(0.0, 0.03, thinFilmThickness));
    // Evaluate the cosTheta on the base layer (Snell law)
    float sinTheta2Sq = sq(outsideIOR / iridescenceIor) * (1.0 - sq(cosTheta1));

    // Handle TIR:
    float cosTheta2Sq = 1.0 - sinTheta2Sq;
    if (cosTheta2Sq < 0.0) return vec3(1.0);
    float cosTheta2 = sqrt(cosTheta2Sq);

    // First interface
    float R0 = IorToFresnel0(iridescenceIor, outsideIOR);
    float R12 = F_Schlick(R0, cosTheta1);
    float R21 = R12;
    float T121 = 1.0 - R12;
    float phi12 = 0.0;
    if (iridescenceIor < outsideIOR) phi12 = M_PI;
    float phi21 = M_PI - phi12;

    // Second interface
    vec3 baseIOR = Fresnel0ToIor(clamp(baseF0, 0.0, 0.9999)); // guard against 1.0
    vec3 R1 = IorToFresnel0(baseIOR, iridescenceIor);
    vec3 R23 = F_Schlick(R1, cosTheta2);
    vec3 phi23 = vec3(0.0);
    if (baseIOR[0] < iridescenceIor) phi23[0] = M_PI;
    if (baseIOR[1] < iridescenceIor) phi23[1] = M_PI;
    if (baseIOR[2] < iridescenceIor) phi23[2] = M_PI;

    // Phase shift
    float OPD = 2.0 * iridescenceIor * thinFilmThickness * cosTheta2;
    vec3 phi = vec3(phi21) + phi23;

    // Compound terms
    vec3 R123 = clamp(R12 * R23, 1e-5, 0.9999);
    vec3 r123 = sqrt(R123);
    vec3 Rs = sq(T121) * R23 / (vec3(1.0) - R123);

    // Reflectance term for m = 0 (DC term amplitude)
    vec3 C0 = R12 + Rs;
    I = C0;

    // Reflectance term for m > 0 (pairs of diracs)
    vec3 Cm = Rs - T121;
    for (int m = 1; m <= 2; ++m)
    {
        Cm *= r123;
        vec3 Sm = 2.0 * evalSensitivity(float(m) * OPD, float(m) * phi);
        I += Cm * Sm;
    }
    // Since out of gamut colors might be produced, negative color values are clamped to 0.
    return max(I, vec3(0.0));
}

/* IBL Radiance */
vec4 getSpecularSample(vec3 reflection, float lod)
{
    vec4 textureSample = textureLod(u_GGXEnvSampler, u_EnvRotation * reflection, lod);
    textureSample.rgb *= u_EnvIntensity;
    return textureSample;
}
vec3 getIBLRadianceGGX(vec3 n, vec3 v, float roughness)
{
    float NdotV = clampedDot(n, v);
    float lod = roughness * float(u_MipCount - 1);
    vec3 reflection = normalize(reflect(-v, n));
    vec4 specularSample = getSpecularSample(reflection, lod);

    vec3 specularLight = specularSample.rgb;
    return specularLight;
}
#ifdef MATERIAL_ANISOTROPY
vec3 getIBLRadianceAnisotropy(vec3 n, vec3 v, float roughness, float anisotropy, vec3 anisotropyDirection)
{
    float NdotV = clampedDot(n, v);

    float tangentRoughness = mix(roughness, 1.0, anisotropy * anisotropy);
    vec3  anisotropicTangent  = cross(anisotropyDirection, v);
    vec3  anisotropicNormal   = cross(anisotropicTangent, anisotropyDirection);
    float bendFactor          = 1.0 - anisotropy * (1.0 - roughness);
    float bendFactorPow4      = bendFactor * bendFactor * bendFactor * bendFactor;
    vec3  bentNormal          = normalize(mix(anisotropicNormal, n, bendFactorPow4));

    float lod = roughness * float(u_MipCount - 1);
    vec3 reflection = normalize(reflect(-v, bentNormal));
    
    vec4 specularSample = getSpecularSample(reflection, lod);
    vec3 specularLight = specularSample.rgb;
    return specularLight;
}
#endif

/* IBL Irridiance */
vec3 getDiffuseLight(vec3 n)
{
    vec4 textureSample = texture(u_LambertianEnvSampler, u_EnvRotation * n);
    textureSample.rgb *= u_EnvIntensity;
    return textureSample.rgb;
}

/* IBL Volume Attenuatio */
vec3 getVolumeTransmissionRay(vec3 n, vec3 v, float thickness, float ior, mat4 modelMatrix)
{
    // Direction of refracted light.
    vec3 refractionVector = refract(-v, normalize(n), 1.0 / ior);
    // Compute rotation-independant scaling of the model matrix.
    vec3 modelScale;
    modelScale.x = length(vec3(modelMatrix[0].xyz));
    modelScale.y = length(vec3(modelMatrix[1].xyz));
    modelScale.z = length(vec3(modelMatrix[2].xyz));
    // The thickness is specified in local space.
    return normalize(refractionVector) * thickness * modelScale;
}
// Compute attenuated light as it travels through a volume.
vec3 applyVolumeAttenuation(vec3 radiance, float transmissionDistance, vec3 attenuationColor, float attenuationDistance)
{
    if (attenuationDistance == 0.0)
    {
        // Attenuation distance is +∞ (which we indicate by zero), i.e. the transmitted color is not attenuated at all.
        return radiance;
    }
    else
    {
        // Compute light attenuation using Beer's law.
        vec3 transmittance = pow(attenuationColor, vec3(transmissionDistance / attenuationDistance));
        return transmittance * radiance;
    }
}
#ifdef MATERIAL_TRANSMISSION
float applyIorToRoughness(float roughness, float ior)
{
    // Scale roughness with IOR so that an IOR of 1.0 results in no microfacet refraction and
    // an IOR of 1.5 results in the default amount of microfacet refraction.
    return roughness * clamp(ior * 2.0 - 2.0, 0.0, 1.0);
}
vec3 getTransmissionSample(vec2 fragCoord, float roughness, float ior)
{
    float framebufferLod = log2(float(u_TransmissionFramebufferSize.x)) * applyIorToRoughness(roughness, ior);
    vec3 transmittedLight = textureLod(u_TransmissionFramebufferSampler, fragCoord.xy, framebufferLod).rgb;
    return transmittedLight;
}
vec3 getIBLVolumeRefraction(vec3 n, vec3 v, float perceptualRoughness, vec3 baseColor, vec3 f0, vec3 f90,
    vec3 position, mat4 modelMatrix, mat4 viewMatrix, mat4 projMatrix, 
    float ior, float thickness, vec3 attenuationColor, float attenuationDistance, float dispersion)
{
    vec3 transmittedLight;
    float transmissionRayLength;
    if (MATERIAL_DISPERSION)
	{
        // Dispersion will spread out the ior values for each r,g,b channel
        float halfSpread = (ior - 1.0) * 0.025 * dispersion;
        vec3 iors = vec3(ior - halfSpread, ior, ior + halfSpread);

        for (int i = 0; i < 3; i++)
        {
            vec3 transmissionRay = getVolumeTransmissionRay(n, v, thickness, iors[i], modelMatrix);
            // TODO: taking length of blue ray, ideally we would take the length of the green ray. For now overwriting seems ok
            transmissionRayLength = length(transmissionRay);
            vec3 refractedRayExit = position + transmissionRay;

            // Project refracted vector on the framebuffer, while mapping to normalized device coordinates.
            vec4 ndcPos = projMatrix * viewMatrix * vec4(refractedRayExit, 1.0);
            vec2 refractionCoords = ndcPos.xy / ndcPos.w;
            refractionCoords += 1.0;
            refractionCoords /= 2.0;

            // Sample framebuffer to get pixel the refracted ray hits for this color channel.
            transmittedLight[i] = getTransmissionSample(refractionCoords, perceptualRoughness, iors[i])[i];
        }
    }
	else
	{
        vec3 transmissionRay = getVolumeTransmissionRay(n, v, thickness, ior, modelMatrix);
        transmissionRayLength = length(transmissionRay);
        vec3 refractedRayExit = position + transmissionRay;

        // Project refracted vector on the framebuffer, while mapping to normalized device coordinates.
        vec4 ndcPos = projMatrix * viewMatrix * vec4(refractedRayExit, 1.0);
        vec2 refractionCoords = ndcPos.xy / ndcPos.w;
        refractionCoords += 1.0;
        refractionCoords /= 2.0;

        // Sample framebuffer to get pixel the refracted ray hits.
        transmittedLight = getTransmissionSample(refractionCoords, perceptualRoughness, ior);
    }
    
    // Sample GGX LUT to get the specular component.
    float NdotV = clampedDot(n, v);
    vec2 brdfSamplePoint = clamp(vec2(NdotV, perceptualRoughness), vec2(0.0, 0.0), vec2(1.0, 1.0));
    vec2 brdf = texture(u_GGXLUT, brdfSamplePoint).rg;
    vec3 specularColor = f0 * brdf.x + f90 * brdf.y;
    
    vec3 attenuatedColor = applyVolumeAttenuation(transmittedLight, transmissionRayLength, attenuationColor, attenuationDistance);
    return (1.0 - specularColor) * attenuatedColor * baseColor;
}
#endif

/* IBL Radiance LUT */
vec3 getIBLGGXFresnel(vec3 n, vec3 v, float roughness, vec3 F0, float specularWeight)
{
    // see https://bruop.github.io/ibl/#single_scattering_results at Single Scattering Results
    // Roughness dependent fresnel, from Fdez-Aguera
    float NdotV = clampedDot(n, v);
    vec2 brdfSamplePoint = clamp(vec2(NdotV, roughness), vec2(0.0, 0.0), vec2(1.0, 1.0));
    vec2 f_ab = texture(u_GGXLUT, brdfSamplePoint).rg;
    
    vec3 Fr = max(vec3(1.0 - roughness), F0) - F0;
    vec3 k_S = F0 + Fr * pow(1.0 - NdotV, 5.0);
    vec3 FssEss = specularWeight * (k_S * f_ab.x + f_ab.y);

    // Multiple scattering, from Fdez-Aguera
    float Ems = (1.0 - (f_ab.x + f_ab.y));
    vec3 F_avg = specularWeight * (F0 + (1.0 - F0) / 21.0);
    vec3 FmsEms = Ems * FssEss * F_avg / (1.0 - F_avg * Ems);

    return FssEss + FmsEms;
}

/* IBL Sheen */
vec4 getSheenSample(vec3 reflection, float lod)
{
    vec4 textureSample =  textureLod(u_CharlieEnvSampler, u_EnvRotation * reflection, lod);
    textureSample.rgb *= u_EnvIntensity;
    return textureSample;
}
vec3 getIBLRadianceCharlie(vec3 n, vec3 v, float sheenRoughness, vec3 sheenColor)
{
    float NdotV = clampedDot(n, v);
    float lod = sheenRoughness * float(u_MipCount - 1);
    vec3 reflection = normalize(reflect(-v, n));
    vec4 sheenSample = getSheenSample(reflection, lod);
    vec3 sheenLight = sheenSample.rgb;
    
    vec2 brdfSamplePoint = clamp(vec2(NdotV, sheenRoughness), vec2(0.0, 0.0), vec2(1.0, 1.0));
    float brdf = texture(u_CharlieLUT, brdfSamplePoint).b;

    return sheenLight * sheenColor * brdf;
}
float albedoSheenScalingLUT(float NdotV, float sheenRoughnessFactor)
{
    return texture(u_SheenELUT, vec2(NdotV, sheenRoughnessFactor)).r;
}

/* IBL Light */
// https://github.com/KhronosGroup/glTF/blob/master/extensions/2.0/Khronos/KHR_lights_punctual/README.md#range-property
float getRangeAttenuation(float range, float distance)
{
    if (range <= 0.0)
    {
        // negative range means unlimited
        return 1.0 / pow(distance, 2.0);
    }
    return max(min(1.0 - pow(distance / range, 4.0), 1.0), 0.0) / pow(distance, 2.0);
}
// https://github.com/KhronosGroup/glTF/blob/master/extensions/2.0/Khronos/KHR_lights_punctual/README.md#inner-and-outer-cone-angles
float getSpotAttenuation(vec3 pointToLight, vec3 spotDirection, float outerConeCos, float innerConeCos)
{
    float actualCos = dot(normalize(spotDirection), normalize(-pointToLight));
    if (actualCos > outerConeCos)
    {
        if (actualCos < innerConeCos)
        {
            float angularAttenuation = (actualCos - outerConeCos) / (innerConeCos - outerConeCos);
            return angularAttenuation * angularAttenuation;
        }
        return 1.0;
    }
    return 0.0;
}
vec3 getLighIntensity(Light light, vec3 pointToLight)
{
    float rangeAttenuation = 1.0;
    if (light.type != LightType_Directional)
    {
        rangeAttenuation = getRangeAttenuation(light.range, length(pointToLight));
    }
    float spotAttenuation = 1.0;
    if (light.type == LightType_Spot)
    {
        spotAttenuation = getSpotAttenuation(pointToLight, light.direction, light.outerConeCos, light.innerConeCos);
    }
    return rangeAttenuation * spotAttenuation * light.intensity * light.color;
}

/* Light Diffuse */
//https://github.com/KhronosGroup/glTF/tree/master/specification/2.0#acknowledgments AppendixB
vec3 BRDF_lambertian(vec3 diffuseColor)
{
    // see https://seblagarde.wordpress.com/2012/01/08/pi-or-not-to-pi-in-game-lighting-equation/
    return (diffuseColor / M_PI);
}

/* Light BTDF */
// The following equation(s) model the distribution of microfacet normals across the area being drawn (aka D())
// Implementation from "Average Irregularity Representation of a Roughened Surface for Ray Reflection" by T. S. Trowbridge, and K. P. Reitz
// Follows the distribution function recommended in the SIGGRAPH 2013 course notes from EPIC Games [1], Equation 3.
float D_GGX(float NdotH, float alphaRoughness)
{
    float alphaRoughnessSq = alphaRoughness * alphaRoughness;
    float f = (NdotH * NdotH) * (alphaRoughnessSq - 1.0) + 1.0;
    return alphaRoughnessSq / (M_PI * f * f);
}
// Smith Joint GGX
// Note: Vis = G / (4 * NdotL * NdotV)
// see Eric Heitz. 2014. Understanding the Masking-Shadowing Function in Microfacet-Based BRDFs. Journal of Computer Graphics Techniques, 3
// see Real-Time Rendering. Page 331 to 336.
// see https://google.github.io/filament/Filament.md.html#materialsystem/specularbrdf/geometricshadowing(specularg)
float V_GGX(float NdotL, float NdotV, float alphaRoughness)
{
    float alphaRoughnessSq = alphaRoughness * alphaRoughness;

    float GGXV = NdotL * sqrt(NdotV * NdotV * (1.0 - alphaRoughnessSq) + alphaRoughnessSq);
    float GGXL = NdotV * sqrt(NdotL * NdotL * (1.0 - alphaRoughnessSq) + alphaRoughnessSq);

    float GGX = GGXV + GGXL;
    if (GGX > 0.0)
    {
        return 0.5 / GGX;
    }
    return 0.0;
}
vec3 getPunctualRadianceTransmission(vec3 normal, vec3 view, vec3 pointToLight, float alphaRoughness,
    vec3 f0, vec3 f90, vec3 baseColor, float ior)
{
    float transmissionRougness = applyIorToRoughness(alphaRoughness, ior);

    vec3 n = normalize(normal);           // Outward direction of surface point
    vec3 v = normalize(view);             // Direction from surface point to view
    vec3 l = normalize(pointToLight);
    vec3 l_mirror = normalize(l + 2.0*n*dot(-l, n));     // Mirror light reflection vector on surface
    vec3 h = normalize(l_mirror + v);            // Halfway vector between transmission light vector and v

    float D = D_GGX(clamp(dot(n, h), 0.0, 1.0), transmissionRougness);
    vec3 F = F_Schlick(f0, f90, clamp(dot(v, h), 0.0, 1.0));
    float Vis = V_GGX(clamp(dot(n, l_mirror), 0.0, 1.0), clamp(dot(n, v), 0.0, 1.0), transmissionRougness);

    // Transmission BTDF
    return (1.0 - F) * baseColor * D * Vis;
}

/* Light Specular BRDF */
//  https://github.com/KhronosGroup/glTF/tree/master/specification/2.0#acknowledgments AppendixB
vec3 BRDF_specularGGX(float alphaRoughness, float NdotL, float NdotV, float NdotH)
{
    float D = D_GGX(NdotH, alphaRoughness);
    float V = V_GGX(NdotL, NdotV, alphaRoughness);
    return vec3(V * D);
}
#ifdef MATERIAL_ANISOTROPY
// GGX Distribution Anisotropic (Same as Babylon.js)
// https://blog.selfshadow.com/publications/s2012-shading-course/burley/s2012_pbs_disney_brdf_notes_v3.pdf Addenda
float D_GGX_anisotropic(float NdotH, float TdotH, float BdotH, float anisotropy, float at, float ab)
{
    float a2 = at * ab;
    vec3 f = vec3(ab * TdotH, at * BdotH, a2 * NdotH);
    float w2 = a2 / dot(f, f);
    return a2 * w2 * w2 / M_PI;
}
// GGX Mask/Shadowing Anisotropic (Same as Babylon.js - smithVisibility_GGXCorrelated_Anisotropic)
// Heitz http://jcgt.org/published/0003/02/03/paper.pdf
float V_GGX_anisotropic(float NdotL, float NdotV, float BdotV, float TdotV, float TdotL, float BdotL, float at, float ab)
{
    float GGXV = NdotL * length(vec3(at * TdotV, ab * BdotV, NdotV));
    float GGXL = NdotV * length(vec3(at * TdotL, ab * BdotL, NdotL));
    float v = 0.5 / (GGXV + GGXL);
    return clamp(v, 0.0, 1.0);
}
vec3 BRDF_specularGGXAnisotropy(float alphaRoughness, float anisotropy, vec3 n, vec3 v, vec3 l, vec3 h, vec3 t, vec3 b)
{
    // Roughness along the anisotropy bitangent is the material roughness, while the tangent roughness increases with anisotropy.
    float at = mix(alphaRoughness, 1.0, anisotropy * anisotropy);
    float ab = clamp(alphaRoughness, 0.001, 1.0);

    float NdotL = clamp(dot(n, l), 0.0, 1.0);
    float NdotH = clamp(dot(n, h), 0.001, 1.0);
    float NdotV = dot(n, v);

    float D = D_GGX_anisotropic(NdotH, dot(t, h), dot(b, h), anisotropy, at, ab);
    float V = V_GGX_anisotropic(NdotL, NdotV, dot(b, v), dot(t, v), dot(t, l), dot(b, l), at, ab);
    return vec3(V * D);
}
#endif

/* Light ClearCoat BRDF */
vec3 getPunctualRadianceClearCoat(vec3 clearcoatNormal, vec3 v, vec3 l, vec3 h, float VdotH, vec3 f0, vec3 f90, float clearcoatRoughness)
{
    float NdotL = clampedDot(clearcoatNormal, l);
    float NdotV = clampedDot(clearcoatNormal, v);
    float NdotH = clampedDot(clearcoatNormal, h);
    return NdotL * BRDF_specularGGX(clearcoatRoughness * clearcoatRoughness, NdotL, NdotV, NdotH);
}
/* Light Sheen BRDF */
float lambdaSheenNumericHelper(float x, float alphaG)
{
    float oneMinusAlphaSq = (1.0 - alphaG) * (1.0 - alphaG);
    float a = mix(21.5473, 25.3245, oneMinusAlphaSq);
    float b = mix(3.82987, 3.32435, oneMinusAlphaSq);
    float c = mix(0.19823, 0.16801, oneMinusAlphaSq);
    float d = mix(-1.97760, -1.27393, oneMinusAlphaSq);
    float e = mix(-4.32054, -4.85967, oneMinusAlphaSq);
    return a / (1.0 + b * pow(x, c)) + d * x + e;
}
float lambdaSheen(float cosTheta, float alphaG)
{
    if (abs(cosTheta) < 0.5)
    {
        return exp(lambdaSheenNumericHelper(cosTheta, alphaG));
    }
    else
    {
        return exp(2.0 * lambdaSheenNumericHelper(0.5, alphaG) - lambdaSheenNumericHelper(1.0 - cosTheta, alphaG));
    }
}
float V_Sheen(float NdotL, float NdotV, float sheenRoughness)
{
    sheenRoughness = max(sheenRoughness, 0.000001); //clamp (0,1]
    float alphaG = sheenRoughness * sheenRoughness;

    return clamp(1.0 / ((1.0 + lambdaSheen(NdotV, alphaG) + lambdaSheen(NdotL, alphaG)) * (4.0 * NdotV * NdotL)), 0.0, 1.0);
}
// Estevez and Kulla http://www.aconty.com/pdf/s2017_pbs_imageworks_sheen.pdf
float D_Charlie(float sheenRoughness, float NdotH)
{
    sheenRoughness = max(sheenRoughness, 0.000001); //clamp (0,1]
    float alphaG = sheenRoughness * sheenRoughness;
    float invR = 1.0 / alphaG;
    float cos2h = NdotH * NdotH;
    float sin2h = 1.0 - cos2h;
    return (2.0 + invR) * pow(sin2h, invR * 0.5) / (2.0 * M_PI);
}
// f_sheen
vec3 BRDF_specularSheen(vec3 sheenColor, float sheenRoughness, float NdotL, float NdotV, float NdotH)
{
    float sheenDistribution = D_Charlie(sheenRoughness, NdotH);
    float sheenVisibility = V_Sheen(NdotL, NdotV, sheenRoughness);
    return sheenColor * sheenDistribution * sheenVisibility;
}
vec3 getPunctualRadianceSheen(vec3 sheenColor, float sheenRoughness, float NdotL, float NdotV, float NdotH)
{
    return NdotL * BRDF_specularSheen(sheenColor, sheenRoughness, NdotL, NdotV, NdotH);
}

void main()
{
    vec4 baseColor = getBaseColor();
    if (u_AlphaMode == ALPHAMODE_OPAQUE) {
        baseColor.a = 1.0;
    }
    
    vec3 color = vec3(0);

    vec3 v = normalize(u_Camera - v_Position);
    NormalInfo normalInfo = getNormalInfo(v);
    
    vec3 n = normalInfo.n;
    vec3 t = normalInfo.t;
    vec3 b = normalInfo.b;
    float NdotV = clampedDot(n, v);
    float TdotV = clampedDot(t, v);
    float BdotV = clampedDot(b, v);

    MaterialInfo materialInfo;// = initMaterialInfo();
    materialInfo.baseColor = baseColor.rgb;
    // The default index of refraction of 1.5 yields a dielectric normal incidence reflectance of 0.04.
    materialInfo.ior = 1.5;
    materialInfo.f0_dielectric = vec3(0.04);
    materialInfo.specularWeight = 1.0;
    // Anything less than 2% is physically impossible and is instead considered to be shadowing. Compare to "Real-Time-Rendering" 4th editon on page 325.
    materialInfo.f90 = vec3(1.0);
    materialInfo.f90_dielectric = materialInfo.f90;
	
    // If the MR debug output is selected, we have to enforce evaluation of the non-iridescence BRDF functions.
//#if DEBUG == DEBUG_METALLIC_ROUGHNESS
//#undef MATERIAL_IRIDESCENCE
//#endif
#ifdef MATERIAL_IOR
    materialInfo = getIorInfo(materialInfo);
#endif
#ifdef MATERIAL_SPECULARGLOSSINESS
    materialInfo = getSpecularGlossinessInfo(materialInfo);
#endif
	if (MATERIAL_METALLICROUGHNESS)
		materialInfo = getMetallicRoughnessInfo(materialInfo);
	if (MATERIAL_SHEEN)
		materialInfo = getSheenInfo(materialInfo);
	if (MATERIAL_CLEARCOAT)
		materialInfo = getClearCoatInfo(materialInfo, normalInfo);
#ifdef MATERIAL_SPECULAR
	materialInfo = getSpecularInfo(materialInfo);
#endif
	if (MATERIAL_TRANSMISSION)
		materialInfo = getTransmissionInfo(materialInfo);
    else {
        materialInfo.transmissionFactor = 0;
        materialInfo.dispersion = 0;    
    }
	if (MATERIAL_VOLUME)
		materialInfo = getVolumeInfo(materialInfo);
    else {
        materialInfo.thickness = 0;
        materialInfo.attenuationColor = vec3(0);
        materialInfo.attenuationDistance = 0;    
    }
	if (MATERIAL_IRIDESCENCE)
		materialInfo = getIridescenceInfo(materialInfo);
	if (MATERIAL_DIFFUSE_TRANSMISSION)
		materialInfo = getDiffuseTransmissionInfo(materialInfo);
	if (MATERIAL_ANISOTROPY)
		materialInfo = getAnisotropyInfo(materialInfo, normalInfo);
    materialInfo.perceptualRoughness = clamp(materialInfo.perceptualRoughness, 0.0, 1.0);
    materialInfo.metallic = clamp(materialInfo.metallic, 0.0, 1.0);
    // Roughness is authored as perceptual roughness; as is convention,
    // convert to material roughness by squaring the perceptual roughness.
    materialInfo.alphaRoughness = materialInfo.perceptualRoughness * materialInfo.perceptualRoughness;

    // LIGHTING
    vec3 iridescenceFresnel_dielectric;
    vec3 iridescenceFresnel_metallic;
	if (MATERIAL_IRIDESCENCE)
	{
		iridescenceFresnel_dielectric = evalIridescence(1.0, materialInfo.iridescenceIor, NdotV, materialInfo.iridescenceThickness, materialInfo.f0_dielectric);
		iridescenceFresnel_metallic = evalIridescence(1.0, materialInfo.iridescenceIor, NdotV, materialInfo.iridescenceThickness, baseColor.rgb);
		if (materialInfo.iridescenceThickness == 0.0) materialInfo.iridescenceFactor = 0.0;
	}

    float diffuseTransmissionThickness = 1.0;
	if (MATERIAL_DIFFUSE_TRANSMISSION && MATERIAL_VOLUME)
		diffuseTransmissionThickness = materialInfo.thickness * 
        (length(vec3(pushConstants.u_ModelMatrix[0].xyz)) + length(vec3(pushConstants.u_ModelMatrix[1].xyz)) + length(vec3(pushConstants.u_ModelMatrix[2].xyz))) / 3.0;
    
    float clearcoatFactor = 0.0;
    vec3 clearcoatFresnel = vec3(0);
	if (MATERIAL_CLEARCOAT)
	{
		clearcoatFactor = materialInfo.clearcoatFactor;
		clearcoatFresnel = F_Schlick(materialInfo.clearcoatF0, materialInfo.clearcoatF90, clampedDot(materialInfo.clearcoatNormal, v));
	}

    // IBL
    vec3 f_diffuse = vec3(0.0);
    vec3 f_specular_transmission = vec3(0.0);
    vec3 f_specular_metal = vec3(0.0);
    vec3 f_specular_dielectric = vec3(0.0);
    vec3 f_metal_brdf_ibl = vec3(0.0);    
    vec3 f_dielectric_brdf_ibl = vec3(0.0);  
    vec3 clearcoat_brdf = vec3(0.0);
    vec3 f_sheen = vec3(0.0);
    float albedoSheenScaling = 1.0;
if (USE_IBL != 0 || MATERIAL_TRANSMISSION)
{
    f_diffuse = getDiffuseLight(n) * baseColor.rgb ;
    if (MATERIAL_DIFFUSE_TRANSMISSION)
	{
        vec3 diffuseTransmissionIBL = getDiffuseLight(-n) * materialInfo.diffuseTransmissionColorFactor;
        if (MATERIAL_VOLUME)
            diffuseTransmissionIBL = applyVolumeAttenuation(diffuseTransmissionIBL, diffuseTransmissionThickness, materialInfo.attenuationColor, materialInfo.attenuationDistance);
        f_diffuse = mix(f_diffuse, diffuseTransmissionIBL, materialInfo.diffuseTransmissionFactor);
    }

    if (MATERIAL_TRANSMISSION)
	{
        f_specular_transmission = getIBLVolumeRefraction(
            n, v,
            materialInfo.perceptualRoughness,
            baseColor.rgb, materialInfo.f0_dielectric, materialInfo.f90,
            v_Position, pushConstants.u_ModelMatrix, u_ViewMatrix, u_ProjectionMatrix,
            materialInfo.ior, materialInfo.thickness, materialInfo.attenuationColor, materialInfo.attenuationDistance, materialInfo.dispersion);
        f_diffuse = mix(f_diffuse, f_specular_transmission, materialInfo.transmissionFactor);
    }

    if (MATERIAL_ANISOTROPY)
	{
        f_specular_metal = getIBLRadianceAnisotropy(n, v, materialInfo.perceptualRoughness, materialInfo.anisotropyStrength, materialInfo.anisotropicB);
        f_specular_dielectric = f_specular_metal;
    }
	else
    {
		f_specular_metal = getIBLRadianceGGX(n, v, materialInfo.perceptualRoughness);
        f_specular_dielectric = f_specular_metal;
    }

    // Calculate fresnel mix for IBL  
    vec3 f_metal_fresnel_ibl = getIBLGGXFresnel(n, v, materialInfo.perceptualRoughness, baseColor.rgb, 1.0);
    f_metal_brdf_ibl = f_metal_fresnel_ibl * f_specular_metal;
    vec3 f_dielectric_fresnel_ibl = getIBLGGXFresnel(n, v, materialInfo.perceptualRoughness, materialInfo.f0_dielectric, materialInfo.specularWeight);
    f_dielectric_brdf_ibl = mix(f_diffuse, f_specular_dielectric,  f_dielectric_fresnel_ibl);
    if (MATERIAL_IRIDESCENCE)
	{
        f_metal_brdf_ibl = mix(f_metal_brdf_ibl, f_specular_metal * iridescenceFresnel_metallic, materialInfo.iridescenceFactor);
        f_dielectric_brdf_ibl = mix(f_dielectric_brdf_ibl, rgb_mix(f_diffuse, f_specular_dielectric, iridescenceFresnel_dielectric), materialInfo.iridescenceFactor);
    }
    color = mix(f_dielectric_brdf_ibl, f_metal_brdf_ibl, materialInfo.metallic);
    
    if (MATERIAL_SHEEN)
	{
        f_sheen = getIBLRadianceCharlie(n, v, materialInfo.sheenRoughnessFactor, materialInfo.sheenColorFactor);
        albedoSheenScaling = 1.0 - max3(materialInfo.sheenColorFactor) * albedoSheenScalingLUT(NdotV, materialInfo.sheenRoughnessFactor);
    }
    color = f_sheen + color * albedoSheenScaling;

    if (MATERIAL_CLEARCOAT)
	{
        clearcoat_brdf = getIBLRadianceGGX(materialInfo.clearcoatNormal, v, materialInfo.clearcoatRoughness);
    }
    color = mix(color, clearcoat_brdf, clearcoatFactor * clearcoatFresnel);

    if (HAS_OCCLUSION_MAP)
	{
        color = color * getOcclusion(); 
	}
}

    // Punctual
if (USE_PUNCTUAL != 0)
{
    for (int i = 0; i < u_LightCount; ++i)
    {
        Light light = u_Lights[i];

        vec3 pointToLight;
        if (light.type != LightType_Directional)
        {
            pointToLight = light.position - v_Position;
        }
        else
        {
            pointToLight = -light.direction;
        }

        vec3 l = normalize(pointToLight);   // Direction from surface point to light
        vec3 h = normalize(l + v);          // Direction of the vector between l and v, called halfway vector
        float NdotL = clampedDot(n, l);
        float NdotV = clampedDot(n, v);
        float NdotH = clampedDot(n, h);
        float LdotH = clampedDot(l, h);
        float VdotH = clampedDot(v, h);
        
        // Diffuse
        vec3 lightIntensity = getLighIntensity(light, pointToLight);
        vec3 l_diffuse = lightIntensity * NdotL * BRDF_lambertian(baseColor.rgb);
		if (MATERIAL_DIFFUSE_TRANSMISSION)
		{
			vec3 diffuse_btdf = lightIntensity * clampedDot(-n, l) * BRDF_lambertian(materialInfo.diffuseTransmissionColorFactor);
			if (MATERIAL_VOLUME)
				diffuse_btdf = applyVolumeAttenuation(diffuse_btdf, diffuseTransmissionThickness, materialInfo.attenuationColor, materialInfo.attenuationDistance);
			l_diffuse = mix(l_diffuse, diffuse_btdf, materialInfo.diffuseTransmissionFactor);
		}

        // BTDF (Bidirectional Transmittance Distribution Function)
		if (MATERIAL_TRANSMISSION)
		{
			// If the light ray travels through the geometry, use the point it exits the geometry again.
			// That will change the angle to the light source, if the material refracts the light ray.
			vec3 transmissionRay = getVolumeTransmissionRay(n, v, materialInfo.thickness, materialInfo.ior, pushConstants.u_ModelMatrix);
			pointToLight -= transmissionRay;
			l = normalize(pointToLight);

			vec3 transmittedLight = lightIntensity * getPunctualRadianceTransmission(n, v, l, materialInfo.alphaRoughness, materialInfo.f0_dielectric, materialInfo.f90, baseColor.rgb, materialInfo.ior);
			if (MATERIAL_VOLUME)
				transmittedLight = applyVolumeAttenuation(transmittedLight, length(transmissionRay), materialInfo.attenuationColor, materialInfo.attenuationDistance);
			l_diffuse = mix(l_diffuse, transmittedLight, materialInfo.transmissionFactor);
		}

        // BRDF
        // https://github.com/KhronosGroup/glTF/tree/master/specification/2.0#acknowledgments AppendixB
        vec3 intensity = getLighIntensity(light, pointToLight);    
        vec3 l_specular_metal = vec3(0.0);
        vec3 l_specular_dielectric = vec3(0.0);
		if (MATERIAL_ANISOTROPY)
		{
			l_specular_metal = intensity * NdotL * BRDF_specularGGXAnisotropy(materialInfo.alphaRoughness, materialInfo.anisotropyStrength, n, v, l, h, materialInfo.anisotropicT, materialInfo.anisotropicB);
			l_specular_dielectric = l_specular_metal;
		}
		else
		{
			l_specular_metal = intensity * NdotL * BRDF_specularGGX(materialInfo.alphaRoughness, NdotL, NdotV, NdotH);
			l_specular_dielectric = l_specular_metal;
		}

        vec3 dielectric_fresnel = F_Schlick(materialInfo.f0_dielectric * materialInfo.specularWeight, materialInfo.f90_dielectric, abs(VdotH));
        vec3 metal_fresnel = F_Schlick(baseColor.rgb, vec3(1.0), abs(VdotH));
        vec3 l_metal_brdf = metal_fresnel * l_specular_metal;
        vec3 l_dielectric_brdf = mix(l_diffuse, l_specular_dielectric, dielectric_fresnel); // Do we need to handle vec3 fresnel here?
		if (MATERIAL_IRIDESCENCE)
		{
			l_metal_brdf = mix(l_metal_brdf, l_specular_metal * iridescenceFresnel_metallic, materialInfo.iridescenceFactor);
			l_dielectric_brdf = mix(l_dielectric_brdf, rgb_mix(l_diffuse, l_specular_dielectric, iridescenceFresnel_dielectric), materialInfo.iridescenceFactor);
		}
        vec3 l_color = mix(l_dielectric_brdf, l_metal_brdf, materialInfo.metallic);
        
        vec3 l_sheen = vec3(0.0);
        float l_albedoSheenScaling = 1.0;
		if (MATERIAL_SHEEN)
		{
			l_sheen = intensity * getPunctualRadianceSheen(materialInfo.sheenColorFactor, materialInfo.sheenRoughnessFactor, NdotL, NdotV, NdotH);
			l_albedoSheenScaling = min(1.0 - max3(materialInfo.sheenColorFactor) * albedoSheenScalingLUT(NdotV, materialInfo.sheenRoughnessFactor),
				1.0 - max3(materialInfo.sheenColorFactor) * albedoSheenScalingLUT(NdotL, materialInfo.sheenRoughnessFactor));
		}
        l_color = l_sheen + l_color * l_albedoSheenScaling;
        
        vec3 l_clearcoat_brdf = vec3(0.0);
		if (MATERIAL_CLEARCOAT)
		{
			l_clearcoat_brdf = intensity * getPunctualRadianceClearCoat(materialInfo.clearcoatNormal, v, l, h, VdotH,
				materialInfo.clearcoatF0, materialInfo.clearcoatF90, materialInfo.clearcoatRoughness);
		}
        l_color = mix(l_color, l_clearcoat_brdf, clearcoatFactor * clearcoatFresnel);
        
        color += l_color;
    }
}

    // Emissive
    vec3 f_emissive = getEmissive();
#ifdef MATERIAL_UNLIT
    color = baseColor.rgb;
#elif defined(NOT_TRIANGLE) && !defined(HAS_NORMAL_VEC3)
    //Points or Lines with no NORMAL attribute SHOULD be rendered without lighting and instead use the sum of the base color value and the emissive value.
    color = f_emissive + baseColor.rgb;
#else
    color = f_emissive * (1.0 - clearcoatFactor * clearcoatFresnel) + color;
#endif

    if (u_AlphaMode == ALPHAMODE_MASK) {
        // Late discard to avoid sampling artifacts. See https://github.com/KhronosGroup/glTF-Sample-Viewer/issues/267
        if (baseColor.a < u_AlphaCutoff) discard;
        baseColor.a = 1.0;
    }

	if (TONEMAP == TONEMAP_LINEAR) g_finalColor = vec4(color.rgb, baseColor.a);
	else g_finalColor = vec4(toneMap(color * u_Exposure), baseColor.a);
	
    // Debug views:
    if (DEBUG != DEBUG_NONE)
    {
        g_finalColor = vec4(1.0);
     
        float frequency = 0.02;
        float gray = 0.9;

        vec2 v1 = step(0.5, fract(frequency * gl_FragCoord.xy));
        vec2 v2 = step(0.5, vec2(1.0) - fract(frequency * gl_FragCoord.xy));
        g_finalColor.rgb *= gray + v1.x * v1.y + v2.x * v2.y;
    }
    
	if (DEBUG == DEBUG_IBL_DIFFUSE) g_finalColor.rgb = f_diffuse;
	if (DEBUG == DEBUG_IBL_SPECULAR_TRANSMISSION) g_finalColor.rgb = f_specular_transmission;
	if (DEBUG == DEBUG_IBL_SPECULAR_METAL) g_finalColor.rgb = f_specular_metal;
	if (DEBUG == DEBUG_IBL_SPECULAR_DIELECTRIC) g_finalColor.rgb = f_specular_dielectric;
	if (DEBUG == DEBUG_IBL_BRDF_METAL) g_finalColor.rgb = f_metal_brdf_ibl;
	if (DEBUG == DEBUG_IBL_BRDF_DIELECTRIC) g_finalColor.rgb = f_dielectric_brdf_ibl;
	if (DEBUG == DEBUG_IBL_BRDF_CLEARCOAT) g_finalColor.rgb = clearcoat_brdf;
	if (DEBUG == DEBUG_IBL_SHEEN) g_finalColor.rgb = f_sheen;
    if (DEBUG == DEBUG_IBL_SHEEN_LIGHT)
    {
        float sheenRoughness = materialInfo.sheenRoughnessFactor;
    
        float NdotV = clampedDot(n, v);
        float lod = sheenRoughness * float(u_MipCount - 1);
        vec3 reflection = normalize(reflect(-v, n));
        vec4 sheenSample = getSheenSample(reflection, lod);
        vec3 sheenLight = sheenSample.rgb;
        g_finalColor.rgb = sheenLight;
    }
    if (DEBUG == DEBUG_IBL_SHEEN_BRDF_POINT || DEBUG == DEBUG_IBL_SHEEN_BRDF)
    {
        float sheenRoughness = materialInfo.sheenRoughnessFactor;
        
        float NdotV = clampedDot(n, v);
        vec2 brdfSamplePoint = clamp(vec2(NdotV, sheenRoughness), vec2(0.0, 0.0), vec2(1.0, 1.0));
        float brdf = texture(u_CharlieLUT, brdfSamplePoint).b;
        if (DEBUG == DEBUG_IBL_SHEEN_BRDF_POINT) g_finalColor.rgb = vec3(brdfSamplePoint, 0.0f);
        else g_finalColor.rgb = vec3(brdf);
    }    
    
	if (DEBUG == DEBUG_VECTOR_V) g_finalColor.rgb = v * 0.5 + vec3(0.5);
	if (DEBUG == DEBUG_VECTOR_L && u_LightCount > 0) 
    {
        Light light = u_Lights[0];
        vec3 pointToLight;
        if (light.type != LightType_Directional) pointToLight = light.position - v_Position;
        else pointToLight = -light.direction;
        vec3 l = normalize(pointToLight);
        g_finalColor.rgb = l * 0.5 + vec3(0.5);
    }
    
	if (DEBUG == DEBUG_DIFFUSE) g_finalColor.rgb = f_diffuse;
	if (DEBUG == DEBUG_UV_0) g_finalColor.rgb = vec3(v_texcoord_0, 0);
	//if (DEBUG == DEBUG_UV_1) g_finalColor.rgb = vec3(v_texcoord_1, 0);
	if (DEBUG == DEBUG_UV_1)
	{
        vec2 UV = getNormalUV();
        vec2 uv_dx = dFdx(UV);
        vec2 uv_dy = -dFdy(UV);
        
        if (length(uv_dx) <= 1e-2) uv_dx = vec2(1.0, 0.0);
        if (length(uv_dy) <= 1e-2) uv_dy = vec2(0.0, 1.0);
        vec3 t_ = (uv_dy.t * dFdx(v_Position) - uv_dx.t * -dFdy(v_Position)) / (uv_dx.s * uv_dy.t - uv_dy.s * uv_dx.t);
    
        vec3 n, t, b, ng;
        // Compute geometrical TBN:
    #ifdef HAS_NORMAL_VEC3
    #ifdef HAS_TANGENT_VEC4
        // Trivial TBN computation, present as vertex attribute.
        // Normalize eigenvectors as matrix is linearly interpolated.
        t = normalize(v_TBN[0]);
        b = normalize(v_TBN[1]);
        ng = normalize(v_TBN[2]);
    #else
        // Normals are either present as vertex attributes or approximated.
        ng = normalize(v_Normal);
        t = normalize(t_ - ng * dot(ng, t_));
        b = cross(ng, t);
    #endif
    #else
        ng = normalize(cross(dFdx(v_Position), dFdy(v_Position)));
        t = normalize(t_ - ng * dot(ng, t_));
        b = cross(ng, t);
    #endif    
    
		g_finalColor.rgb = t * 0.5 + vec3(0.5); //vec3(normalize(uv_dy), 0);
	}
	if (HAS_NORMAL_MAP && DEBUG == DEBUG_NORMAL_TEXTURE) g_finalColor.rgb = (normalInfo.ntex + 1.0) / 2.0;
	if (DEBUG == DEBUG_SHADING_NORMAL) g_finalColor.rgb = (n + 1.0) / 2.0;
	if (DEBUG == DEBUG_GEOMETRY_NORMAL) g_finalColor.rgb = (normalInfo.ng + 1.0) / 2.0;
	if (DEBUG == DEBUG_GEOMETRY_TANGENT) g_finalColor.rgb = (normalInfo.t + 1.0) / 2.0;
	if (DEBUG == DEBUG_GEOMETRY_BITANGENT) g_finalColor.rgb = (normalInfo.b + 1.0) / 2.0;

	if (DEBUG == DEBUG_ALPHA) g_finalColor.rgb = vec3(baseColor.a);
	if (HAS_OCCLUSION_MAP && DEBUG == DEBUG_OCCLUSION) g_finalColor.rgb = vec3(texture(u_OcclusionSampler,  getOcclusionUV()).r);
	if (DEBUG == DEBUG_EMISSIVE) g_finalColor.rgb = linearTosRGB(f_emissive);
    // MR:
#ifdef MATERIAL_METALLICROUGHNESS
	if (DEBUG == DEBUG_METALLIC) g_finalColor.rgb = vec3(materialInfo.metallic);
	if (DEBUG == DEBUG_ROUGHNESS) g_finalColor.rgb = vec3(materialInfo.perceptualRoughness);
	if (DEBUG == DEBUG_BASE_COLOR) g_finalColor.rgb = linearTosRGB(materialInfo.baseColor);
#endif
    // Clearcoat:
#ifdef MATERIAL_CLEARCOAT
	if (DEBUG == DEBUG_CLEARCOAT_FACTOR) g_finalColor.rgb = vec3(materialInfo.clearcoatFactor);
	if (DEBUG == DEBUG_CLEARCOAT_ROUGHNESS) g_finalColor.rgb = vec3(materialInfo.clearcoatRoughness);
	if (DEBUG == DEBUG_CLEARCOAT_NORMAL) g_finalColor.rgb = (materialInfo.clearcoatNormal + vec3(1)) / 2.0;
#endif
    // Sheen:
#ifdef MATERIAL_SHEEN
	if (DEBUG == DEBUG_SHEEN_COLOR) g_finalColor.rgb = materialInfo.sheenColorFactor;
	if (DEBUG == DEBUG_SHEEN_ROUGHNESS) g_finalColor.rgb = vec3(materialInfo.sheenRoughnessFactor);
#endif
    // Transmission:
#ifdef MATERIAL_TRANSMISSION
	if (DEBUG == DEBUG_TRANSMISSION_FACTOR) g_finalColor.rgb = vec3(materialInfo.transmissionFactor);
#endif
	// Volume:
#ifdef MATERIAL_VOLUME
	if (DEBUG == DEBUG_VOLUME_THICKNESS) g_finalColor.rgb = vec3(materialInfo.thickness / u_ThicknessFactor);
#endif
    // Iridescence:
#ifdef MATERIAL_IRIDESCENCE
	if (DEBUG == DEBUG_IRIDESCENCE_FACTOR) g_finalColor.rgb = vec3(materialInfo.iridescenceFactor);
	if (DEBUG == DEBUG_IRIDESCENCE_THICKNESS) g_finalColor.rgb = vec3(materialInfo.iridescenceThickness / 1200.0);
#endif
    // Anisotropy:
#ifdef MATERIAL_ANISOTROPY
	if (DEBUG == DEBUG_ANISOTROPIC_STRENGTH) g_finalColor.rgb = vec3(materialInfo.anisotropyStrength);
	if (HAS_ANISOTROPY_MAP && DEBUG == DEBUG_ANISOTROPIC_DIRECTION)
	{
		vec2 direction = vec2(1.0, 0.0);
		direction = texture(u_AnisotropySampler, getAnisotropyUV()).xy;
		direction = direction * 2.0 - vec2(1.0); // [0, 1] -> [-1, 1]

		vec2 directionRotation = u_Anisotropy.xy; // cos(theta), sin(theta)
		mat2 rotationMatrix = mat2(directionRotation.x, directionRotation.y, -directionRotation.y, directionRotation.x);
		direction = (direction + vec2(1.0)) * 0.5; // [-1, 1] -> [0, 1]

		g_finalColor.rgb = vec3(direction, 0.0);
	}
#endif
    // Diffuse Transmission:
#ifdef MATERIAL_DIFFUSE_TRANSMISSION
	if (DEBUG == DEBUG_DIFFUSE_TRANSMISSION_FACTOR) g_finalColor.rgb = linearTosRGB(vec3(materialInfo.diffuseTransmissionFactor));
	if (DEBUG == DEBUG_DIFFUSE_TRANSMISSION_COLOR_FACTOR) g_finalColor.rgb = linearTosRGB(materialInfo.diffuseTransmissionColorFactor);
#endif
}