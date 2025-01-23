#include "Light.h"
#include "GltfReadUtils.h"

static void resetLightUniform(Light& lightParam)
{
	lightParam.direction = vec3(0, 0, 1);
	lightParam.color = vec3(1, 1, 1);
	lightParam.intensity = 1;
	lightParam.range = -1;
	lightParam.type = LightType_Directional;
	lightParam.innerConeCos = cos(0);
	lightParam.outerConeCos = cos(M_PI / 4);
}
Light1::Light1(Light* p)
{
	param = p;
	resetLightUniform(*param);
}

/**
 * LightManager
 */
LightManager::LightManager(vks::VulkanDevice* vulkanDevice, VkDescriptorPool descriptorPool)
{
	vulkanDevice_ = vulkanDevice;
	device_ = vulkanDevice_->logicalDevice;
	descriptorPool_ = descriptorPool;

	params.u_LightCount = 0;
	{
		std::vector<VkDescriptorSetLayoutBinding> setLayoutBinding_Light;
		setLayoutBinding_Light.push_back(vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_FRAGMENT_BIT, LIGHT_BINDING));
		VkDescriptorSetLayoutCreateInfo descriptorSetLayoutCI_Params = vks::initializers::descriptorSetLayoutCreateInfo(setLayoutBinding_Light.data(), setLayoutBinding_Light.size());
		VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device_, &descriptorSetLayoutCI_Params, nullptr, &descriptorSetLayout));

		VkDescriptorSetAllocateInfo allocInfo = vks::initializers::descriptorSetAllocateInfo(descriptorPool_, &descriptorSetLayout, 1);
		VK_CHECK_RESULT(vkAllocateDescriptorSets(device_, &allocInfo, &descriptorSet));

		VK_CHECK_RESULT(vulkanDevice->createBuffer(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &uniformBuffer, sizeof(params)));
	}
}
LightManager::~LightManager()
{
	destroy();
}
void LightManager::destroy()
{
	//vkSafeFreeDescriptorSets(device_, descriptorPool_, 1, descriptorSet);
	vkSafeDestroyDescriptorSetLayout(device_, descriptorSetLayout);
	uniformBuffer.destroy();
}
void LightManager::reset()
{
	removeAllLights();
}

void LightManager::load(tinygltf::Model& gltfMdl)
{
	reset();
	for (int i = 0; i < std::min((int)gltfMdl.lights.size(), LIGHT_COUNT); ++i) 
	{
		createLight();

		const auto& srcLgt = gltfMdl.lights[i];
		auto& dstLgt = params.u_Lights[i];
		dstLgt.color = toVec3(srcLgt.color);

		dstLgt.range = srcLgt.range;
		dstLgt.intensity = srcLgt.intensity;
		dstLgt.innerConeCos = cos(srcLgt.spot.innerConeAngle);
		dstLgt.outerConeCos = cos(srcLgt.spot.outerConeAngle);
		dstLgt.type = 0;
		if (srcLgt.type == "directional") dstLgt.type = LightType_Directional;
		else if (srcLgt.type == "point") dstLgt.type = LightType_Point;
		else if (srcLgt.type == "spot") dstLgt.type = LightType_Spot;

		auto iter = std::find_if(gltfMdl.nodes.begin(), gltfMdl.nodes.end(), [&srcLgt](const tinygltf::Node& node) {
			return node.name == srcLgt.name;
		});
		if (iter != gltfMdl.nodes.end()) {
			auto& node = *iter;
			glm::quat rot = toQuat(node.rotation);
			dstLgt.direction = toVec4(rot * glm::vec3(0,0,-1));
			dstLgt.position = toVec3(node.translation);
		}
	}
	if (params.u_LightCount == 0)
	{
		createDefaultLights();
	}

	uploadParams2Gpu();
}
void LightManager::createDefaultLights()
{
	removeAllLights();

	params.u_LightCount = 2;
	auto& lightKey = params.u_Lights[0];
	auto& lightFill = params.u_Lights[1];
	resetLightUniform(lightKey);
	resetLightUniform(lightFill);

	lightKey.type = LightType_Directional;
	lightKey.direction = toVec4(glm::quat(0.8535534, -0.3535534, -0.353553385, -0.146446586) * glm::vec3(0, 0, -1));

	lightFill.intensity = 0.5;
	lightFill.type = LightType_Directional;
	lightFill.direction = toVec4(glm::quat(-0.353553444, -0.8535534, 0.146446645, -0.353553325) * glm::vec3(0, 0, -1));
}

void LightManager::removeAllLights()
{
	lights_.clear();
	params.u_LightCount = 0;
}
LightPtr LightManager::createLight(int lightType /*= LightType_Directional*/)
{
	if (params.u_LightCount >= LIGHT_COUNT) return nullptr;

	LightPtr light = std::make_shared<Light1>(&params.u_Lights[params.u_LightCount]);
	lights_.push_back(light);
	params.u_LightCount++;
	return light;
}

void LightManager::uploadParams2Gpu()
{
	VK_CHECK_RESULT(uniformBuffer.map());
	memcpy(uniformBuffer.mapped, &params, sizeof(params));
	uniformBuffer.unmap();
}
void LightManager::uploadDescriptorSet2Gpu(std::vector<VkWriteDescriptorSet>& writeDescriptorSet)
{
	writeDescriptorSet.push_back(vks::initializers::writeDescriptorSet(descriptorSet, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, LIGHT_BINDING, &uniformBuffer.descriptor));
}


