/*
 * Vulkan Example - Cascaded shadow mapping (CSM) for directional light sources
 *
 * Copyright (C) 2016-2022 by Sascha Willems - www.saschawillems.de
 *
 * This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
 */

/*
 * This example implements projective cascaded shadow mapping. This technique splits up the camera frustum into
 * multiple frustums with each getting its own full-res shadow map, implemented as a layered depth-only image.
 * The shader then selects the proper shadow map layer depending on what split of the frustum the depth value
 * to compare fits into. This calculation is done in s.
 *
 * This results in a better shadow map resolution distribution that can be tweaked even further by increasing
 * the number of frustum splits.
 *
 * A further optimization could be done using a geometry shader to do a single-pass render for the depth map
 * cascades instead of multiple passes (geometry shaders are not supported on all target devices).
 */

#include "vulkanexamplebase.h"
#include "VulkanglTFModel.h"

#define ENABLE_VALIDATION false

class VulkanExample : public VulkanExampleBase
{
public:
	// Use a smaller size for the shadow map on mobile devices
#if defined(__ANDROID__)
	VkExtent2D shadowMapExtent = { 2048, 2048 };
#else
	VkExtent2D shadowMapExtent = { 4096, 4096 };
#endif
	static constexpr size_t shadowMapCascadeCount = 4;

	bool displayDepthMap = false;
	int32_t displayDepthMapCascadeIndex = 0;
	bool colorCascades = false;
	bool filterPCF = false;

	float cascadeSplitLambda = 0.95f;

	float zNear = 0.5f;
	float zFar = 48.0f;

	glm::vec4 lightPos = glm::vec4();

	struct Models {
		vkglTF::Model terrain;
		vkglTF::Model tree;
	} models;

	struct UniformData {
		glm::mat4 projection;
		glm::mat4 view;
		glm::mat4 model;
		glm::vec4 lightDir;
		// CSM related uniforms
		float cascadeSplits[4];
		glm::mat4 cascadeViewProjMat[4];
		glm::mat4 inverseViewMat;
		// Options
		int32_t colorCascades;
	} uniformData;

	struct FrameObjects : public VulkanFrameObjects {
		vks::Buffer uniformBuffer;
		VkDescriptorSet descriptorSet;
	};
	std::vector<FrameObjects> frameObjects;
	// The descriptor for the shadow cascade image is static, and not required to be per-frame
	VkDescriptorSet cascadesDescriptorSet;

	struct PipelineLayouts {
		VkPipelineLayout shadowmapGeneration;
		VkPipelineLayout sceneRendering;
	} pipelineLayouts;

	struct Pipelines {
		VkPipeline debugShadowMap;
		VkPipeline sceneShadow;
		VkPipeline sceneShadowPCF;
		VkPipeline cascadeGeneration;
	} pipelines;

	struct DescriptorSetLayouts {
		VkDescriptorSetLayout uniformbuffers;
		VkDescriptorSetLayout images;
	} descriptorSetLayouts;

	// We use push constants to pass certain values to the shaders
	struct PushConstBlock {
		glm::vec4 position;
		uint32_t cascadeIndex;
	};

	// Layered depth image containing all shadow cascades
	struct DepthImage {
		VkImage image;
		VkDeviceMemory mem;
		VkImageView view;
		VkSampler sampler;
		VkRenderPass renderPass;
		void destroy(VkDevice device) {
			vkDestroyImageView(device, view, nullptr);
			vkDestroyImage(device, image, nullptr);
			vkFreeMemory(device, mem, nullptr);
			vkDestroySampler(device, sampler, nullptr);
		}
	} depth;

	// Contains all resources for a single shadow map cascade
	struct Cascade {
		VkFramebuffer frameBuffer;
		VkDescriptorSet descriptorSet;
		VkImageView view;
		float splitDepth;
		glm::mat4 viewProjMatrix;
		void destroy(VkDevice device) {
			vkDestroyImageView(device, view, nullptr);
			vkDestroyFramebuffer(device, frameBuffer, nullptr);
		}
	};
	std::array<Cascade, shadowMapCascadeCount> cascades;

