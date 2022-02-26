/*
 * Vulkan Example - Omni directional shadows using a dynamic cube map
 *
 * Copyright (C) 2016-2022 by Sascha Willems - www.saschawillems.de
 *
 * This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
 */

/*
 * This sample shows how to apply omni-directional shadows to a scene.
 * It renders a 360° view of the scene from a light's point-of-view into a cubemap image (see updateCubeFace).
 * This cube map is then sourced for shadowing in the final scene rendering pass.
 */

#include "vulkanexamplebase.h"
#include "VulkanglTFModel.h"

#define ENABLE_VALIDATION false

class VulkanExample : public VulkanExampleBase
{
public:
	VkExtent2D shadowMapExtent = { 1024, 1024 };
	VkFormat shadowMapFormat{};

	bool displayShadowCubemap = false;
	glm::vec4 lightPos = glm::vec4(0.0f, -2.5f, 0.0f, 1.0);

	// We keep depth range as small as possible for better shadow map precision
	float zNear = 0.1f;
	float zFar = 64.0f;

	vkglTF::Model scene;

	struct UniformData {
		glm::mat4 projection;
		glm::mat4 view;
		glm::mat4 model;
		glm::vec4 lightPos;
	} uniformDataScene, uniformDataShadow;

	struct FrameObjects : public VulkanFrameObjects {
		struct UniformBuffers {
			vks::Buffer shadow;
			vks::Buffer scene;
		} uniformBuffers;
		struct DescriptorSets {
			VkDescriptorSet shadow;
			VkDescriptorSet scene;
		} descriptorSets;
	};
	std::vector<FrameObjects> frameObjects;
	// The descriptor for the shadow cube map is static, and not required to be per-frame
	VkDescriptorSet shadowcubemapDescriptorSet;

	struct Pipelines {
		VkPipeline scene;
		VkPipeline offscreen;
		VkPipeline cubemapDisplay;
	} pipelines;

	struct PipelineLayouts {
		VkPipelineLayout scene;
		VkPipelineLayout offscreen;
	} pipelineLayouts;

	struct DescriptorSetLayouts {
		VkDescriptorSetLayout uniformbuffers;
		VkDescriptorSetLayout shadowcubemap;
	} descriptorSetLayouts;

	// Holds the Vulkan objects for the shadow cubemap's offscreen framebuffer
	struct ShadowCubeMap {
		VkDeviceMemory memory;
		VkImage image;
		VkImageView view;
		VkSampler sampler;
	} shadowCubeMap;

	// Holds the Vulkan objects to store an offscreen framebuffer for rendering the depth map to
	// This will then be sampled as the shadow map during scene rendering
	struct FrameBufferAttachment {
		VkDeviceMemory memory;
		VkImage image;
		VkImageView view;
		void destroy(VkDevice device) {
			vkDestroyImage(device, image, nullptr);
			vkDestroyImageView(device, view, nullptr);
			vkFreeMemory(device, memory, nullptr);
		}
	};
	struct ShadowPass {
		VkFramebuffer frameBuffer;
		FrameBufferAttachment color, depth;
		VkRenderPass renderPass;
	} shadowPass;

	VulkanExample() : VulkanExampleBase(ENABLE_VALIDATION)
	{
		title = "Point light shadows (cubemap)";
		settings.overlay = true;
		camera.setType(Camera::CameraType::lookat);
		camera.setPerspective(45.0f, (float)width / (float)height, zNear, zFar);
		camera.setRotation(glm::vec3(-20.5f, -673.0f, 0.0f));
		camera.setPosition(glm::vec3(0.0f, 0.5f, -15.0f));
		timerSpeed *= 0.5f;
	}

	~VulkanExample()
	{
		if (device) {
			// Cube map
			vkDestroyImageView(device, shadowCubeMap.view, nullptr);
			vkDestroyImage(device, shadowCubeMap.image, nullptr);
			vkDestroySampler(device, shadowCubeMap.sampler, nullptr);
			vkDestroyFramebuffer(device, shadowPass.frameBuffer, nullptr);
			vkDestroyRenderPass(device, shadowPass.renderPass, nullptr);
			vkFreeMemory(device, shadowCubeMap.memory, nullptr);
			shadowPass.color.destroy(device);
			shadowPass.depth.destroy(device);
			// Pipelines
			vkDestroyPipeline(device, pipelines.scene, nullptr);
			vkDestroyPipeline(device, pipelines.offscreen, nullptr);
			vkDestroyPipeline(device, pipelines.cubemapDisplay, nullptr);
			vkDestroyPipelineLayout(device, pipelineLayouts.scene, nullptr);
			vkDestroyPipelineLayout(device, pipelineLayouts.offscreen, nullptr);
			vkDestroyDescriptorSetLayout(device, descriptorSetLayouts.uniformbuffers, nullptr);
			vkDestroyDescriptorSetLayout(device, descriptorSetLayouts.shadowcubemap, nullptr);
			for (FrameObjects& frame : frameObjects) {
				frame.uniformBuffers.scene.destroy();
				frame.uniformBuffers.shadow.destroy();
				destroyBaseFrameObjects(frame);
			}
		}
	}

