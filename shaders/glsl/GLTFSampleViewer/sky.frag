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
#define ENV_TEX_GGX_ENV_BIDING 8
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


layout (location = 0) in vec3 inUVW;
layout (location = 0) out vec4 g_finalColor;

void main()
{
    float lod = u_EnvBlurNormalized * float(u_MipCount - 1);
    vec4 color = textureLod(u_GGXEnvSampler, normalize(inUVW), lod);
    color.rgb *= u_EnvIntensity;
    color.a = 1.0;
	
#ifdef LINEAR_OUTPUT
    g_finalColor = color.rgba;
#else
    g_finalColor = vec4(toneMap(color.rgb * u_Exposure), color.a);
#endif
}