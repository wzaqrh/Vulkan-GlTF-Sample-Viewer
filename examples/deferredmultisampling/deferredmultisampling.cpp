/*
 * Vulkan Example - Multi sampling with explicit resolve for deferred shading example
 *
 * Copyright (C) 2016-2021 by Sascha Willems - www.saschawillems.de
 *
 * This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
 */

/*
 * This sample builds on the deferred rendering sample and shows how to do multi sample with a deferred setup
 * Attachments are created with multiple samples and the the composition fragment shader does a manual resolve into the final, anti aliased image (see deferred.frag)
 */

#include "vulkanexamplebase.h"
#include "VulkanglTFModel.h"

#define ENABLE_VALIDATION false

class VulkanExample : public VulkanExampleBase
{
public:
	VkExtent2D renderTargetExtent = { 2048, 2048 };

	int32_t debugDisplayTarget = 0;
	bool useMSAA = true;
	bool useSampleShading = true;
	VkSampleCountFlagBits sampleCount = VK_SAMPLE_COUNT_1_BIT;

	struct Textures {
		struct TextureMap {
			vks::Texture2D color;
			vks::Texture2D normal;
		} model, background;
	} textures;

	struct Models {
		vkglTF::Model model;
		vkglTF::Model background;
	} models;

	struct Light {
		glm::vec4 position;
		glm::vec3 color;
		float radius;
	};

	struct UniformData {
		glm::mat4 projection;
		glm::mat4 model;
		glm::mat4 view;
		// Used to place the objects in the scane
		glm::vec4 instancePos[3];
		// Dynamic lights
		Light lights[6];
		glm::vec4 viewPos;
		// Debug display toggle (if > 0)
		int debugDisplayTarget = 0;
	} uniformData;

	struct FrameObjects : public VulkanFrameObjects {
		vks::Buffer uniformBuffer;
		VkDescriptorSet descriptorSet;
	};
	std::vector<FrameObjects> frameObjects;
	// The descriptors for the render targets and textures are static, and not required to be per-frame
	struct StaticDescriptorSets {
		VkDescriptorSet gBuffer;
		VkDescriptorSet modelTextures;
		VkDescriptorSet backgroundTextures;
	} staticDescriptorSets;

	struct DescriptorSetLayouts {
		VkDescriptorSetLayout uniformbuffers;
		VkDescriptorSetLayout images;
	} descriptorSetLayouts;

	struct Pipelines {
		VkPipeline deferred;				// Deferred lighting calculation
		VkPipeline deferredNoMSAA;			// Deferred lighting calculation with explicit MSAA resolve
		VkPipeline offscreen;				// (Offscreen) scene rendering (fill G-Buffers)
		VkPipeline offscreenSampleShading;	// (Offscreen) scene rendering (fill G-Buffers) with sample shading rate enabled
	} pipelines;
	VkPipelineLayout pipelineLayout;

	struct FrameBufferAttachment {
		VkImage image;
		VkDeviceMemory memory;
		VkImageView view;
		VkFormat format;
		void destroy(VkDevice device) {
			vkDestroyImage(device, image, nullptr);
			vkDestroyImageView(device, view, nullptr);
			vkFreeMemory(device, memory, nullptr);
		}
	};
	// Holds the attachments for the components of the G-Buffer
	struct gBufferPass {
		VkFramebuffer frameBuffer;
		FrameBufferAttachment position, normal, albedo, depth;
		VkRenderPass renderPass;
		VkSampler sampler;
	} gBufferPass;

	VulkanExample() : VulkanExampleBase(ENABLE_VALIDATION)
	{
		title = "Multi sampled deferred shading";
		camera.type = Camera::CameraType::firstperson;
		camera.movementSpeed = 5.0f;
#ifndef __ANDROID__
		camera.rotationSpeed = 0.25f;
#endif
		camera.position = { 2.15f, 0.3f, -8.75f };
		camera.setRotation(glm::vec3(-0.75f, 12.5f, 0.0f));
		camera.setPerspective(60.0f, (float)width / (float)height, 0.1f, 256.0f);
		paused = true;
		settings.overlay = true;
	}