	VulkanExample() : VulkanExampleBase(ENABLE_VALIDATION)
	{
		title = "Cascaded shadow mapping";
		camera.setType(Camera::CameraType::firstperson);
		camera.setMovementSpeed(2.5f);
		camera.setPerspective(45.0f, (float)width / (float)height, zNear, zFar);
		camera.setPosition(glm::vec3(-0.12f, 1.14f, -2.25f));
		camera.setRotation(glm::vec3(-17.0f, 7.0f, 0.0f));
		settings.overlay = true;
		timerSpeed *= 0.025f;
		timer = 0.2f;
	}

	~VulkanExample()
	{
		if (device) {
			for (auto cascade : cascades) {
				cascade.destroy(device);
			}
			depth.destroy(device);
			vkDestroyRenderPass(device, depth.renderPass, nullptr);
			vkDestroyPipeline(device, pipelines.debugShadowMap, nullptr);
			vkDestroyPipeline(device, pipelines.cascadeGeneration, nullptr);
			vkDestroyPipeline(device, pipelines.sceneShadow, nullptr);
			vkDestroyPipeline(device, pipelines.sceneShadowPCF, nullptr);
			vkDestroyPipelineLayout(device, pipelineLayouts.sceneRendering, nullptr);
			vkDestroyPipelineLayout(device, pipelineLayouts.shadowmapGeneration, nullptr);
			vkDestroyDescriptorSetLayout(device, descriptorSetLayouts.images, nullptr);
			vkDestroyDescriptorSetLayout(device, descriptorSetLayouts.uniformbuffers, nullptr);
			for (FrameObjects& frame : frameObjects) {
				frame.uniformBuffer.destroy();
				destroyBaseFrameObjects(frame);
			}
		}
	}

	virtual void getEnabledFeatures()
	{
		enabledFeatures.samplerAnisotropy = deviceFeatures.samplerAnisotropy;
		// Depth clamp to avoid near plane clipping
		enabledFeatures.depthClamp = deviceFeatures.depthClamp;
	}

	//Render the example scene with given command buffer, pipeline layout and descriptor set
	//Used by the scene rendering and depth pass generation command buffer
	void renderScene(VkCommandBuffer commandBuffer, VkPipelineLayout pipelineLayout, uint32_t cascadeIndex = 0) {
		// We use push constants for passing shadow cascade info to the shaders
		PushConstBlock pushConstBlock = { glm::vec4(0.0f), cascadeIndex };

		// Floor
		vkCmdPushConstants(commandBuffer, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(PushConstBlock), &pushConstBlock);
		models.terrain.draw(commandBuffer, vkglTF::RenderFlags::BindImages, pipelineLayout, 1);

		// Trees
		const std::vector<glm::vec3> positions = {
			glm::vec3(0.0f, 0.0f, 0.0f),
			glm::vec3(1.25f, 0.25f, 1.25f),
			glm::vec3(-1.25f, -0.2f, 1.25f),
			glm::vec3(1.25f, 0.1f, -1.25f),
			glm::vec3(-1.25f, -0.25f, -1.25f),
		};

		for (auto position : positions) {
			pushConstBlock.position = glm::vec4(position, 0.0f);
			vkCmdPushConstants(commandBuffer, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(PushConstBlock), &pushConstBlock);
			models.tree.draw(commandBuffer, vkglTF::RenderFlags::BindImages, pipelineLayout);
		}
	}