	void prepareCubeMap()
	{
		// Create cube map image
		VkImageCreateInfo imageCreateInfo = vks::initializers::imageCreateInfo();
		imageCreateInfo.imageType = VK_IMAGE_TYPE_2D;
		imageCreateInfo.format = VK_FORMAT_R32_SFLOAT;
		imageCreateInfo.extent.width = shadowMapExtent.width;
		imageCreateInfo.extent.height = shadowMapExtent.height;
		imageCreateInfo.extent.depth = 1;
		imageCreateInfo.mipLevels = 1;
		imageCreateInfo.arrayLayers = 6;
		imageCreateInfo.samples = VK_SAMPLE_COUNT_1_BIT;
		imageCreateInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
		imageCreateInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
		imageCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		imageCreateInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		imageCreateInfo.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
		VK_CHECK_RESULT(vkCreateImage(device, &imageCreateInfo, nullptr, &shadowCubeMap.image));

		VkMemoryAllocateInfo memAllocInfo = vks::initializers::memoryAllocateInfo();
		VkMemoryRequirements memReqs;

		vkGetImageMemoryRequirements(device, shadowCubeMap.image, &memReqs);
		memAllocInfo.allocationSize = memReqs.size;
		memAllocInfo.memoryTypeIndex = vulkanDevice->getMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
		VK_CHECK_RESULT(vkAllocateMemory(device, &memAllocInfo, nullptr, &shadowCubeMap.memory));
		VK_CHECK_RESULT(vkBindImageMemory(device, shadowCubeMap.image, shadowCubeMap.memory, 0));

		// Image barrier for optimal image (target)
		VkCommandBuffer layoutCmd = vulkanDevice->createCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);
		VkImageSubresourceRange subresourceRange = {};
		subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		subresourceRange.baseMipLevel = 0;
		subresourceRange.levelCount = 1;
		subresourceRange.layerCount = 6;
		vks::tools::setImageLayout(layoutCmd, shadowCubeMap.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, subresourceRange);
		vulkanDevice->flushCommandBuffer(layoutCmd, queue, true);