	~VulkanExample()
	{
		if (device) {
			vkDestroySampler(device, gBufferPass.sampler, nullptr);
			gBufferPass.albedo.destroy(device);
			gBufferPass.depth.destroy(device);
			gBufferPass.normal.destroy(device);
			gBufferPass.position.destroy(device);
			vkDestroyFramebuffer(device, gBufferPass.frameBuffer, nullptr);
			vkDestroyPipeline(device, pipelines.deferred, nullptr);
			vkDestroyPipeline(device, pipelines.deferredNoMSAA, nullptr);
			vkDestroyPipeline(device, pipelines.offscreen, nullptr);
			vkDestroyPipeline(device, pipelines.offscreenSampleShading, nullptr);
			vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
			vkDestroyRenderPass(device, gBufferPass.renderPass, nullptr);
			vkDestroyDescriptorSetLayout(device, descriptorSetLayouts.images, nullptr);
			vkDestroyDescriptorSetLayout(device, descriptorSetLayouts.uniformbuffers, nullptr);
			textures.model.color.destroy();
			textures.model.normal.destroy();
			textures.background.color.destroy();
			textures.background.normal.destroy();
			for (FrameObjects& frame : frameObjects) {
				frame.uniformBuffer.destroy();
				destroyBaseFrameObjects(frame);
			}
		}
	}

	// Enable physical device features required for this example
	virtual void getEnabledFeatures()
	{
		// Enable sample rate shading filtering if supported
		if (deviceFeatures.sampleRateShading) {
			enabledFeatures.sampleRateShading = VK_TRUE;
		}
		// Enable anisotropic filtering if supported
		if (deviceFeatures.samplerAnisotropy) {
			enabledFeatures.samplerAnisotropy = VK_TRUE;
		}
	};

	// Create a frame buffer attachment
	void createAttachment(VkFormat format, VkImageUsageFlagBits usage, FrameBufferAttachment* attachment)
	{
		VkImageAspectFlags aspectMask = 0;
		VkImageLayout imageLayout;

		attachment->format = format;

		if (usage & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT)
		{
			aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		}
		if (usage & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT)
		{
			aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
			imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
		}

		assert(aspectMask > 0);

		VkImageCreateInfo image = vks::initializers::imageCreateInfo();
		image.imageType = VK_IMAGE_TYPE_2D;
		image.format = format;
		image.extent.width = renderTargetExtent.width;
		image.extent.height = renderTargetExtent.height;
		image.extent.depth = 1;
		image.mipLevels = 1;
		image.arrayLayers = 1;
		image.samples = VK_SAMPLE_COUNT_1_BIT;
		image.tiling = VK_IMAGE_TILING_OPTIMAL;
		image.usage = usage | VK_IMAGE_USAGE_SAMPLED_BIT;
		image.samples = sampleCount;

		VkMemoryAllocateInfo memAlloc = vks::initializers::memoryAllocateInfo();
		VkMemoryRequirements memReqs;

		VK_CHECK_RESULT(vkCreateImage(device, &image, nullptr, &attachment->image));
		vkGetImageMemoryRequirements(device, attachment->image, &memReqs);
		memAlloc.allocationSize = memReqs.size;
		memAlloc.memoryTypeIndex = vulkanDevice->getMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
		VK_CHECK_RESULT(vkAllocateMemory(device, &memAlloc, nullptr, &attachment->memory));
		VK_CHECK_RESULT(vkBindImageMemory(device, attachment->image, attachment->memory, 0));

		VkImageViewCreateInfo imageView = vks::initializers::imageViewCreateInfo();
		imageView.viewType = VK_IMAGE_VIEW_TYPE_2D;
		imageView.format = format;
		imageView.subresourceRange = {};
		imageView.subresourceRange.aspectMask = aspectMask;
		imageView.subresourceRange.baseMipLevel = 0;
		imageView.subresourceRange.levelCount = 1;
		imageView.subresourceRange.baseArrayLayer = 0;
		imageView.subresourceRange.layerCount = 1;
		imageView.image = attachment->image;
		VK_CHECK_RESULT(vkCreateImageView(device, &imageView, nullptr, &attachment->view));
	}

