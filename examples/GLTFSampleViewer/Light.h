#pragma once
#include "GltfShaderStruct.h"

class Light1
{
	Light* param = nullptr;
public:
	Light1(Light* p);

	void setColor(vec3 color) { param->color = color; }
	vec3 getColor() const { return param->color; }
	
	void setIntensity(float intensity) { param->intensity = intensity; }
	float getIntensity() const { return param->intensity; }

	void setPosition(vec3 pos) { param->position = pos; }
	vec3 getPosition() const { return param->position; }

	void setDirection(vec3 pos) { param->direction = pos; }
	vec3 getDirection() const { return param->direction; }

	int setLightType(int type) { param->type = type; }
	int getLightType() const { return param->type; }
};
using LightPtr = std::shared_ptr<Light1>;

class LightManager
{
	vks::VulkanDevice* vulkanDevice_ = nullptr;
	VkDevice device_ = VK_NULL_HANDLE;
	VkDescriptorPool descriptorPool_ = VK_NULL_HANDLE;

	std::vector<LightPtr> lights_;
public:
	LightUniforms params{};

	VkDescriptorSetLayout descriptorSetLayout;
	VkDescriptorSet descriptorSet;
	vks::Buffer uniformBuffer;

public:
	LightManager(vks::VulkanDevice* vulkanDevice, VkDescriptorPool descriptorPool);
	~LightManager();
	void reset();
	void destroy();
	void load(tinygltf::Model& gltfMdl);

	void createDefaultLights();
	LightPtr createLight(int lightType = LightType_Directional);
	void removeAllLights();

	void uploadParams2Gpu();
	void uploadDescriptorSet2Gpu(std::vector<VkWriteDescriptorSet>& writeParams);
};
using LightManagerPtr = std::shared_ptr<LightManager>;