		// Create image view
		VkImageViewCreateInfo view = vks::initializers::imageViewCreateInfo();
		view.image = VK_NULL_HANDLE;
		view.viewType = VK_IMAGE_VIEW_TYPE_CUBE;
		view.format = VK_FORMAT_R32_SFLOAT;
		view.components = { VK_COMPONENT_SWIZZLE_R };
		view.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
		view.subresourceRange.layerCount = 6;
		view.image = shadowCubeMap.image;
		VK_CHECK_RESULT(vkCreateImageView(device, &view, nullptr, &shadowCubeMap.view));
	}

	// Create all Vulkan objects for the shadow map generation pass
	// This includes the depth image (that's also sampled in the scene rendering pass) and a separate render pass
	void createSadowCubemapObjects()
	{
		std::array<VkAttachmentDescription,2> attachmentDescription{};

		attachmentDescription[0].format = VK_FORMAT_R32_SFLOAT;
		attachmentDescription[0].samples = VK_SAMPLE_COUNT_1_BIT;
		attachmentDescription[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		attachmentDescription[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		attachmentDescription[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		attachmentDescription[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		attachmentDescription[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		attachmentDescription[0].finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

		// Depth attachment
		attachmentDescription[1].format = shadowMapFormat;
		attachmentDescription[1].samples = VK_SAMPLE_COUNT_1_BIT;
		attachmentDescription[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		attachmentDescription[1].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		attachmentDescription[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		attachmentDescription[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		attachmentDescription[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		attachmentDescription[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

		VkAttachmentReference colorReference = {};
		colorReference.attachment = 0;
		colorReference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

		VkAttachmentReference depthReference = {};
		depthReference.attachment = 1;
		depthReference.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

		VkSubpassDescription subpass = {};
		subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
		subpass.colorAttachmentCount = 1;
		subpass.pColorAttachments = &colorReference;
		subpass.pDepthStencilAttachment = &depthReference;

		VkRenderPassCreateInfo renderPassCreateInfo = vks::initializers::renderPassCreateInfo();
		renderPassCreateInfo.attachmentCount = static_cast<uint32_t>(attachmentDescription.size());
		renderPassCreateInfo.pAttachments = attachmentDescription.data();
		renderPassCreateInfo.subpassCount = 1;
		renderPassCreateInfo.pSubpasses = &subpass;

		VK_CHECK_RESULT(vkCreateRenderPass(device, &renderPassCreateInfo, nullptr, &shadowPass.renderPass));

		// Create the per-frame offscreen framebuffers for rendering the depth information from the light's point-of-view to
		// This will later on be used to sample from in the fragment shader of the shadowing pass

		// Color attachment using a single 32 bit float channel to store world positions
		VkImageCreateInfo imageCreateInfo = vks::initializers::imageCreateInfo();
		imageCreateInfo.imageType = VK_IMAGE_TYPE_2D;
		imageCreateInfo.format = VK_FORMAT_R32_SFLOAT;
		imageCreateInfo.extent.width = shadowMapExtent.width;
		imageCreateInfo.extent.height = shadowMapExtent.height;
		imageCreateInfo.extent.depth = 1;
		imageCreateInfo.mipLevels = 1;
		imageCreateInfo.arrayLayers = 1;
		imageCreateInfo.samples = VK_SAMPLE_COUNT_1_BIT;
		imageCreateInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
		// Image of the framebuffer is blit source
		imageCreateInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		imageCreateInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
		imageCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		VK_CHECK_RESULT(vkCreateImage(device, &imageCreateInfo, nullptr, &shadowPass.color.image));

		VkMemoryAllocateInfo memAlloc = vks::initializers::memoryAllocateInfo();

		VkMemoryRequirements memReqs;

		vkGetImageMemoryRequirements(device, shadowPass.color.image, &memReqs);
		memAlloc.allocationSize = memReqs.size;
		memAlloc.memoryTypeIndex = vulkanDevice->getMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
		VK_CHECK_RESULT(vkAllocateMemory(device, &memAlloc, nullptr, &shadowPass.color.memory));
		VK_CHECK_RESULT(vkBindImageMemory(device, shadowPass.color.image, shadowPass.color.memory, 0));

		VkImageViewCreateInfo colorImageView = vks::initializers::imageViewCreateInfo();
		colorImageView.viewType = VK_IMAGE_VIEW_TYPE_2D;
		colorImageView.format = VK_FORMAT_R32_SFLOAT;
		colorImageView.flags = 0;
		colorImageView.subresourceRange = {};
		colorImageView.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		colorImageView.subresourceRange.baseMipLevel = 0;
		colorImageView.subresourceRange.levelCount = 1;
		colorImageView.subresourceRange.baseArrayLayer = 0;
		colorImageView.subresourceRange.layerCount = 1;
		colorImageView.image = shadowPass.color.image;
		VK_CHECK_RESULT(vkCreateImageView(device, &colorImageView, nullptr, &shadowPass.color.view));

		// Create a depth image that can be sampled in a shader as we only need depth information for shadow mapping
		VkImageCreateInfo image = vks::initializers::imageCreateInfo();
		image.imageType = VK_IMAGE_TYPE_2D;
		image.extent.width = shadowMapExtent.width;
		image.extent.height = shadowMapExtent.height;
		image.extent.depth = 1;
		image.mipLevels = 1;
		image.arrayLayers = 1;
		image.samples = VK_SAMPLE_COUNT_1_BIT;
		image.tiling = VK_IMAGE_TILING_OPTIMAL;
		image.format = shadowMapFormat;
		image.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
		VK_CHECK_RESULT(vkCreateImage(device, &image, nullptr, &shadowPass.depth.image));

		// @todo: rename
		memAlloc = vks::initializers::memoryAllocateInfo();
		vkGetImageMemoryRequirements(device, shadowPass.depth.image, &memReqs);
		memAlloc.allocationSize = memReqs.size;
		memAlloc.memoryTypeIndex = vulkanDevice->getMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
		VK_CHECK_RESULT(vkAllocateMemory(device, &memAlloc, nullptr, &shadowPass.depth.memory));
		VK_CHECK_RESULT(vkBindImageMemory(device, shadowPass.depth.image, shadowPass.depth.memory, 0));

		// Create the image View
		VkImageViewCreateInfo depthStencilView = vks::initializers::imageViewCreateInfo();
		depthStencilView.viewType = VK_IMAGE_VIEW_TYPE_2D;
		depthStencilView.format = shadowMapFormat;
		depthStencilView.subresourceRange = {};
		depthStencilView.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
		depthStencilView.subresourceRange.baseMipLevel = 0;
		depthStencilView.subresourceRange.levelCount = 1;
		depthStencilView.subresourceRange.baseArrayLayer = 0;
		depthStencilView.subresourceRange.layerCount = 1;
		depthStencilView.image = shadowPass.depth.image;
		VK_CHECK_RESULT(vkCreateImageView(device, &depthStencilView, nullptr, &shadowPass.depth.view));

		// Create the frame buffer

		VkImageView attachments[2];
		attachments[0] = shadowPass.color.view;
		attachments[1] = shadowPass.depth.view;

		VkFramebufferCreateInfo fbufCreateInfo = vks::initializers::framebufferCreateInfo();
		fbufCreateInfo.renderPass = shadowPass.renderPass;
		fbufCreateInfo.attachmentCount = 2;
		fbufCreateInfo.pAttachments = attachments;
		fbufCreateInfo.width = shadowMapExtent.width;
		fbufCreateInfo.height = shadowMapExtent.height;
		fbufCreateInfo.layers = 1;
		VK_CHECK_RESULT(vkCreateFramebuffer(device, &fbufCreateInfo, nullptr, &shadowPass.frameBuffer));
	
		// Create the sampler used to sample from the depth attachment in the scene rendering pass
		// Check if the current implementation supports linear filtering for the desired shadow map format
		VkFilter shadowmapFilter = vks::tools::formatIsFilterable(physicalDevice, shadowMapFormat, VK_IMAGE_TILING_OPTIMAL) ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
		VkSamplerCreateInfo sampler = vks::initializers::samplerCreateInfo();
		sampler.magFilter = shadowmapFilter;
		sampler.minFilter = shadowmapFilter;
		sampler.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
		sampler.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		sampler.addressModeV = sampler.addressModeU;
		sampler.addressModeW = sampler.addressModeU;
		sampler.mipLodBias = 0.0f;
		sampler.maxAnisotropy = 1.0f;
		sampler.minLod = 0.0f;
		sampler.maxLod = 1.0f;
		sampler.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
		VK_CHECK_RESULT(vkCreateSampler(device, &sampler, nullptr, &shadowCubeMap.sampler));
	}

	void loadAssets()
	{
		const uint32_t glTFLoadingFlags = vkglTF::FileLoadingFlags::PreTransformVertices | vkglTF::FileLoadingFlags::PreMultiplyVertexColors | vkglTF::FileLoadingFlags::FlipY;
		scene.loadFromFile(getAssetPath() + "models/shadowscene_fire.gltf", vulkanDevice, queue, glTFLoadingFlags);
	}

	void createDescriptors()
	{
		// Pool
		// @todo
		std::vector<VkDescriptorPoolSize> poolSizes = {
			vks::initializers::descriptorPoolSize(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 3 * 100),
			vks::initializers::descriptorPoolSize(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2 * 100)
		};
		VkDescriptorPoolCreateInfo descriptorPoolInfo = vks::initializers::descriptorPoolCreateInfo(poolSizes, 3 * 100);
		VK_CHECK_RESULT(vkCreateDescriptorPool(device, &descriptorPoolInfo, nullptr, &descriptorPool));
		
		// Layouts
		VkDescriptorSetLayoutCreateInfo descriptorSetLayoutCI{};
		VkDescriptorSetLayoutBinding setLayoutBinding{};

		// Layout for the per-frame uniform buffers
		setLayoutBinding = vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT, 0);
		descriptorSetLayoutCI = vks::initializers::descriptorSetLayoutCreateInfo(setLayoutBinding);
		VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &descriptorSetLayoutCI, nullptr, &descriptorSetLayouts.uniformbuffers));

		// Layout for the shadow map image
		setLayoutBinding = vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 0);
		descriptorSetLayoutCI = vks::initializers::descriptorSetLayoutCreateInfo(setLayoutBinding);
		VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &descriptorSetLayoutCI, nullptr, &descriptorSetLayouts.shadowcubemap));

		// Sets
		// Per-frame for dynamic uniform buffers
		for (FrameObjects& frame : frameObjects) {
			VkWriteDescriptorSet writeDescriptorSet{};
			VkDescriptorSetAllocateInfo allocInfo{};

			// Offscreen depth cube map generation
			allocInfo = vks::initializers::descriptorSetAllocateInfo(descriptorPool, &descriptorSetLayouts.uniformbuffers, 1);
			VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &allocInfo, &frame.descriptorSets.shadow));
			writeDescriptorSet = vks::initializers::writeDescriptorSet(frame.descriptorSets.shadow, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 0, &frame.uniformBuffers.shadow.descriptor);
			vkUpdateDescriptorSets(device, 1, &writeDescriptorSet, 0, nullptr);

			// Scene rendering with shadow cube map applied
			VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &allocInfo, &frame.descriptorSets.scene));
			writeDescriptorSet = vks::initializers::writeDescriptorSet(frame.descriptorSets.scene, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 0, &frame.uniformBuffers.scene.descriptor);
			vkUpdateDescriptorSets(device, 1, &writeDescriptorSet, 0, nullptr);

		}

		// Global set for the shadow cube map image
		VkDescriptorSetAllocateInfo allocInfo = vks::initializers::descriptorSetAllocateInfo(descriptorPool, &descriptorSetLayouts.shadowcubemap, 1);
		VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &allocInfo, &shadowcubemapDescriptorSet));
		VkDescriptorImageInfo shadowCubeMapDescriptor = vks::initializers::descriptorImageInfo(shadowCubeMap.sampler, shadowCubeMap.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
		VkWriteDescriptorSet writeDescriptorSet = vks::initializers::writeDescriptorSet(shadowcubemapDescriptorSet, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 0, &shadowCubeMapDescriptor);
		vkUpdateDescriptorSets(device, 1, &writeDescriptorSet, 0, nullptr);
	}

	void createPipelines()
	{
		// Layouts
		// @todo
		// 3D scene pipeline layout
		std::vector<VkDescriptorSetLayout> setLayouts = { descriptorSetLayouts.uniformbuffers, descriptorSetLayouts.shadowcubemap };
		VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo = vks::initializers::pipelineLayoutCreateInfo(setLayouts.data(), 2);
		VK_CHECK_RESULT(vkCreatePipelineLayout(device, &pipelineLayoutCreateInfo, nullptr, &pipelineLayouts.scene));

		// Offscreen pipeline layout
		pipelineLayoutCreateInfo = vks::initializers::pipelineLayoutCreateInfo(&descriptorSetLayouts.uniformbuffers, 1);
		// Push constants for cube map face view matrices
		VkPushConstantRange pushConstantRange = vks::initializers::pushConstantRange(VK_SHADER_STAGE_VERTEX_BIT, sizeof(glm::mat4), 0);
		// Push constant ranges are part of the pipeline layout
		pipelineLayoutCreateInfo.pushConstantRangeCount = 1;
		pipelineLayoutCreateInfo.pPushConstantRanges = &pushConstantRange;
		VK_CHECK_RESULT(vkCreatePipelineLayout(device, &pipelineLayoutCreateInfo, nullptr, &pipelineLayouts.offscreen));

		// Pipelines
		VkPipelineInputAssemblyStateCreateInfo inputAssemblyState = vks::initializers::pipelineInputAssemblyStateCreateInfo(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, 0, VK_FALSE);
		VkPipelineRasterizationStateCreateInfo rasterizationState = vks::initializers::pipelineRasterizationStateCreateInfo(VK_POLYGON_MODE_FILL, VK_CULL_MODE_BACK_BIT, VK_FRONT_FACE_COUNTER_CLOCKWISE, 0);
		VkPipelineColorBlendAttachmentState blendAttachmentState = vks::initializers::pipelineColorBlendAttachmentState(0xf, VK_FALSE);
		VkPipelineColorBlendStateCreateInfo colorBlendState = vks::initializers::pipelineColorBlendStateCreateInfo(1, &blendAttachmentState);
		VkPipelineDepthStencilStateCreateInfo depthStencilState = vks::initializers::pipelineDepthStencilStateCreateInfo(VK_TRUE, VK_TRUE, VK_COMPARE_OP_LESS_OR_EQUAL);
		VkPipelineViewportStateCreateInfo viewportState = vks::initializers::pipelineViewportStateCreateInfo(1, 1, 0);
		VkPipelineMultisampleStateCreateInfo multisampleState = vks::initializers::pipelineMultisampleStateCreateInfo(VK_SAMPLE_COUNT_1_BIT, 0);
		std::vector<VkDynamicState> dynamicStateEnables = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
		VkPipelineDynamicStateCreateInfo dynamicState = vks::initializers::pipelineDynamicStateCreateInfo(dynamicStateEnables);
		std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages;

		VkGraphicsPipelineCreateInfo pipelineCI = vks::initializers::pipelineCreateInfo(pipelineLayouts.scene, renderPass, 0);
		pipelineCI.pInputAssemblyState = &inputAssemblyState;
		pipelineCI.pRasterizationState = &rasterizationState;
		pipelineCI.pColorBlendState = &colorBlendState;
		pipelineCI.pMultisampleState = &multisampleState;
		pipelineCI.pViewportState = &viewportState;
		pipelineCI.pDepthStencilState = &depthStencilState;
		pipelineCI.pDynamicState = &dynamicState;
		pipelineCI.stageCount = static_cast<uint32_t>(shaderStages.size());
		pipelineCI.pStages = shaderStages.data();
		pipelineCI.pVertexInputState = vkglTF::Vertex::getPipelineVertexInputState({vkglTF::VertexComponent::Position, vkglTF::VertexComponent::Color, vkglTF::VertexComponent::Normal});

		// 3D scene pipelinewe
		shaderStages[0] = loadShader(getShadersPath() + "shadowmappingomni/scene.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
		shaderStages[1] = loadShader(getShadersPath() + "shadowmappingomni/scene.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
		VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCI, nullptr, &pipelines.scene));

		// Offscreen pipeline
		shaderStages[0] = loadShader(getShadersPath() + "shadowmappingomni/offscreen.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
		shaderStages[1] = loadShader(getShadersPath() + "shadowmappingomni/offscreen.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
		pipelineCI.layout = pipelineLayouts.offscreen;
		pipelineCI.renderPass = shadowPass.renderPass;
		VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCI, nullptr, &pipelines.offscreen));

		// Cube map display pipeline
		shaderStages[0] = loadShader(getShadersPath() + "shadowmappingomni/cubemapdisplay.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
		shaderStages[1] = loadShader(getShadersPath() + "shadowmappingomni/cubemapdisplay.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
		VkPipelineVertexInputStateCreateInfo emptyInputState = vks::initializers::pipelineVertexInputStateCreateInfo();
		pipelineCI.pVertexInputState = &emptyInputState;
		pipelineCI.layout = pipelineLayouts.scene;
		pipelineCI.renderPass = renderPass;
		rasterizationState.cullMode = VK_CULL_MODE_NONE;
		VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCI, nullptr, &pipelines.cubemapDisplay));
	}

	void prepare()
	{
		VulkanExampleBase::prepare();
		// Prepare per-frame ressources
		frameObjects.resize(getFrameCount());
		for (FrameObjects& frame : frameObjects) {
			createBaseFrameObjects(frame);
			// Uniform buffers
			VK_CHECK_RESULT(vulkanDevice->createAndMapBuffer(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &frame.uniformBuffers.scene, sizeof(UniformData)));
			VK_CHECK_RESULT(vulkanDevice->createAndMapBuffer(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &frame.uniformBuffers.shadow, sizeof(UniformData)));
		}

		// Get a supported depth format that we can use for the shadow maps @todo
		VkBool32 validDepthFormat = vks::tools::getSupportedDepthFormat(physicalDevice, &shadowMapFormat);
		assert(validDepthFormat);

		loadAssets();
		createSadowCubemapObjects();
		prepareCubeMap();
		createDescriptors();
		createPipelines();
		prepared = true;
	}

	// Updates a single cube map face
	// Renders the scene with face's view and does a copy from framebuffer to cube face
	// Uses push constants for quick update of view matrix for the current cube map face
	void updateCubeFace(uint32_t faceIndex, FrameObjects currentFrame)
	{
		VkClearValue clearValues[2];
		clearValues[0].color = { { 0.0f, 0.0f, 0.0f, 1.0f } };
		clearValues[1].depthStencil = { 1.0f, 0 };

		VkRenderPassBeginInfo renderPassBeginInfo = vks::initializers::renderPassBeginInfo();
		// Reuse render pass from example pass
		renderPassBeginInfo.renderPass = shadowPass.renderPass;
		renderPassBeginInfo.framebuffer = shadowPass.frameBuffer;
		renderPassBeginInfo.renderArea.extent.width = shadowMapExtent.width;
		renderPassBeginInfo.renderArea.extent.height = shadowMapExtent.height;
		renderPassBeginInfo.clearValueCount = 2;
		renderPassBeginInfo.pClearValues = clearValues;

		// Update view matrix via push constant

		glm::mat4 viewMatrix = glm::mat4(1.0f);
		switch (faceIndex)
		{
		case 0: // POSITIVE_X
			viewMatrix = glm::rotate(viewMatrix, glm::radians(90.0f), glm::vec3(0.0f, 1.0f, 0.0f));
			viewMatrix = glm::rotate(viewMatrix, glm::radians(180.0f), glm::vec3(1.0f, 0.0f, 0.0f));
			break;
		case 1:	// NEGATIVE_X
			viewMatrix = glm::rotate(viewMatrix, glm::radians(-90.0f), glm::vec3(0.0f, 1.0f, 0.0f));
			viewMatrix = glm::rotate(viewMatrix, glm::radians(180.0f), glm::vec3(1.0f, 0.0f, 0.0f));
			break;
		case 2:	// POSITIVE_Y
			viewMatrix = glm::rotate(viewMatrix, glm::radians(-90.0f), glm::vec3(1.0f, 0.0f, 0.0f));
			break;
		case 3:	// NEGATIVE_Y
			viewMatrix = glm::rotate(viewMatrix, glm::radians(90.0f), glm::vec3(1.0f, 0.0f, 0.0f));
			break;
		case 4:	// POSITIVE_Z
			viewMatrix = glm::rotate(viewMatrix, glm::radians(180.0f), glm::vec3(1.0f, 0.0f, 0.0f));
			break;
		case 5:	// NEGATIVE_Z
			viewMatrix = glm::rotate(viewMatrix, glm::radians(180.0f), glm::vec3(0.0f, 0.0f, 1.0f));
			break;
		}

		// Render scene from cube face's point of view
		vkCmdBeginRenderPass(currentFrame.commandBuffer, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

		// Update shader push constant block
		// Contains current face view matrix
		vkCmdPushConstants(
			currentFrame.commandBuffer,
			pipelineLayouts.offscreen,
			VK_SHADER_STAGE_VERTEX_BIT,
			0,
			sizeof(glm::mat4),
			&viewMatrix);

		vkCmdBindPipeline(currentFrame.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.offscreen);
		vkCmdBindDescriptorSets(currentFrame.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayouts.offscreen, 0, 1, &currentFrame.descriptorSets.shadow, 0, nullptr);
		scene.draw(currentFrame.commandBuffer);

		vkCmdEndRenderPass(currentFrame.commandBuffer);

		// @todo: sub pass dependencies

		// Make sure color writes to the framebuffer are finished before using it as transfer source
		vks::tools::setImageLayout(
			currentFrame.commandBuffer,
			shadowPass.color.image,
			VK_IMAGE_ASPECT_COLOR_BIT,
			VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
			VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

		VkImageSubresourceRange cubeFaceSubresourceRange = {};
		cubeFaceSubresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		cubeFaceSubresourceRange.baseMipLevel = 0;
		cubeFaceSubresourceRange.levelCount = 1;
		cubeFaceSubresourceRange.baseArrayLayer = faceIndex;
		cubeFaceSubresourceRange.layerCount = 1;

		// Change image layout of one cubemap face to transfer destination
		vks::tools::setImageLayout(
			currentFrame.commandBuffer,
			shadowCubeMap.image,
			VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			cubeFaceSubresourceRange);

		// Copy region for transfer from framebuffer to cube face
		VkImageCopy copyRegion = {};

		copyRegion.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		copyRegion.srcSubresource.baseArrayLayer = 0;
		copyRegion.srcSubresource.mipLevel = 0;
		copyRegion.srcSubresource.layerCount = 1;
		copyRegion.srcOffset = { 0, 0, 0 };

		copyRegion.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		copyRegion.dstSubresource.baseArrayLayer = faceIndex;
		copyRegion.dstSubresource.mipLevel = 0;
		copyRegion.dstSubresource.layerCount = 1;
		copyRegion.dstOffset = { 0, 0, 0 };

		copyRegion.extent.width = shadowMapExtent.width;
		copyRegion.extent.height = shadowMapExtent.height;
		copyRegion.extent.depth = 1;

		// Put image copy into command buffer
		vkCmdCopyImage(
			currentFrame.commandBuffer,
			shadowPass.color.image,
			VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			shadowCubeMap.image,
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			1,
			&copyRegion);

		// Transform framebuffer color attachment back
		vks::tools::setImageLayout(
			currentFrame.commandBuffer,
			shadowPass.color.image,
			VK_IMAGE_ASPECT_COLOR_BIT,
			VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

		// Change image layout of copied face to shader read
		vks::tools::setImageLayout(
			currentFrame.commandBuffer,
			shadowCubeMap.image,
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			cubeFaceSubresourceRange);
	}

	virtual void render()
	{
		FrameObjects currentFrame = frameObjects[getCurrentFrameIndex()];

		VulkanExampleBase::prepareFrame(currentFrame);

		// Update uniform-buffers for the next frame
		if (!paused) {
			lightPos.x = sin(glm::radians(timer * 360.0f)) * 0.15f;
			lightPos.z = cos(glm::radians(timer * 360.0f)) * 0.15f;
		}

		// Uniform data used for rendering the final scene
		uniformDataScene.projection = camera.matrices.perspective;
		uniformDataScene.view       = camera.matrices.view;
		uniformDataScene.model      = glm::mat4(1.0f);
		uniformDataScene.lightPos   = lightPos;
		memcpy(currentFrame.uniformBuffers.scene.mapped, &uniformDataScene, sizeof(uniformDataScene));

		// Uniform data for shadow cube map generation
		uniformDataShadow.projection = glm::perspective((float)(M_PI / 2.0), 1.0f, zNear, zFar);
		uniformDataShadow.view       = glm::mat4(1.0f);
		uniformDataShadow.model      = glm::translate(glm::mat4(1.0f), glm::vec3(-lightPos.x, -lightPos.y, -lightPos.z));
		uniformDataShadow.lightPos   = lightPos;
		memcpy(currentFrame.uniformBuffers.shadow.mapped, &uniformDataShadow, sizeof(uniformDataShadow));

		// Build the command buffer
		const VkCommandBuffer commandBuffer = currentFrame.commandBuffer;
		const VkCommandBufferBeginInfo commandBufferBeginInfo = getCommandBufferBeginInfo();
		VK_CHECK_RESULT(vkBeginCommandBuffer(commandBuffer, &commandBufferBeginInfo));

		VkViewport viewport{};
		VkRect2D renderArea{};

		// First set of render passes: Generate shadow cube maps using one render pass per face
		viewport = vks::initializers::viewport((float)shadowMapExtent.width, (float)shadowMapExtent.height, 0.0f, 1.0f);
		renderArea = vks::initializers::rect2D(shadowMapExtent.width, shadowMapExtent.height, 0, 0);
		vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
		vkCmdSetScissor(commandBuffer, 0, 1, &renderArea);
		for (uint32_t face = 0; face < 6; face++) {
			updateCubeFace(face, currentFrame);
		}

		// Final render pass: Scene rendering with applied shadow map
		// Note: Explicit synchronization is not required between the render pass, as this is done implicit via sub pass dependencies
		const VkRenderPassBeginInfo renderPassBeginInfo = getRenderPassBeginInfo(renderPass, defaultClearValues);
		vkCmdBeginRenderPass(commandBuffer, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

		viewport = getViewport();
		renderArea = getRenderArea();
		vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
		vkCmdSetScissor(commandBuffer, 0, 1, &renderArea);

		vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayouts.scene, 0, 1, &currentFrame.descriptorSets.scene, 0, nullptr);
		vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayouts.scene, 1, 1, &shadowcubemapDescriptorSet, 0, nullptr);

		if (displayShadowCubemap) {
			// Display all six sides of the shadow cube map
			// Note: Visualization of the different faces is done in the fragment shader, see cubemapdisplay.frag
			vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.cubemapDisplay);
			vkCmdDraw(commandBuffer, 6, 1, 0, 0);
		} else {
			vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.scene);
			scene.draw(commandBuffer);
		}

		drawUI(commandBuffer);
		vkCmdEndRenderPass(commandBuffer);

		VK_CHECK_RESULT(vkEndCommandBuffer(commandBuffer));

		VulkanExampleBase::submitFrame(currentFrame);
	}

	virtual void OnUpdateUIOverlay(vks::UIOverlay *overlay)
	{
		if (overlay->header("Settings")) {
			overlay->checkBox("Display shadow cubemap render target", &displayShadowCubemap);
		}
	}
};

VULKAN_EXAMPLE_MAIN()