	// Prepares attachments, framebuffer and render pass for the offscreen G-Buffer creation
	void createGBuffer()
	{
		// The G-Buffer in this sample contains four attachments
		// (World space) Positions
		createAttachment(VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, &gBufferPass.position);
		// (World space) Normals
		createAttachment(VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, &gBufferPass.normal);
		// Albedo
		createAttachment(VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, &gBufferPass.albedo);
		// Depth
		VkFormat depthFormat;
		VkBool32 validDepthFormat = vks::tools::getSupportedDepthFormat(physicalDevice, &depthFormat);
		assert(validDepthFormat);
		createAttachment(depthFormat, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, &gBufferPass.depth);

		// Set up a renderpass with references to the color and depth attachments that's used to fill the attachments
		std::array<VkAttachmentDescription, 4> attachmentDescriptions = {};
		attachmentDescriptions[0].format = gBufferPass.position.format;
		attachmentDescriptions[1].format = gBufferPass.normal.format;
		attachmentDescriptions[2].format = gBufferPass.albedo.format;
		attachmentDescriptions[3].format = gBufferPass.depth.format;
		for (uint32_t i = 0; i < 4; ++i) {
			attachmentDescriptions[i].samples = VK_SAMPLE_COUNT_1_BIT;
			attachmentDescriptions[i].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
			attachmentDescriptions[i].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
			attachmentDescriptions[i].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
			attachmentDescriptions[i].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
			if (attachmentDescriptions[i].format == depthFormat) {
				// Layout for the depth attachment
				attachmentDescriptions[i].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
				attachmentDescriptions[i].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
			}
			else {
				// Layouts for the color attachment, which are read from the shaders in the composition pass
				attachmentDescriptions[i].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
				attachmentDescriptions[i].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			}
			// Set the selected sample count 
			attachmentDescriptions[i].samples = sampleCount;
		}

		std::vector<VkAttachmentReference> colorReferences;
		colorReferences.push_back({ 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL });
		colorReferences.push_back({ 1, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL });
		colorReferences.push_back({ 2, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL });
		VkAttachmentReference depthReference = {};
		depthReference.attachment = 3;
		depthReference.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
		VkSubpassDescription subpass = {};
		subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
		subpass.pColorAttachments = colorReferences.data();
		subpass.colorAttachmentCount = static_cast<uint32_t>(colorReferences.size());
		subpass.pDepthStencilAttachment = &depthReference;

		// We use se subpass dependencies for attachment layout transitions
		std::array<VkSubpassDependency, 3> dependencies{};

		// These dependencies ensure that writes to both color and depth attachments have finished before we start writing to them again
		dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
		dependencies[0].dstSubpass = 0;
		dependencies[0].srcStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
		dependencies[0].dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
		dependencies[0].srcAccessMask = 0;
		dependencies[0].dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

		dependencies[1].srcSubpass = VK_SUBPASS_EXTERNAL;
		dependencies[1].dstSubpass = 0;
		dependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		dependencies[1].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		dependencies[1].srcAccessMask = 0;
		dependencies[1].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

		// This dependency ensures that writes to the color attachments have finished before the second render pass starts reading from them
		dependencies[2].srcSubpass = 0;
		dependencies[2].dstSubpass = VK_SUBPASS_EXTERNAL;
		dependencies[2].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		dependencies[2].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
		dependencies[2].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		dependencies[2].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

		VkRenderPassCreateInfo renderPassCI = vks::initializers::renderPassCreateInfo();
		renderPassCI.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
		renderPassCI.pAttachments = attachmentDescriptions.data();
		renderPassCI.attachmentCount = static_cast<uint32_t>(attachmentDescriptions.size());
		renderPassCI.subpassCount = 1;
		renderPassCI.pSubpasses = &subpass;
		renderPassCI.dependencyCount = static_cast<uint32_t>(dependencies.size());
		renderPassCI.pDependencies = dependencies.data();
		VK_CHECK_RESULT(vkCreateRenderPass(device, &renderPassCI, nullptr, &gBufferPass.renderPass));

		std::array<VkImageView, 4> attachments;
		attachments[0] = gBufferPass.position.view;
		attachments[1] = gBufferPass.normal.view;
		attachments[2] = gBufferPass.albedo.view;
		attachments[3] = gBufferPass.depth.view;

		VkFramebufferCreateInfo framebufferCI = vks::initializers::framebufferCreateInfo();
		framebufferCI.renderPass = gBufferPass.renderPass;
		framebufferCI.pAttachments = attachments.data();
		framebufferCI.attachmentCount = static_cast<uint32_t>(attachments.size());
		framebufferCI.width = renderTargetExtent.width;
		framebufferCI.height = renderTargetExtent.height;
		framebufferCI.layers = 1;
		VK_CHECK_RESULT(vkCreateFramebuffer(device, &framebufferCI, nullptr, &gBufferPass.frameBuffer));

		// Create a sampler for the G-Buffer attachments
		VkSamplerCreateInfo samplerCI = vks::initializers::samplerCreateInfo();
		samplerCI.magFilter = VK_FILTER_NEAREST;
		samplerCI.minFilter = VK_FILTER_NEAREST;
		samplerCI.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
		samplerCI.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		samplerCI.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		samplerCI.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		samplerCI.mipLodBias = 0.0f;
		samplerCI.maxAnisotropy = 1.0f;
		samplerCI.minLod = 0.0f;
		samplerCI.maxLod = 1.0f;
		VK_CHECK_RESULT(vkCreateSampler(device, &samplerCI, nullptr, &gBufferPass.sampler));
	}

