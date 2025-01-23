#pragma once
#include "GltfShaderStruct.h"
#include "VulkanFrameBuffer.hpp"

class AnimatedModel;
struct EnviromentImagesPath 
{
	std::string lambertEnvPath;
	std::string ggxEnvPath;
	std::string ggxLutPath;
	std::string charlieEnvPath;
	std::string charlieLutPath;
	std::string sheenLutPath;
};
class Enviroment
{
	vks::VulkanDevice* vulkanDevice_ = nullptr;
	VkDevice device_ = VK_NULL_HANDLE;
	VkDescriptorPool descriptorPool_ = VK_NULL_HANDLE;
	VkQueue queue_ = VK_NULL_HANDLE;

	EnviromentUniforms params{};
public:
	int environmentRotation = 90;
	float envIntensity = 1;
	bool environmentBlur = true;

	VkDescriptorSetLayout descriptorSetLayout;
	VkDescriptorSet descriptorSet;
	vks::Buffer uniformBuffer;

	vks::Texture2D imageGGXLut, imageCharlieLut, imageSheenELut;
	vks::TextureCubeMap imageLambertEnv, imageGGXEnv, imageCharlieEnv;
	VkDescriptorImageInfo transmissionTexture_ = { VK_NULL_HANDLE, VK_NULL_HANDLE };
public:
	Enviroment(vks::VulkanDevice* vulkanDevice, VkDescriptorPool descriptorPool, VkQueue queue);
	~Enviroment();
	void reset();
	void load(EnviromentImagesPath imagePaths, const vks::FramebufferAttachment* transmissionFb = nullptr);
	void destroy();

	void uploadParams2Gpu();
	void uploadDescriptorSet2Gpu(std::vector<VkWriteDescriptorSet>& writeParams);
};
using EnviromentPtr = std::shared_ptr<Enviroment>;