	// Setup resources used by the depth pass
	// The depth image is layered with each layer storing one shadow map cascade
	void createShadowCascadeObjects()
	{
		VkFormat depthFormat = vulkanDevice->getSupportedDepthFormat(true);

		// Depth map renderpass
		VkAttachmentDescription attachmentDescription{};
		attachmentDescription.format = depthFormat;
		attachmentDescription.samples = VK_SAMPLE_COUNT_1_BIT;
		attachmentDescription.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		attachmentDescription.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		attachmentDescription.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		attachmentDescription.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		attachmentDescription.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		attachmentDescription.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;

		VkAttachmentReference depthReference = {};
		depthReference.attachment = 0;
		depthReference.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

		VkSubpassDescription subpass = {};
		subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
		subpass.colorAttachmentCount = 0;
		subpass.pDepthStencilAttachment = &depthReference;

		// Use subpass dependencies for layout transitions
		std::array<VkSubpassDependency, 2> dependencies;

		dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
		dependencies[0].dstSubpass = 0;
		dependencies[0].srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
		dependencies[0].dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
		dependencies[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
		dependencies[0].dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
		dependencies[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

		dependencies[1].srcSubpass = 0;
		dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
		dependencies[1].srcStageMask = VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
		dependencies[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
		dependencies[1].srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
		dependencies[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
		dependencies[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

		VkRenderPassCreateInfo renderPassCreateInfo = vks::initializers::renderPassCreateInfo();
		renderPassCreateInfo.attachmentCount = 1;
		renderPassCreateInfo.pAttachments = &attachmentDescription;
		renderPassCreateInfo.subpassCount = 1;
		renderPassCreateInfo.pSubpasses = &subpass;
		renderPassCreateInfo.dependencyCount = static_cast<uint32_t>(dependencies.size());
		renderPassCreateInfo.pDependencies = dependencies.data();
		VK_CHECK_RESULT(vkCreateRenderPass(device, &renderPassCreateInfo, nullptr, &depth.renderPass));

		// Layered depth image and views
		VkImageCreateInfo imageInfo = vks::initializers::imageCreateInfo();
		imageInfo.imageType = VK_IMAGE_TYPE_2D;
		imageInfo.extent.width = shadowMapExtent.width;
		imageInfo.extent.height = shadowMapExtent.height;
		imageInfo.extent.depth = 1;
		imageInfo.mipLevels = 1;
		imageInfo.arrayLayers = shadowMapCascadeCount;
		imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
		imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
		imageInfo.format = depthFormat;
		imageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
		VK_CHECK_RESULT(vkCreateImage(device, &imageInfo, nullptr, &depth.image));
		VkMemoryAllocateInfo memAlloc = vks::initializers::memoryAllocateInfo();
		VkMemoryRequirements memReqs;
		vkGetImageMemoryRequirements(device, depth.image, &memReqs);
		memAlloc.allocationSize = memReqs.size;
		memAlloc.memoryTypeIndex = vulkanDevice->getMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
		VK_CHECK_RESULT(vkAllocateMemory(device, &memAlloc, nullptr, &depth.mem));
		VK_CHECK_RESULT(vkBindImageMemory(device, depth.image, depth.mem, 0));
		// Full depth map view (all layers)
		VkImageViewCreateInfo viewInfo = vks::initializers::imageViewCreateInfo();
		viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
		viewInfo.format = depthFormat;
		viewInfo.subresourceRange = {};
		viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
		viewInfo.subresourceRange.baseMipLevel = 0;
		viewInfo.subresourceRange.levelCount = 1;
		viewInfo.subresourceRange.baseArrayLayer = 0;
		viewInfo.subresourceRange.layerCount = shadowMapCascadeCount;
		viewInfo.image = depth.image;
		VK_CHECK_RESULT(vkCreateImageView(device, &viewInfo, nullptr, &depth.view));

		// One image and framebuffer per cascade
		for (uint32_t i = 0; i < shadowMapCascadeCount; i++) {
			// Image view for this cascade's layer (inside the depth map)
			// This view is used to render to that specific depth image layer
			VkImageViewCreateInfo viewInfo = vks::initializers::imageViewCreateInfo();
			viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
			viewInfo.format = depthFormat;
			viewInfo.subresourceRange = {};
			viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
			viewInfo.subresourceRange.baseMipLevel = 0;
			viewInfo.subresourceRange.levelCount = 1;
			viewInfo.subresourceRange.baseArrayLayer = i;
			viewInfo.subresourceRange.layerCount = 1;
			viewInfo.image = depth.image;
			VK_CHECK_RESULT(vkCreateImageView(device, &viewInfo, nullptr, &cascades[i].view));
			// Framebuffer
			VkFramebufferCreateInfo framebufferInfo = vks::initializers::framebufferCreateInfo();
			framebufferInfo.renderPass = depth.renderPass;
			framebufferInfo.attachmentCount = 1;
			framebufferInfo.pAttachments = &cascades[i].view;
			framebufferInfo.width = shadowMapExtent.width;
			framebufferInfo.height = shadowMapExtent.height;
			framebufferInfo.layers = 1;
			VK_CHECK_RESULT(vkCreateFramebuffer(device, &framebufferInfo, nullptr, &cascades[i].frameBuffer));
		}

		// Shared sampler for cascade depth reads
		VkSamplerCreateInfo sampler = vks::initializers::samplerCreateInfo();
		sampler.magFilter = VK_FILTER_LINEAR;
		sampler.minFilter = VK_FILTER_LINEAR;
		sampler.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
		sampler.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		sampler.addressModeV = sampler.addressModeU;
		sampler.addressModeW = sampler.addressModeU;
		sampler.mipLodBias = 0.0f;
		sampler.maxAnisotropy = 1.0f;
		sampler.minLod = 0.0f;
		sampler.maxLod = 1.0f;
		sampler.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
		VK_CHECK_RESULT(vkCreateSampler(device, &sampler, nullptr, &depth.sampler));
	}

	void loadAssets()
	{
		uint32_t glTFLoadingFlags = vkglTF::FileLoadingFlags::PreTransformVertices | vkglTF::FileLoadingFlags::FlipY;
		models.terrain.loadFromFile(getAssetPath() + "models/terrain_gridlines.gltf", vulkanDevice, queue, glTFLoadingFlags);
		models.tree.loadFromFile(getAssetPath() + "models/oaktree.gltf", vulkanDevice, queue, glTFLoadingFlags);
	}

	void createDescriptors()
	{
		// Descriptor pool
		std::vector<VkDescriptorPoolSize> poolSizes = {
			vks::initializers::descriptorPoolSize(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, getFrameCount()),
			vks::initializers::descriptorPoolSize(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, shadowMapCascadeCount + 1)
		};
		VkDescriptorPoolCreateInfo descriptorPoolInfo =
			vks::initializers::descriptorPoolCreateInfo(static_cast<uint32_t>(poolSizes.size()), poolSizes.data(), 4 + shadowMapCascadeCount);
		VK_CHECK_RESULT(vkCreateDescriptorPool(device, &descriptorPoolInfo, nullptr, &descriptorPool));

		// Layouts
		VkDescriptorSetLayoutCreateInfo descriptorSetLayoutCI{};
		VkDescriptorSetLayoutBinding setLayoutBinding{};

		// Layout for the per-frame uniform buffers
		setLayoutBinding = vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0);
		descriptorSetLayoutCI = vks::initializers::descriptorSetLayoutCreateInfo(setLayoutBinding);
		VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &descriptorSetLayoutCI, nullptr, &descriptorSetLayouts.uniformbuffers));

		// Layout for the shadow map cascade images
		setLayoutBinding = vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 0);
		descriptorSetLayoutCI = vks::initializers::descriptorSetLayoutCreateInfo(setLayoutBinding);
		VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &descriptorSetLayoutCI, nullptr, &descriptorSetLayouts.images));