	void loadAssets()
	{
		const uint32_t glTFLoadingFlags = vkglTF::FileLoadingFlags::PreTransformVertices | vkglTF::FileLoadingFlags::PreMultiplyVertexColors | vkglTF::FileLoadingFlags::FlipY;
		models.model.loadFromFile(getAssetPath() + "models/armor/armor.gltf", vulkanDevice, queue, glTFLoadingFlags);
		models.background.loadFromFile(getAssetPath() + "models/deferred_box.gltf", vulkanDevice, queue, glTFLoadingFlags);
		textures.model.color.loadFromFile(getAssetPath() + "models/armor/colormap_rgba.ktx", VK_FORMAT_R8G8B8A8_UNORM, vulkanDevice, queue);
		textures.model.normal.loadFromFile(getAssetPath() + "models/armor/normalmap_rgba.ktx", VK_FORMAT_R8G8B8A8_UNORM, vulkanDevice, queue);
		textures.background.color.loadFromFile(getAssetPath() + "textures/stonefloor02_color_rgba.ktx", VK_FORMAT_R8G8B8A8_UNORM, vulkanDevice, queue);
		textures.background.normal.loadFromFile(getAssetPath() + "textures/stonefloor02_normal_rgba.ktx", VK_FORMAT_R8G8B8A8_UNORM, vulkanDevice, queue);
	}

