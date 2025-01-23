#include "Enviroment.h"
#include "AnimatedModel.h"

Enviroment::Enviroment(vks::VulkanDevice* vulkanDevice, VkDescriptorPool descriptorPool, VkQueue queue)
{
	vulkanDevice_ = vulkanDevice;
	device_ = vulkanDevice_->logicalDevice;
	descriptorPool_ = descriptorPool;
	queue_ = queue;
	{
		std::vector<VkDescriptorSetLayoutBinding> setLayoutBinding_Env;
		setLayoutBinding_Env.push_back(vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, ENVIROMENT_BINDING));
		for (int binding = ENVIROMENT_TEXTURE_FIRST_BINDING; binding <= ENVIROMENT_TEXTURE_LAST_BINDING; binding++) {
			setLayoutBinding_Env.push_back(vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, binding));
		}
		VkDescriptorSetLayoutCreateInfo descriptorSetLayoutCI_Tex = vks::initializers::descriptorSetLayoutCreateInfo(setLayoutBinding_Env.data(), setLayoutBinding_Env.size());

		VkDescriptorSetLayoutBindingFlagsCreateInfo dsLayoutBindingFCInfo = {};
		std::vector<VkDescriptorBindingFlags> bindingFlags;
		bindingFlags.push_back(0);
		for (int i = 1; i < setLayoutBinding_Env.size(); ++i) {
			bindingFlags.push_back(VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT);
		}
		dsLayoutBindingFCInfo.bindingCount = bindingFlags.size();
		dsLayoutBindingFCInfo.pBindingFlags = bindingFlags.data();
		dsLayoutBindingFCInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO;
		dsLayoutBindingFCInfo.pNext = nullptr;
		descriptorSetLayoutCI_Tex.pNext = &dsLayoutBindingFCInfo;

		VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device_, &descriptorSetLayoutCI_Tex, nullptr, &descriptorSetLayout));
	}

	VkDescriptorSetAllocateInfo allocInfo = vks::initializers::descriptorSetAllocateInfo(descriptorPool, &descriptorSetLayout, 1);
	VK_CHECK_RESULT(vkAllocateDescriptorSets(device_, &allocInfo, &descriptorSet));

	VK_CHECK_RESULT(vulkanDevice->createBuffer(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &uniformBuffer, sizeof(params)));
}
Enviroment::~Enviroment()
{
	destroy();
}

void Enviroment::destroy()
{
	//vkSafeFreeDescriptorSets(device_, descriptorPool_, 1, descriptorSet);
	vkSafeDestroyDescriptorSetLayout(device_, descriptorSetLayout);
	uniformBuffer.destroy();

	reset();
}
void Enviroment::reset()
{
	vkSafeDestroySampler(device_, transmissionTexture_.sampler);
	vkSafeDestroyImageView(device_, transmissionTexture_.imageView);

	imageGGXLut.destroy();
	imageCharlieLut.destroy();
	imageSheenELut.destroy();
	imageLambertEnv.destroy();
	imageGGXEnv.destroy();
	imageCharlieEnv.destroy();
}