		// Sets
		VkWriteDescriptorSet writeDescriptorSet{};
		VkDescriptorSetAllocateInfo allocInfo;

		// Per-frame for dynamic uniform buffers
		for (FrameObjects& frame : frameObjects) {
			allocInfo = vks::initializers::descriptorSetAllocateInfo(descriptorPool, &descriptorSetLayouts.uniformbuffers, 1);
			VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &allocInfo, &frame.descriptorSet));
			writeDescriptorSet = vks::initializers::writeDescriptorSet(frame.descriptorSet, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 0, &frame.uniformBuffer.descriptor);
			vkUpdateDescriptorSets(device, 1, &writeDescriptorSet, 0, nullptr);
		}

		// Per-cascade descriptor sets
		// Each descriptor set represents a single layer of the array texture
		allocInfo = vks::initializers::descriptorSetAllocateInfo(descriptorPool, &descriptorSetLayouts.images, 1);
		for (uint32_t i = 0; i < shadowMapCascadeCount; i++) {
			VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &allocInfo, &cascades[i].descriptorSet));
			VkDescriptorImageInfo cascadeImageInfo = vks::initializers::descriptorImageInfo(depth.sampler, depth.view, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL);
			writeDescriptorSet = vks::initializers::writeDescriptorSet(cascades[i].descriptorSet, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 0, &cascadeImageInfo);
			vkUpdateDescriptorSets(device, 1, &writeDescriptorSet, 0, nullptr);
		}

		// Set for the whole cascade image to be sampled from
		VkDescriptorImageInfo depthMapDescriptor = vks::initializers::descriptorImageInfo(depth.sampler, depth.view, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL);
		allocInfo = vks::initializers::descriptorSetAllocateInfo(descriptorPool, &descriptorSetLayouts.images, 1);
		VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &allocInfo, &cascadesDescriptorSet));
		writeDescriptorSet = vks::initializers::writeDescriptorSet(cascadesDescriptorSet, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 0, &depthMapDescriptor);
		vkUpdateDescriptorSets(device, 1, &writeDescriptorSet, 0, nullptr);
	}

	void createPipelines()
	{
		// Layouts
		// Layout for rendering the scene with applied shadow map
		VkPushConstantRange pushConstantRange = vks::initializers::pushConstantRange(VK_SHADER_STAGE_VERTEX_BIT, sizeof(PushConstBlock), 0);
		std::vector<VkDescriptorSetLayout> setLayouts = { descriptorSetLayouts.uniformbuffers, descriptorSetLayouts.images, descriptorSetLayouts.images };
		VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo = vks::initializers::pipelineLayoutCreateInfo(setLayouts.data(), 3);
		pipelineLayoutCreateInfo.pushConstantRangeCount = 1;
		pipelineLayoutCreateInfo.pPushConstantRanges = &pushConstantRange;
		VK_CHECK_RESULT(vkCreatePipelineLayout(device, &pipelineLayoutCreateInfo, nullptr, &pipelineLayouts.sceneRendering));
		// Layout for passing uniform buffers to the shadow map generation pass
		setLayouts = { descriptorSetLayouts.uniformbuffers, descriptorSetLayouts.images };
		pipelineLayoutCreateInfo = vks::initializers::pipelineLayoutCreateInfo(setLayouts.data(), 2);
		pipelineLayoutCreateInfo.pushConstantRangeCount = 1;
		pipelineLayoutCreateInfo.pPushConstantRanges = &pushConstantRange;
		VK_CHECK_RESULT(vkCreatePipelineLayout(device, &pipelineLayoutCreateInfo, nullptr, &pipelineLayouts.shadowmapGeneration));

		VkPipelineInputAssemblyStateCreateInfo inputAssemblyState = vks::initializers::pipelineInputAssemblyStateCreateInfo(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, 0, VK_FALSE);
		VkPipelineRasterizationStateCreateInfo rasterizationState = vks::initializers::pipelineRasterizationStateCreateInfo(VK_POLYGON_MODE_FILL, VK_CULL_MODE_BACK_BIT, VK_FRONT_FACE_CLOCKWISE, 0);
		VkPipelineColorBlendAttachmentState blendAttachmentState = vks::initializers::pipelineColorBlendAttachmentState(0xf, VK_FALSE);
		VkPipelineColorBlendStateCreateInfo colorBlendState = vks::initializers::pipelineColorBlendStateCreateInfo(1, &blendAttachmentState);
		VkPipelineDepthStencilStateCreateInfo depthStencilState = vks::initializers::pipelineDepthStencilStateCreateInfo(VK_TRUE, VK_TRUE, VK_COMPARE_OP_LESS_OR_EQUAL);
		VkPipelineViewportStateCreateInfo viewportState = vks::initializers::pipelineViewportStateCreateInfo(1, 1, 0);
		VkPipelineMultisampleStateCreateInfo multisampleState = vks::initializers::pipelineMultisampleStateCreateInfo(VK_SAMPLE_COUNT_1_BIT, 0);
		std::vector<VkDynamicState> dynamicStateEnables = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
		VkPipelineDynamicStateCreateInfo dynamicState = vks::initializers::pipelineDynamicStateCreateInfo(dynamicStateEnables);
		std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages;

		VkGraphicsPipelineCreateInfo pipelineCI = vks::initializers::pipelineCreateInfo();
		pipelineCI.pInputAssemblyState = &inputAssemblyState;
		pipelineCI.pRasterizationState = &rasterizationState;
		pipelineCI.pColorBlendState = &colorBlendState;
		pipelineCI.pMultisampleState = &multisampleState;
		pipelineCI.pViewportState = &viewportState;
		pipelineCI.pDepthStencilState = &depthStencilState;
		pipelineCI.pDynamicState = &dynamicState;
		pipelineCI.stageCount = static_cast<uint32_t>(shaderStages.size());
		pipelineCI.pStages = shaderStages.data();

		// Shadow map cascade debug quad display
		pipelineCI.renderPass = renderPass;
		pipelineCI.layout = pipelineLayouts.sceneRendering;

		rasterizationState.cullMode = VK_CULL_MODE_BACK_BIT;
		shaderStages[0] = loadShader(getShadersPath() + "shadowmappingcascade/debugshadowmap.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
		shaderStages[1] = loadShader(getShadersPath() + "shadowmappingcascade/debugshadowmap.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
		// Empty vertex input state
		VkPipelineVertexInputStateCreateInfo emptyInputState = vks::initializers::pipelineVertexInputStateCreateInfo();
		pipelineCI.pVertexInputState = &emptyInputState;
		VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCI, nullptr, &pipelines.debugShadowMap));

		// Shadow mapped scene rendering
		pipelineCI.pVertexInputState = vkglTF::Vertex::getPipelineVertexInputState({ vkglTF::VertexComponent::Position, vkglTF::VertexComponent::UV, vkglTF::VertexComponent::Color, vkglTF::VertexComponent::Normal });
		pipelineCI.renderPass = renderPass;
		pipelineCI.layout = pipelineLayouts.sceneRendering;
		rasterizationState.cullMode = VK_CULL_MODE_NONE;
		shaderStages[0] = loadShader(getShadersPath() + "shadowmappingcascade/scene.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
		shaderStages[1] = loadShader(getShadersPath() + "shadowmappingcascade/scene.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
		// Use specialization constants to select between horizontal and vertical blur
		uint32_t enablePCF = 0;
		VkSpecializationMapEntry specializationMapEntry = vks::initializers::specializationMapEntry(0, 0, sizeof(uint32_t));
		VkSpecializationInfo specializationInfo = vks::initializers::specializationInfo(1, &specializationMapEntry, sizeof(uint32_t), &enablePCF);
		shaderStages[1].pSpecializationInfo = &specializationInfo;
		VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCI, nullptr, &pipelines.sceneShadow));
		enablePCF = 1;
		VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCI, nullptr, &pipelines.sceneShadowPCF));

		// Depth map generation
		shaderStages[0] = loadShader(getShadersPath() + "shadowmappingcascade/depthpass.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
		shaderStages[1] = loadShader(getShadersPath() + "shadowmappingcascade/depthpass.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
		// No blend attachment states (no color attachments used)
		colorBlendState.attachmentCount = 0;
		depthStencilState.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
		// Enable depth clamp (if available)
		rasterizationState.depthClampEnable = deviceFeatures.depthClamp;
		pipelineCI.layout = pipelineLayouts.shadowmapGeneration;
		pipelineCI.renderPass = depth.renderPass;
		VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCI, nullptr, &pipelines.cascadeGeneration));
	}

	// Calculate frustum split depths and matrices for the shadow map cascades
	// Based on https://johanmedestrom.wordpress.com/2016/03/18/opengl-cascaded-shadow-maps/
	void updateCascades()
	{
		float cascadeSplits[shadowMapCascadeCount];

		float nearClip = camera.getNearClip();
		float farClip = camera.getFarClip();
		float clipRange = farClip - nearClip;

		float minZ = nearClip;
		float maxZ = nearClip + clipRange;

		float range = maxZ - minZ;
		float ratio = maxZ / minZ;

		// Calculate split depths based on view camera frustum
		// Based on method presented in https://developer.nvidia.com/gpugems/GPUGems3/gpugems3_ch10.html
		for (uint32_t i = 0; i < shadowMapCascadeCount; i++) {
			float p = (i + 1) / static_cast<float>(shadowMapCascadeCount);
			float log = minZ * std::pow(ratio, p);
			float uniform = minZ + range * p;
			float d = cascadeSplitLambda * (log - uniform) + uniform;
			cascadeSplits[i] = (d - nearClip) / clipRange;
		}

		// Calculate orthographic projection matrix for each cascade
		float lastSplitDist = 0.0;
		for (uint32_t i = 0; i < shadowMapCascadeCount; i++) {
			float splitDist = cascadeSplits[i];

			glm::vec3 frustumCorners[8] = {
				glm::vec3(-1.0f,  1.0f, -1.0f),
				glm::vec3( 1.0f,  1.0f, -1.0f),
				glm::vec3( 1.0f, -1.0f, -1.0f),
				glm::vec3(-1.0f, -1.0f, -1.0f),
				glm::vec3(-1.0f,  1.0f,  1.0f),
				glm::vec3( 1.0f,  1.0f,  1.0f),
				glm::vec3( 1.0f, -1.0f,  1.0f),
				glm::vec3(-1.0f, -1.0f,  1.0f),
			};

			// Project frustum corners into world space
			glm::mat4 invCam = glm::inverse(camera.matrices.perspective * camera.matrices.view);
			for (uint32_t i = 0; i < 8; i++) {
				glm::vec4 invCorner = invCam * glm::vec4(frustumCorners[i], 1.0f);
				frustumCorners[i] = invCorner / invCorner.w;
			}

			for (uint32_t i = 0; i < 4; i++) {
				glm::vec3 dist = frustumCorners[i + 4] - frustumCorners[i];
				frustumCorners[i + 4] = frustumCorners[i] + (dist * splitDist);
				frustumCorners[i] = frustumCorners[i] + (dist * lastSplitDist);
			}

			// Get frustum center
			glm::vec3 frustumCenter = glm::vec3(0.0f);
			for (uint32_t i = 0; i < 8; i++) {
				frustumCenter += frustumCorners[i];
			}
			frustumCenter /= 8.0f;

			float radius = 0.0f;
			for (uint32_t i = 0; i < 8; i++) {
				float distance = glm::length(frustumCorners[i] - frustumCenter);
				radius = glm::max(radius, distance);
			}
			radius = std::ceil(radius * 16.0f) / 16.0f;

			glm::vec3 maxExtents = glm::vec3(radius);
			glm::vec3 minExtents = -maxExtents;

			glm::vec3 lightDir = normalize(-lightPos);
			glm::mat4 lightViewMatrix = glm::lookAt(frustumCenter - lightDir * -minExtents.z, frustumCenter, glm::vec3(0.0f, 1.0f, 0.0f));
			glm::mat4 lightOrthoMatrix = glm::ortho(minExtents.x, maxExtents.x, minExtents.y, maxExtents.y, 0.0f, maxExtents.z - minExtents.z);

			// Store split distance and matrix in cascade
			cascades[i].splitDepth = (camera.getNearClip() + splitDist * clipRange) * -1.0f;
			cascades[i].viewProjMatrix = lightOrthoMatrix * lightViewMatrix;

			lastSplitDist = cascadeSplits[i];
		}
	}

	void prepare()
	{
		VulkanExampleBase::prepare();
		// Prepare per-frame ressources
		frameObjects.resize(getFrameCount());
		for (FrameObjects& frame : frameObjects) {
			createBaseFrameObjects(frame);
			// Uniform buffers
			VK_CHECK_RESULT(vulkanDevice->createAndMapBuffer(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &frame.uniformBuffer, sizeof(UniformData)));
		}
		loadAssets();
		createShadowCascadeObjects();
		createDescriptors();
		createPipelines();
		prepared = true;
	}

	virtual void render()
	{
		FrameObjects currentFrame = frameObjects[getCurrentFrameIndex()];

		VulkanExampleBase::prepareFrame(currentFrame);

		// Update cascade matrices for the next frame
		updateCascades();
		
		// Update uniform-buffers for the next frame
		if (!paused) {
			float angle = glm::radians(timer * 360.0f);
			float radius = 20.0f;
			lightPos = glm::vec4(cos(angle) * radius, -radius, sin(angle) * radius, 0.0f);
		}
		uniformData.projection = camera.matrices.perspective;
		uniformData.view = camera.matrices.view;
		uniformData.model = glm::mat4(1.0f);
		uniformData.lightDir = normalize(-lightPos);
		for (uint32_t i = 0; i < shadowMapCascadeCount; i++) {
			uniformData.cascadeSplits[i] = cascades[i].splitDepth;
			uniformData.cascadeViewProjMat[i] = cascades[i].viewProjMatrix;
		}
		uniformData.inverseViewMat = glm::inverse(camera.matrices.view);
		uniformData.lightDir = normalize(-lightPos);
		uniformData.colorCascades = colorCascades;
		memcpy(currentFrame.uniformBuffer.mapped, &uniformData, sizeof(uniformData));

		// Build the command buffer
		const VkCommandBuffer commandBuffer = currentFrame.commandBuffer;
		const VkCommandBufferBeginInfo commandBufferBeginInfo = getCommandBufferBeginInfo();
		VK_CHECK_RESULT(vkBeginCommandBuffer(commandBuffer, &commandBufferBeginInfo));

		VkRenderPassBeginInfo renderPassBeginInfo{};
		VkRect2D renderArea{};
		VkViewport viewport{};
		VkClearValue clearValues[2]{};

		// First passes: Generate depth map cascades
		// Uses multiple passes with each pass rendering the scene to the cascade's depth image layer
		// Could be optimized using a geometry shader (and layered frame buffer) on devices that support geometry shaders

		viewport = vks::initializers::viewport((float)shadowMapExtent.width, (float)shadowMapExtent.height, 0.0f, 1.0f);
		renderArea = vks::initializers::rect2D(shadowMapExtent.width, shadowMapExtent.height, 0, 0);
		vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
		vkCmdSetScissor(commandBuffer, 0, 1, &renderArea);

		clearValues[0].depthStencil = { 1.0f, 0 };
		renderPassBeginInfo = vks::initializers::renderPassBeginInfo();
		renderPassBeginInfo.renderPass = depth.renderPass;
		renderPassBeginInfo.renderArea.extent = shadowMapExtent;
		renderPassBeginInfo.clearValueCount = 1;
		renderPassBeginInfo.pClearValues = clearValues;

		// One pass per cascade
		// The layer that this pass renders to is defined by the cascade's image view (selected via the cascade's descriptor set)
		vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayouts.sceneRendering, 0, 1, &currentFrame.descriptorSet, 0, nullptr); // @todo
		for (uint32_t j = 0; j < shadowMapCascadeCount; j++) {
			renderPassBeginInfo.framebuffer = cascades[j].frameBuffer;
			vkCmdBeginRenderPass(commandBuffer, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);
			vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.cascadeGeneration );
			vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayouts.shadowmapGeneration, 1, 1, &cascades[j].descriptorSet, 0, nullptr);
			renderScene(commandBuffer, pipelineLayouts.shadowmapGeneration, j);
			vkCmdEndRenderPass(commandBuffer);
		}

		// Final pass: Scene rendering using depth cascades for shadow mapping
		// Note: Explicit synchronization is not required between the render pass, as this is done implicit via sub pass dependencies

		renderArea = getRenderArea();
		viewport = getViewport();
		clearValues[0].color = { { 0.0f, 0.0f, 0.2f, 1.0f } };
		clearValues[1].depthStencil = { 1.0f, 0 };
		renderPassBeginInfo = getRenderPassBeginInfo(renderPass, clearValues);
		vkCmdBeginRenderPass(commandBuffer, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);
		vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
		vkCmdSetScissor(commandBuffer, 0, 1, &renderArea);

		// Bind the uniform buffer for the current frame
		vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayouts.sceneRendering, 0, 1, &currentFrame.descriptorSet, 0, nullptr);

		// Visualize shadow map cascade
		if (displayDepthMap) {
			vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayouts.sceneRendering, 1, 1, &cascadesDescriptorSet, 0, nullptr);
			vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.debugShadowMap);
			PushConstBlock pushConstBlock = {};
			pushConstBlock.cascadeIndex = displayDepthMapCascadeIndex;
			vkCmdPushConstants(commandBuffer, pipelineLayouts.sceneRendering, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(PushConstBlock), &pushConstBlock);
			vkCmdDraw(commandBuffer, 3, 1, 0, 0);
		}

		// Render shadowed scene
		vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, (filterPCF) ? pipelines.sceneShadowPCF : pipelines.sceneShadow);
		vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayouts.sceneRendering, 2, 1, &cascadesDescriptorSet, 0, nullptr);
		renderScene(commandBuffer, pipelineLayouts.sceneRendering);

		drawUI(commandBuffer);

		vkCmdEndRenderPass(commandBuffer);

		VK_CHECK_RESULT(vkEndCommandBuffer(commandBuffer));

		VulkanExampleBase::submitFrame(currentFrame);
	}

	virtual void OnUpdateUIOverlay(vks::UIOverlay *overlay)
	{
		if (overlay->header("Settings")) {
			if (overlay->sliderFloat("Split lambda", &cascadeSplitLambda, 0.1f, 1.0f)) {
				updateCascades();
			}
			overlay->checkBox("Color cascades", &colorCascades);
			overlay->checkBox("Display depth map", &displayDepthMap);
			if (displayDepthMap) {
				overlay->sliderInt("Cascade", &displayDepthMapCascadeIndex, 0, shadowMapCascadeCount - 1);
			}
			overlay->checkBox("PCF filtering", &filterPCF);
		}
	}
};

VULKAN_EXAMPLE_MAIN()
