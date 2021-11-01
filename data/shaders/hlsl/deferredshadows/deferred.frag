// Copyright 2020 Google LLC
// Copyright 2021 Sascha Willems

Texture2D textureposition : register(t0, space1);
SamplerState samplerposition : register(s0, space1);
Texture2D textureNormal : register(t1, space1);
SamplerState samplerNormal : register(s1, space1);
Texture2D textureAlbedo : register(t2, space1);
SamplerState samplerAlbedo : register(s2, space1);
// Depth from the light's point of view
Texture2DArray textureShadowMap : register(t3, space1);
SamplerState samplerShadowMap : register(s3, space1);

#include "ubo.include.hlsl"

float textureProj(float4 P, float layer, float2 offset)
{
	float shadow = 1.0;
	float4 shadowCoord = P / P.w;
	shadowCoord.xy = shadowCoord.xy * 0.5 + 0.5;

	if (shadowCoord.z > -1.0 && shadowCoord.z < 1.0)
	{
		float dist = textureShadowMap.Sample(samplerShadowMap, float3(shadowCoord.xy + offset, layer)).r;
		if (shadowCoord.w > 0.0 && dist < shadowCoord.z)
		{
			shadow = SHADOW_FACTOR;
		}
	}
	return shadow;
}

float filterPCF(float4 sc, float layer)
{
	int2 texDim; int elements; int levels;
	textureShadowMap.GetDimensions(0, texDim.x, texDim.y, elements, levels);
	float scale = 1.5;
	float dx = scale * 1.0 / float(texDim.x);
	float dy = scale * 1.0 / float(texDim.y);

	float shadowFactor = 0.0;
	int count = 0;
	int range = 1;

	for (int x = -range; x <= range; x++)
	{
		for (int y = -range; y <= range; y++)
		{
			shadowFactor += textureProj(sc, layer, float2(dx*x, dy*y));
			count++;
		}

	}
	return shadowFactor / count;
}

float3 shadow(float3 fragcolor, float3 fragPos) {
	for (int i = 0; i < LIGHT_COUNT; ++i)
	{
		float4 shadowClip = mul(ubo.lights[i].viewMatrix, float4(fragPos.xyz, 1.0));

		float shadowFactor;
		#ifdef USE_PCF
			shadowFactor= filterPCF(shadowClip, i);
		#else
			shadowFactor = textureProj(shadowClip, i, float2(0.0, 0.0));
		#endif

		fragcolor *= shadowFactor;
	}
	return fragcolor;
}

float4 main([[vk::location(0)]] float2 inUV : TEXCOORD0) : SV_TARGET
{
	// Get G-Buffer values
	float3 fragPos = textureposition.Sample(samplerposition, inUV).rgb;
	float3 normal = textureNormal.Sample(samplerNormal, inUV).rgb;
	float4 albedo = textureAlbedo.Sample(samplerAlbedo, inUV);

	float3 fragcolor;

	// Debug display
	if (ubo.debugDisplayTarget > 0) {
		switch (ubo.debugDisplayTarget) {
			case 1: 
				fragcolor.rgb = shadow(float3(1.0, 1.0, 1.0), fragPos);
				break;
			case 2: 
				fragcolor.rgb = fragPos;
				break;
			case 3: 
				fragcolor.rgb = normal;
				break;
			case 4: 
				fragcolor.rgb = albedo.rgb;
				break;
			case 5: 
				fragcolor.rgb = albedo.aaa;
				break;
		}		
		return float4(fragcolor, 1.0);
	}

	// Ambient part
	fragcolor  = albedo.rgb * AMBIENT_LIGHT;

	float3 N = normalize(normal);

	for(int i = 0; i < LIGHT_COUNT; ++i)
	{
		// Vector to light
		float3 L = ubo.lights[i].position.xyz - fragPos;
		// Distance from light to fragment position
		float dist = length(L);
		L = normalize(L);

		// Viewer to fragment
		float3 V = ubo.viewPos.xyz - fragPos;
		V = normalize(V);

		float lightCosInnerAngle = cos(radians(15.0));
		float lightCosOuterAngle = cos(radians(25.0));
		float lightRange = 100.0;

		// Direction vector from source to target
		float3 dir = normalize(ubo.lights[i].position.xyz - ubo.lights[i].target.xyz);

		// Dual cone spot light with smooth transition between inner and outer angle
		float cosDir = dot(L, dir);
		float spotEffect = smoothstep(lightCosOuterAngle, lightCosInnerAngle, cosDir);
		float heightAttenuation = smoothstep(lightRange, 0.0f, dist);

		// Diffuse lighting
		float NdotL = max(0.0, dot(N, L));
		float3 diff = NdotL.xxx;

		// Specular lighting
		float3 R = reflect(-L, N);
		float NdotR = max(0.0, dot(R, V));
		float3 spec = (pow(NdotR, 16.0) * albedo.a * 2.5).xxx;

		fragcolor += float3((diff + spec) * spotEffect * heightAttenuation) * ubo.lights[i].color.rgb * albedo.rgb;
	}

	// Shadow calculations in a separate pass
	if (ubo.useShadows > 0)
	{
		fragcolor = shadow(fragcolor, fragPos);
	}

	return float4(fragcolor, 1);
}