void Enviroment::load(EnviromentImagesPath imgs, const vks::FramebufferAttachment* transmissionFb)
{
	reset();

	vks::Texture2D::SamplerOption lutOpt;
	lutOpt.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	lutOpt.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	lutOpt.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	lutOpt.anisotropyEnable = true;
	vks::Texture2D::SamplerOption envOpt;
	lutOpt.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	lutOpt.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	lutOpt.addressModeW = VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
	lutOpt.anisotropyEnable = true;
	SAFE_ASSERT(imageGGXEnv.loadFromFile(imgs.ggxEnvPath, VK_FORMAT_R8G8B8A8_UNORM, vulkanDevice_, queue_));
	SAFE_ASSERT(imageGGXLut.loadFromFile(imgs.ggxLutPath, VK_FORMAT_R8G8B8A8_UNORM, vulkanDevice_, queue_, VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, false, lutOpt));
	SAFE_ASSERT(imageLambertEnv.loadFromFile(imgs.lambertEnvPath, VK_FORMAT_R8G8B8A8_UNORM, vulkanDevice_, queue_));
	SAFE_ASSERT(imageCharlieEnv.loadFromFile(imgs.charlieEnvPath, VK_FORMAT_R8G8B8A8_UNORM, vulkanDevice_, queue_));
	SAFE_ASSERT(imageCharlieLut.loadFromFile(imgs.charlieLutPath, VK_FORMAT_R8G8B8A8_UNORM, vulkanDevice_, queue_, VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, false, lutOpt));
	SAFE_ASSERT(imageSheenELut.loadFromFile(imgs.sheenLutPath, VK_FORMAT_R8G8B8A8_UNORM, vulkanDevice_, queue_, VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, false, lutOpt));

	if (transmissionFb)
	{
		vks::Texture2D::SamplerOption samplerOpt;

		// Create sampler
		VkSamplerCreateInfo samplerCreateInfo = {};
		samplerCreateInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
		samplerCreateInfo.magFilter = samplerOpt.magFilter;
		samplerCreateInfo.minFilter = samplerOpt.minFilter;
		samplerCreateInfo.mipmapMode = samplerOpt.mipmapMode;
		samplerCreateInfo.addressModeU = samplerOpt.addressModeU;
		samplerCreateInfo.addressModeV = samplerOpt.addressModeV;
		samplerCreateInfo.addressModeW = samplerOpt.addressModeW;
		samplerCreateInfo.compareEnable = samplerOpt.compareEnable;
		samplerCreateInfo.compareOp = samplerOpt.compareOp;
		if (samplerOpt.anisotropyEnable) {
			samplerCreateInfo.maxAnisotropy = vulkanDevice_->enabledFeatures.samplerAnisotropy ? vulkanDevice_->properties.limits.maxSamplerAnisotropy : 1.0f;
			samplerCreateInfo.anisotropyEnable = vulkanDevice_->enabledFeatures.samplerAnisotropy;
		}
		else {
			samplerCreateInfo.maxAnisotropy = 1.0f;
			samplerCreateInfo.anisotropyEnable = false;
		}
		samplerCreateInfo.mipLodBias = 0.0f;
		samplerCreateInfo.minLod = 0.0f;
		samplerCreateInfo.maxLod = 0.0f;
		VK_CHECK_RESULT(vkCreateSampler(device_, &samplerCreateInfo, nullptr, &transmissionTexture_.sampler));

		// Create image view
		VkImageViewCreateInfo viewCreateInfo = {};
		viewCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		viewCreateInfo.pNext = NULL;
		viewCreateInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
		viewCreateInfo.format = transmissionFb->format;
		viewCreateInfo.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
		viewCreateInfo.subresourceRange.levelCount = 1;
		viewCreateInfo.image = transmissionFb->image;
		VK_CHECK_RESULT(vkCreateImageView(device_, &viewCreateInfo, nullptr, &transmissionTexture_.imageView));

		transmissionTexture_.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	}

	uploadParams2Gpu();
}

void Enviroment::uploadParams2Gpu()
{
	params.u_EnvIntensity = envIntensity;
	params.u_EnvRotation = glm::rotate(glm::mat4(1), glm::radians(environmentRotation * 1.0f), vec3(0,1,0));
	params.u_MipCount = imageGGXEnv.mipLevels;
	params.u_EnvBlurNormalized = environmentBlur ? 0.6 : 0;

	VK_CHECK_RESULT(uniformBuffer.map());
	memcpy(uniformBuffer.mapped, &params, sizeof(params));
	uniformBuffer.unmap();
}
void Enviroment::uploadDescriptorSet2Gpu(std::vector<VkWriteDescriptorSet>& writeDescriptorSet)
{
	writeDescriptorSet.push_back(vks::initializers::writeDescriptorSet(descriptorSet, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, ENVIROMENT_BINDING, &uniformBuffer.descriptor));
	
	auto func_pushDescriptorSet = [&](int binding, VkDescriptorImageInfo* imageInfo) {
		writeDescriptorSet.push_back(vks::initializers::writeDescriptorSet(descriptorSet, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, binding, imageInfo));
	};
	func_pushDescriptorSet(ENV_TEX_GGX_ENV_BIDING, &imageGGXEnv.descriptor);
	func_pushDescriptorSet(ENV_TEX_GGX_LUT_BIDING, &imageGGXLut.descriptor);
	func_pushDescriptorSet(ENV_TEX_LAMBERT_ENV_BIDING, &imageLambertEnv.descriptor);
	func_pushDescriptorSet(ENV_TEX_CHARLIE_ENV_BIDING, &imageCharlieEnv.descriptor);
	func_pushDescriptorSet(ENV_TEX_CHARLIE_LUT_BIDING, &imageCharlieLut.descriptor);
	func_pushDescriptorSet(ENV_TEX_SHEEN_ELUT_BIDING, &imageSheenELut.descriptor);
	if (transmissionTexture_.sampler) func_pushDescriptorSet(ENV_TEX_TRANSMISSION_FRAMEBUFFER_BIDING, &transmissionTexture_);
}