	void createDescriptors()
	{
		// Pool
		std::vector<VkDescriptorPoolSize> poolSizes = {
			vks::initializers::descriptorPoolSize(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, getFrameCount()),
			vks::initializers::descriptorPoolSize(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 9)
		};
		VkDescriptorPoolCreateInfo descriptorPoolInfo = vks::initializers::descriptorPoolCreateInfo(poolSizes, getFrameCount() + 3);
		VK_CHECK_RESULT(vkCreateDescriptorPool(device, &descriptorPoolInfo, nullptr, &descriptorPool));

		// Layouts
		VkDescriptorSetLayoutCreateInfo descriptorSetLayoutCI{};
		std::vector<VkDescriptorSetLayoutBinding> setLayoutBindings{};

		// Layout for the per-frame uniform buffers
		VkDescriptorSetLayoutBinding setLayoutBinding = vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0);
		descriptorSetLayoutCI = vks::initializers::descriptorSetLayoutCreateInfo(setLayoutBinding);
		VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &descriptorSetLayoutCI, nullptr, &descriptorSetLayouts.uniformbuffers));

		// Layout for the deferred render targets and model textures
		setLayoutBindings = {
			vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 0),
			vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 1),
			vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 2),
		};
		descriptorSetLayoutCI = vks::initializers::descriptorSetLayoutCreateInfo(setLayoutBindings);
		VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &descriptorSetLayoutCI, nullptr, &descriptorSetLayouts.images));

		// Sets
		// Per-frame for dynamic uniform buffers
		for (FrameObjects& frame : frameObjects) {
			VkDescriptorSetAllocateInfo allocInfo = vks::initializers::descriptorSetAllocateInfo(descriptorPool, &descriptorSetLayouts.uniformbuffers, 1);
			VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &allocInfo, &frame.descriptorSet));
			VkWriteDescriptorSet writeDescriptorSet = vks::initializers::writeDescriptorSet(frame.descriptorSet, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 0, &frame.uniformBuffer.descriptor);
			vkUpdateDescriptorSets(device, 1, &writeDescriptorSet, 0, nullptr);
		}

		// Global sets for the images
		std::vector<VkWriteDescriptorSet> writeDescriptorSets{};
		VkDescriptorSetAllocateInfo allocInfo = vks::initializers::descriptorSetAllocateInfo(descriptorPool, &descriptorSetLayouts.images, 1);;
		// G-Buffer attachments
		VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &allocInfo, &staticDescriptorSets.gBuffer));
		std::vector<VkDescriptorImageInfo> imageDescriptors = {
			vks::initializers::descriptorImageInfo(gBufferPass.sampler, gBufferPass.position.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL),
			vks::initializers::descriptorImageInfo(gBufferPass.sampler, gBufferPass.normal.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL),
			vks::initializers::descriptorImageInfo(gBufferPass.sampler, gBufferPass.albedo.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
		};
		writeDescriptorSets = {
			vks::initializers::writeDescriptorSet(staticDescriptorSets.gBuffer, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 0, &imageDescriptors[0]),
			vks::initializers::writeDescriptorSet(staticDescriptorSets.gBuffer, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, &imageDescriptors[1]),
			vks::initializers::writeDescriptorSet(staticDescriptorSets.gBuffer, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2, &imageDescriptors[2]),
		};
		vkUpdateDescriptorSets(device, static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, nullptr);
		// Textures
		VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &allocInfo, &staticDescriptorSets.modelTextures));
		writeDescriptorSets = {
			vks::initializers::writeDescriptorSet(staticDescriptorSets.modelTextures, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 0, &textures.model.color.descriptor),
			vks::initializers::writeDescriptorSet(staticDescriptorSets.modelTextures, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, &textures.model.normal.descriptor)
		};
		vkUpdateDescriptorSets(device, static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, nullptr);
		// Floor
		VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &allocInfo, &staticDescriptorSets.backgroundTextures));
		writeDescriptorSets = {
			vks::initializers::writeDescriptorSet(staticDescriptorSets.backgroundTextures, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 0, &textures.background.color.descriptor),
			vks::initializers::writeDescriptorSet(staticDescriptorSets.backgroundTextures, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, &textures.background.normal.descriptor)
		};
		vkUpdateDescriptorSets(device, static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, nullptr);
	}

	void createPipelines()
	{
		// Layout shared by all pipelines with per-frame uniform buffers and static images
		std::vector<VkDescriptorSetLayout> setLayouts = { descriptorSetLayouts.uniformbuffers, descriptorSetLayouts.images };
		VkPipelineLayoutCreateInfo pipelineLayoutCI = vks::initializers::pipelineLayoutCreateInfo(setLayouts.data(), 2);
		VK_CHECK_RESULT(vkCreatePipelineLayout(device, &pipelineLayoutCI, nullptr, &pipelineLayout));

		// Pipelines
		VkPipelineInputAssemblyStateCreateInfo inputAssemblyState = vks::initializers::pipelineInputAssemblyStateCreateInfo(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, 0, VK_FALSE);
		VkPipelineRasterizationStateCreateInfo rasterizationState = vks::initializers::pipelineRasterizationStateCreateInfo(VK_POLYGON_MODE_FILL, VK_CULL_MODE_BACK_BIT, VK_FRONT_FACE_COUNTER_CLOCKWISE, 0);
		VkPipelineColorBlendAttachmentState blendAttachmentState = vks::initializers::pipelineColorBlendAttachmentState(0xf, VK_FALSE);
		VkPipelineColorBlendStateCreateInfo colorBlendState = vks::initializers::pipelineColorBlendStateCreateInfo(1, &blendAttachmentState);
		VkPipelineDepthStencilStateCreateInfo depthStencilState = vks::initializers::pipelineDepthStencilStateCreateInfo(VK_TRUE, VK_TRUE, VK_COMPARE_OP_LESS_OR_EQUAL);
		VkPipelineViewportStateCreateInfo viewportState = vks::initializers::pipelineViewportStateCreateInfo(1, 1, 0);
		VkPipelineMultisampleStateCreateInfo multisampleState = vks::initializers::pipelineMultisampleStateCreateInfo(VK_SAMPLE_COUNT_1_BIT, 0);
		std::vector<VkDynamicState> dynamicStateEnables = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
		VkPipelineDynamicStateCreateInfo dynamicState = vks::initializers::pipelineDynamicStateCreateInfo(dynamicStateEnables);
		std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages;

		VkGraphicsPipelineCreateInfo pipelineCI = vks::initializers::pipelineCreateInfo();
		pipelineCI.layout = pipelineLayout;
		pipelineCI.renderPass = renderPass;
		pipelineCI.pInputAssemblyState = &inputAssemblyState;
		pipelineCI.pRasterizationState = &rasterizationState;
		pipelineCI.pColorBlendState = &colorBlendState;
		pipelineCI.pMultisampleState = &multisampleState;
		pipelineCI.pViewportState = &viewportState;
		pipelineCI.pDepthStencilState = &depthStencilState;
		pipelineCI.pDynamicState = &dynamicState;
		pipelineCI.stageCount = static_cast<uint32_t>(shaderStages.size());
		pipelineCI.pStages = shaderStages.data();

		// Fullscreen composition pass
		// Empty vertex input state, vertices are generated by the vertex shader
		VkPipelineVertexInputStateCreateInfo emptyInputState = vks::initializers::pipelineVertexInputStateCreateInfo();
		pipelineCI.pVertexInputState = &emptyInputState;
		// Use specialization constants to pass number of samples to the shader (used for MSAA resolve)
		VkSpecializationMapEntry specializationEntry{};
		specializationEntry.constantID = 0;
		specializationEntry.offset = 0;
		specializationEntry.size = sizeof(uint32_t);
		uint32_t specializationData = sampleCount;
		VkSpecializationInfo specializationInfo;
		specializationInfo.mapEntryCount = 1;
		specializationInfo.pMapEntries = &specializationEntry;
		specializationInfo.dataSize = sizeof(specializationData);
		specializationInfo.pData = &specializationData;
		rasterizationState.cullMode = VK_CULL_MODE_FRONT_BIT;

		// With MSAA enabled
		shaderStages[0] = loadShader(getShadersPath() + "deferredmultisampling/deferred.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
		shaderStages[1] = loadShader(getShadersPath() + "deferredmultisampling/deferred.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
		shaderStages[1].pSpecializationInfo = &specializationInfo;
		VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCI, nullptr, &pipelines.deferred));

		// No MSAA (1 sample)
		specializationData = 1;
		VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCI, nullptr, &pipelines.deferredNoMSAA));

		// Vertex input state from glTF model for pipeline rendering models
		pipelineCI.pVertexInputState = vkglTF::Vertex::getPipelineVertexInputState({ vkglTF::VertexComponent::Position, vkglTF::VertexComponent::UV, vkglTF::VertexComponent::Color, vkglTF::VertexComponent::Normal, vkglTF::VertexComponent::Tangent });
		rasterizationState.cullMode = VK_CULL_MODE_BACK_BIT;

		// Offscreen scene rendering pipelines to fill the G-Buffer		
		pipelineCI.renderPass = gBufferPass.renderPass;
		shaderStages[0] = loadShader(getShadersPath() + "deferredmultisampling/mrt.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
		shaderStages[1] = loadShader(getShadersPath() + "deferredmultisampling/mrt.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);

		// Set no. of MSAA sample
		multisampleState.rasterizationSamples = sampleCount;
		multisampleState.alphaToCoverageEnable = VK_TRUE;

		// Blend attachment states required for all color attachments
		// This is important, as color write mask will otherwise be 0x0 and you
		// won't see anything rendered to the attachment
		std::array<VkPipelineColorBlendAttachmentState, 3> blendAttachmentStates = {
			vks::initializers::pipelineColorBlendAttachmentState(0xf, VK_FALSE),
			vks::initializers::pipelineColorBlendAttachmentState(0xf, VK_FALSE),
			vks::initializers::pipelineColorBlendAttachmentState(0xf, VK_FALSE)
		};
		// Without sample rate shading
		colorBlendState.attachmentCount = static_cast<uint32_t>(blendAttachmentStates.size());
		colorBlendState.pAttachments = blendAttachmentStates.data();
		VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCI, nullptr, &pipelines.offscreen));

		// With sample rate shading
		multisampleState.sampleShadingEnable = VK_TRUE;
		multisampleState.minSampleShading = 0.25f;
		VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCI, nullptr, &pipelines.offscreenSampleShading));
	}

	void initUniformValues()
	{
		// Setup instanced model positions
		uniformData.instancePos[0] = glm::vec4(0.0f);
		uniformData.instancePos[1] = glm::vec4(-4.0f, 0.0, -4.0f, 0.0f);
		uniformData.instancePos[2] = glm::vec4(4.0f, 0.0, -4.0f, 0.0f);
		// Lights
		// White
		uniformData.lights[0].position = glm::vec4(0.0f, 0.0f, 1.0f, 0.0f);
		uniformData.lights[0].color = glm::vec3(1.5f);
		uniformData.lights[0].radius = 15.0f * 0.25f;
		// Red
		uniformData.lights[1].position = glm::vec4(-2.0f, 0.0f, 0.0f, 0.0f);
		uniformData.lights[1].color = glm::vec3(1.0f, 0.0f, 0.0f);
		uniformData.lights[1].radius = 15.0f;
		// Blue
		uniformData.lights[2].position = glm::vec4(2.0f, -1.0f, 0.0f, 0.0f);
		uniformData.lights[2].color = glm::vec3(0.0f, 0.0f, 2.5f);
		uniformData.lights[2].radius = 5.0f;
		// Yellow
		uniformData.lights[3].position = glm::vec4(0.0f, -0.9f, 0.5f, 0.0f);
		uniformData.lights[3].color = glm::vec3(1.0f, 1.0f, 0.0f);
		uniformData.lights[3].radius = 2.0f;
		// Green
		uniformData.lights[4].position = glm::vec4(0.0f, -0.5f, 0.0f, 0.0f);
		uniformData.lights[4].color = glm::vec3(0.0f, 1.0f, 0.2f);
		uniformData.lights[4].radius = 5.0f;
		// Yellow
		uniformData.lights[5].position = glm::vec4(0.0f, -1.0f, 0.0f, 0.0f);
		uniformData.lights[5].color = glm::vec3(1.0f, 0.7f, 0.3f);
		uniformData.lights[5].radius = 25.0f;
	}

	// Returns a reasonable maximum sample count usable by the selected device
	VkSampleCountFlagBits getMaxUsableSampleCount()
	{
		VkSampleCountFlags counts = std::min(deviceProperties.limits.framebufferColorSampleCounts, deviceProperties.limits.framebufferDepthSampleCounts);
		// We want 8 samples at max
		if (counts & VK_SAMPLE_COUNT_8_BIT) { return VK_SAMPLE_COUNT_8_BIT; }
		if (counts & VK_SAMPLE_COUNT_4_BIT) { return VK_SAMPLE_COUNT_4_BIT; }
		if (counts & VK_SAMPLE_COUNT_2_BIT) { return VK_SAMPLE_COUNT_2_BIT; }
		return VK_SAMPLE_COUNT_1_BIT;
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
#if defined(__ANDROID__)
		// On Andoid, we use the larger screen dimension for the G-Buffer attchment size
		renderTargetExtent = { std::max(width,height), std::max(width,height) };
#endif
		sampleCount = getMaxUsableSampleCount();
		loadAssets();
		initUniformValues();
		createGBuffer();
		createDescriptors();
		createPipelines();
		prepared = true;
	}

	virtual void render()
	{
		FrameObjects currentFrame = frameObjects[getCurrentFrameIndex()];

		VulkanExampleBase::prepareFrame(currentFrame);

		// Update uniform-buffers for the next frame
		uniformData.projection = camera.matrices.perspective;
		uniformData.view = camera.matrices.view;
		uniformData.model = glm::mat4(1.0f);
		uniformData.viewPos = glm::vec4(camera.position, 0.0f) * glm::vec4(-1.0f, 1.0f, -1.0f, 1.0f);
		uniformData.debugDisplayTarget = debugDisplayTarget;
		// Animate the lights
		uniformData.lights[0].position.x = sin(glm::radians(360.0f * timer)) * 5.0f;
		uniformData.lights[0].position.z = cos(glm::radians(360.0f * timer)) * 5.0f;
		uniformData.lights[1].position.x = -4.0f + sin(glm::radians(360.0f * timer) + 45.0f) * 2.0f;
		uniformData.lights[1].position.z = 0.0f + cos(glm::radians(360.0f * timer) + 45.0f) * 2.0f;
		uniformData.lights[2].position.x = 4.0f + sin(glm::radians(360.0f * timer)) * 2.0f;
		uniformData.lights[2].position.z = 0.0f + cos(glm::radians(360.0f * timer)) * 2.0f;
		uniformData.lights[4].position.x = 0.0f + sin(glm::radians(360.0f * timer + 90.0f)) * 5.0f;
		uniformData.lights[4].position.z = 0.0f - cos(glm::radians(360.0f * timer + 45.0f)) * 5.0f;
		uniformData.lights[5].position.x = 0.0f + sin(glm::radians(-360.0f * timer + 135.0f)) * 10.0f;
		uniformData.lights[5].position.z = 0.0f - cos(glm::radians(-360.0f * timer - 45.0f)) * 10.0f;
		memcpy(currentFrame.uniformBuffer.mapped, &uniformData, sizeof(uniformData));

		// Build the command buffer
		const VkCommandBuffer commandBuffer = currentFrame.commandBuffer;
		const VkCommandBufferBeginInfo commandBufferBeginInfo = getCommandBufferBeginInfo();
		VkRenderPassBeginInfo renderPassBeginInfo{};
		VkViewport viewport{};
		VkRect2D renderArea{};
		VK_CHECK_RESULT(vkBeginCommandBuffer(commandBuffer, &commandBufferBeginInfo));

		// First pass: Fill the G-Buffer attachments with positions, normals, albedo and specular values

		// We need to clear all attachments written in the fragment shader
		std::array<VkClearValue, 4> clearValues;
		clearValues[0].color = { { 0.0f, 0.0f, 0.0f, 0.0f } };
		clearValues[1].color = { { 0.0f, 0.0f, 0.0f, 0.0f } };
		clearValues[2].color = { { 0.0f, 0.0f, 0.0f, 0.0f } };
		clearValues[3].depthStencil = { 1.0f, 0 };

		renderPassBeginInfo = vks::initializers::renderPassBeginInfo();
		renderPassBeginInfo.renderPass = gBufferPass.renderPass;
		renderPassBeginInfo.framebuffer = gBufferPass.frameBuffer;
		renderPassBeginInfo.renderArea.extent = renderTargetExtent;
		renderPassBeginInfo.clearValueCount = 4;
		renderPassBeginInfo.pClearValues = clearValues.data();

		viewport = vks::initializers::viewport(renderTargetExtent, 0.0f, 1.0f);
		renderArea = vks::initializers::rect2D(renderTargetExtent);

		vkCmdBeginRenderPass(commandBuffer, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

		vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
		vkCmdSetScissor(commandBuffer, 0, 1, &renderArea);

		vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, useSampleShading ? pipelines.offscreenSampleShading : pipelines.offscreen);
		vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &frameObjects[0].descriptorSet, 0, nullptr);

		// Draw the floor
		vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 1, 1, &staticDescriptorSets.backgroundTextures, 0, nullptr);
		models.background.draw(commandBuffer);

		// draw the instanced models, positions are taken from the uniform buffer
		vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 1, 1, &staticDescriptorSets.modelTextures, 0, nullptr);
		models.model.bindBuffers(commandBuffer);
		vkCmdDrawIndexed(commandBuffer, models.model.indices.count, 3, 0, 0, 0);

		vkCmdEndRenderPass(commandBuffer);

		// Second render pass: Use the G-Buffer attachments to compose the final scene, applying lighting in screen space
		renderArea = getRenderArea();
		viewport = getViewport();
		renderPassBeginInfo = getRenderPassBeginInfo(renderPass, defaultClearValues);
		vkCmdBeginRenderPass(commandBuffer, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);
		vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
		vkCmdSetScissor(commandBuffer, 0, 1, &renderArea);

		vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &frameObjects[0].descriptorSet, 0, nullptr);
		vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 1, 1, &staticDescriptorSets.gBuffer, 0, nullptr);

		vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, useMSAA ? pipelines.deferred : pipelines.deferredNoMSAA);
		// Draw a screen covering triangle, composition is done in the fragment shader
		vkCmdDraw(commandBuffer, 3, 1, 0, 0);

		drawUI(commandBuffer);

		vkCmdEndRenderPass(commandBuffer);

		VK_CHECK_RESULT(vkEndCommandBuffer(commandBuffer));

		VulkanExampleBase::submitFrame(currentFrame);
	}

	virtual void OnUpdateUIOverlay(vks::UIOverlay *overlay)
	{
		if (overlay->header("Settings")) {
			overlay->comboBox("Display", &debugDisplayTarget, { "Final composition", "Position", "Normals", "Albedo", "Specular" });
			overlay->checkBox("MSAA", &useMSAA);
			if (vulkanDevice->features.sampleRateShading) {
				overlay->checkBox("Sample rate shading", &useSampleShading);
			}
		}
	}

};

VULKAN_EXAMPLE_MAIN()
