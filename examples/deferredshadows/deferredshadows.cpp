/*
 * Vulkan Example - Deferred shading with shadows from multiple light sources using geometry shader instancing
 *
 * Copyright (C) 2016-2021 by Sascha Willems - www.saschawillems.de
 *
 * This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
 */

/*
 * This sample builds on the deferred rendering sample and shows how to add multiple shadows to a deferred setup
 * In addition to the G-Buffer attachments, a layered shadow attachment is created with the nubmer of layers = number of lights in the scene (see createLayeredShadowmap)
 * A geometry shader is then used to output depth for each light in a single pass to these layers in a single pass
 * The layered shadow attachment is then used in the final composition pass to apply shadows to the scene
 */

#include "vulkanexamplebase.h"
#include "VulkanglTFModel.h"

#define ENABLE_VALIDATION false

// Must match the LIGHT_COUNT define in the shadow and deferred shaders
#define LIGHT_COUNT 3

class VulkanExample : public VulkanExampleBase
{
public:
	VkExtent2D renderTargetExtent = { 2048, 2048 };
	VkExtent2D shadowMapExtent = { 2048, 2048 };

	int32_t debugDisplayTarget = 0;
	bool enableShadows = true;

	// We keep the Z-Range as small as possible to increase shadow precisions
	float zNear = 0.1f;
	float zFar = 64.0f;
	float lightFOV = 100.0f;

	// Depth bias (and slope) are used to avoid shadowing artifacts
	float depthBiasConstant = 1.25f;
	float depthBiasSlope = 1.75f;

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
		glm::vec4 target;
		glm::vec4 color;
		// This contains the matrix from the light's point-of-view, which is used for creating and applying shadows
		glm::mat4 viewMatrix;
	};

	struct UniformData {
		glm::mat4 projection;
		glm::mat4 model;
		glm::mat4 view;
		// Used to place the objects in the scane
		glm::vec4 instancePos[3];
		glm::vec4 viewPos;
		Light lights[LIGHT_COUNT];
		uint32_t useShadows = 1;
		int32_t debugDisplayTarget = 0;
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

	struct PipelineLayouts {
		VkPipeline deferred;
		VkPipeline offscreen;
		VkPipeline shadowpass;
	} pipelines;
	VkPipelineLayout pipelineLayout;

	struct DescriptorSetLayouts {
		VkDescriptorSetLayout uniformbuffers;
		VkDescriptorSetLayout images;
	} descriptorSetLayouts;

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
	// Holds the layered attachment for the shadow maps
	struct ShadowPass {
		VkExtent2D Size;
		VkFramebuffer frameBuffer;
		FrameBufferAttachment attachment;
		VkRenderPass renderPass;
		VkSampler sampler;
	} shadowPass;

	VulkanExample() : VulkanExampleBase(ENABLE_VALIDATION)
	{
		title = "Deferred shading with shadows";
		camera.type = Camera::CameraType::firstperson;
#if defined(__ANDROID__)
		camera.movementSpeed = 2.5f;
#else
		camera.movementSpeed = 5.0f;
		camera.rotationSpeed = 0.25f;
#endif
		camera.position = { 2.15f, 0.3f, -8.75f };
		camera.setRotation(glm::vec3(-0.75f, 12.5f, 0.0f));
		camera.setPerspective(60.0f, (float)width / (float)height, zNear, zFar);
		timerSpeed *= 0.25f;
		settings.overlay = true;
	}

	~VulkanExample()
	{
		if (device) {
			vkDestroySampler(device, shadowPass.sampler, nullptr);
			shadowPass.attachment.destroy(device);
			vkDestroyFramebuffer(device, shadowPass.frameBuffer, nullptr);
			vkDestroyRenderPass(device, shadowPass.renderPass, nullptr);
			vkDestroySampler(device, gBufferPass.sampler, nullptr);
			gBufferPass.albedo.destroy(device);
			gBufferPass.depth.destroy(device);
			gBufferPass.normal.destroy(device);
			gBufferPass.position.destroy(device);
			vkDestroyFramebuffer(device, gBufferPass.frameBuffer, nullptr);
			vkDestroyRenderPass(device, gBufferPass.renderPass, nullptr);
			vkDestroyPipeline(device, pipelines.deferred, nullptr);
			vkDestroyPipeline(device, pipelines.offscreen, nullptr);
			vkDestroyPipeline(device, pipelines.shadowpass, nullptr);
			vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
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
		// Geometry shader support is required for writing to multiple shadow map layers in one single pass
		if (deviceFeatures.geometryShader) {
			enabledFeatures.geometryShader = VK_TRUE;
		} else {
			vks::tools::exitFatal("Selected GPU does not support geometry shaders!", VK_ERROR_FEATURE_NOT_PRESENT);
		}
		// Enable anisotropic filtering if supported
		if (deviceFeatures.samplerAnisotropy) {
			enabledFeatures.samplerAnisotropy = VK_TRUE;
		}
	}

	// Prepare a layered shadow map with each layer containing depth from a light's point of view
	// The shadow mapping pass uses geometry shader instancing to output the scene from the different
	// light sources' point of view to the layers of the depth attachment in one single pass
	void createLayeredShadowmap()
	{
		// Get a supported depth format that we can use for the shadow maps
		VkFormat depthFormat;
		VkBool32 validDepthFormat = vks::tools::getSupportedDepthFormat(physicalDevice, &depthFormat);
		assert(validDepthFormat);

		// Create a layered image
		VkImageCreateInfo image = vks::initializers::imageCreateInfo();
		image.imageType = VK_IMAGE_TYPE_2D;
		image.format = depthFormat;
		image.extent.width = shadowMapExtent.width;
		image.extent.height = shadowMapExtent.height;
		image.extent.depth = 1;
		image.mipLevels = 1;
		image.arrayLayers = LIGHT_COUNT;
		image.samples = VK_SAMPLE_COUNT_1_BIT;
		image.tiling = VK_IMAGE_TILING_OPTIMAL;
		image.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
		VK_CHECK_RESULT(vkCreateImage(device, &image, nullptr, &shadowPass.attachment.image));
		VkMemoryRequirements memRequirements;
		vkGetImageMemoryRequirements(device, shadowPass.attachment.image, &memRequirements);
		VkMemoryAllocateInfo memAllocInfo = vks::initializers::memoryAllocateInfo();
		memAllocInfo.allocationSize = memRequirements.size;
		memAllocInfo.memoryTypeIndex = vulkanDevice->getMemoryType(memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
		VK_CHECK_RESULT(vkAllocateMemory(device, &memAllocInfo, nullptr, &shadowPass.attachment.memory));
		VK_CHECK_RESULT(vkBindImageMemory(device, shadowPass.attachment.image, shadowPass.attachment.memory, 0));

		VkImageViewCreateInfo imageView = vks::initializers::imageViewCreateInfo();
		imageView.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
		imageView.format = depthFormat;
		imageView.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
		imageView.subresourceRange.levelCount = 1;
		imageView.subresourceRange.layerCount = LIGHT_COUNT;
		imageView.image = shadowPass.attachment.image;
		VK_CHECK_RESULT(vkCreateImageView(device, &imageView, nullptr, &shadowPass.attachment.view));

		// Create a sampler for the layered shadow attachment
		VkSamplerCreateInfo samplerCI = vks::initializers::samplerCreateInfo();
		samplerCI.magFilter = VK_FILTER_LINEAR;
		samplerCI.minFilter = VK_FILTER_LINEAR;
		samplerCI.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
		samplerCI.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		samplerCI.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		samplerCI.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		samplerCI.mipLodBias = 0.0f;
		samplerCI.maxAnisotropy = 1.0f;
		samplerCI.minLod = 0.0f;
		samplerCI.maxLod = 1.0f;
		VK_CHECK_RESULT(vkCreateSampler(device, &samplerCI, nullptr, &shadowPass.sampler));

		// Set up a renderpass with references to the color and depth attachments that's used to fill the attachments
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
		subpass.pDepthStencilAttachment = &depthReference;

		// We use se subpass dependencies for attachment layout transitions
		std::array<VkSubpassDependency, 2> dependencies{};

		// These dependencies ensure that writes to the depth attachment have finished before we start writing to it again
		dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
		dependencies[0].dstSubpass = 0;
		dependencies[0].srcStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
		dependencies[0].dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
		dependencies[0].srcAccessMask = 0;
		dependencies[0].dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

		// This dependency ensures that writes to the depth attachments have finished before the second render pass starts reading from it
		dependencies[1].srcSubpass = 0;
		dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
		dependencies[1].srcStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;;
		dependencies[1].dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;;
		dependencies[1].srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
		dependencies[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

		VkRenderPassCreateInfo renderPassCI = vks::initializers::renderPassCreateInfo();
		renderPassCI.pAttachments = &attachmentDescription;
		renderPassCI.attachmentCount = 1;
		renderPassCI.subpassCount = 1;
		renderPassCI.pSubpasses = &subpass;
		renderPassCI.dependencyCount = static_cast<uint32_t>(dependencies.size());
		renderPassCI.pDependencies = dependencies.data();
		VK_CHECK_RESULT(vkCreateRenderPass(device, &renderPassCI, nullptr, &shadowPass.renderPass));

		VkFramebufferCreateInfo framebufferCI = vks::initializers::framebufferCreateInfo();
		framebufferCI.renderPass = shadowPass.renderPass;
		framebufferCI.pAttachments = &shadowPass.attachment.view;
		framebufferCI.attachmentCount = 1;
		framebufferCI.width = shadowMapExtent.width;
		framebufferCI.height = shadowMapExtent.height;
		framebufferCI.layers = LIGHT_COUNT;
		VK_CHECK_RESULT(vkCreateFramebuffer(device, &framebufferCI, nullptr, &shadowPass.frameBuffer));

	}

	// Create a frame buffer attachment
	void createAttachment(VkFormat format, VkImageUsageFlagBits usage, FrameBufferAttachment* attachment, VkExtent2D size)
	{
		attachment->format = format;

		VkImageAspectFlags aspectMask{};
		if (usage & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) {
			aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		}
		if (usage & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT) {
			aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
		}

		VkImageCreateInfo image = vks::initializers::imageCreateInfo();
		image.imageType = VK_IMAGE_TYPE_2D;
		image.format = format;
		image.extent.width = size.width;
		image.extent.height = size.height;
		image.extent.depth = 1;
		image.mipLevels = 1;
		image.arrayLayers = 1;
		image.samples = VK_SAMPLE_COUNT_1_BIT;
		image.tiling = VK_IMAGE_TILING_OPTIMAL;
		image.usage = usage | VK_IMAGE_USAGE_SAMPLED_BIT;
		VK_CHECK_RESULT(vkCreateImage(device, &image, nullptr, &attachment->image));

		VkMemoryRequirements memRequirements;
		vkGetImageMemoryRequirements(device, attachment->image, &memRequirements);
		VkMemoryAllocateInfo memAllocInfo = vks::initializers::memoryAllocateInfo();
		memAllocInfo.allocationSize = memRequirements.size;
		memAllocInfo.memoryTypeIndex = vulkanDevice->getMemoryType(memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
		VK_CHECK_RESULT(vkAllocateMemory(device, &memAllocInfo, nullptr, &attachment->memory));
		VK_CHECK_RESULT(vkBindImageMemory(device, attachment->image, attachment->memory, 0));

		VkImageViewCreateInfo imageView = vks::initializers::imageViewCreateInfo();
		imageView.viewType = VK_IMAGE_VIEW_TYPE_2D;
		imageView.format = format;
		imageView.subresourceRange.aspectMask = aspectMask;
		imageView.subresourceRange.levelCount = 1;
		imageView.subresourceRange.layerCount = 1;
		imageView.image = attachment->image;
		VK_CHECK_RESULT(vkCreateImageView(device, &imageView, nullptr, &attachment->view));
	}

	// Prepare the framebuffer for offscreen rendering with multiple attachments used as render targets inside the fragment shaders
	void createGBuffer()
	{
		createAttachment(VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, &gBufferPass.position, renderTargetExtent);
		createAttachment(VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, &gBufferPass.normal, renderTargetExtent);
		createAttachment(VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, &gBufferPass.albedo, renderTargetExtent);
		VkFormat depthFormat;
		VkBool32 validDepthFormat = vks::tools::getSupportedDepthFormat(physicalDevice, &depthFormat);
		assert(validDepthFormat);
		createAttachment(depthFormat, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, &gBufferPass.depth, renderTargetExtent);

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
			} else {
				// Layouts for the color attachment, which are read from the shaders in the composition pass
				attachmentDescriptions[i].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
				attachmentDescriptions[i].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			}
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

	// Put render commands for the scene into the given command buffer
	void renderScene(FrameObjects frame)
	{
		vkCmdBindDescriptorSets(frame.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &frame.descriptorSet, 0, nullptr);
		vkCmdBindDescriptorSets(frame.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 1, 1, &staticDescriptorSets.backgroundTextures, 0, nullptr);
		models.background.draw(frame.commandBuffer);
		vkCmdBindDescriptorSets(frame.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 1, 1, &staticDescriptorSets.modelTextures, 0, nullptr);
		models.model.bindBuffers(frame.commandBuffer);
		vkCmdDrawIndexed(frame.commandBuffer, models.model.indices.count, 3, 0, 0, 0);
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
			vks::initializers::descriptorPoolSize(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 12)
		};
		VkDescriptorPoolCreateInfo descriptorPoolInfo = vks::initializers::descriptorPoolCreateInfo(poolSizes, getFrameCount() + 4);
		VK_CHECK_RESULT(vkCreateDescriptorPool(device, &descriptorPoolInfo, nullptr, &descriptorPool));

		// Layouts
		VkDescriptorSetLayoutCreateInfo descriptorSetLayoutCI{};
		std::vector<VkDescriptorSetLayoutBinding> setLayoutBindings{};

		// Layout for the per-frame uniform buffers
		VkDescriptorSetLayoutBinding setLayoutBinding = vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_GEOMETRY_BIT, 0);
		descriptorSetLayoutCI = vks::initializers::descriptorSetLayoutCreateInfo(setLayoutBinding);
		VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &descriptorSetLayoutCI, nullptr, &descriptorSetLayouts.uniformbuffers));

		// Layout for the deferred render targets and model textures
		setLayoutBindings = {
			vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 0),
			vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 1),
			vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 2),
			vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 3),
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
			vks::initializers::descriptorImageInfo(gBufferPass.sampler, gBufferPass.albedo.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL),
			vks::initializers::descriptorImageInfo(shadowPass.sampler, shadowPass.attachment.view, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL)
		};
		writeDescriptorSets = {
			vks::initializers::writeDescriptorSet(staticDescriptorSets.gBuffer, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 0, &imageDescriptors[0]),
			vks::initializers::writeDescriptorSet(staticDescriptorSets.gBuffer, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, &imageDescriptors[1]),
			vks::initializers::writeDescriptorSet(staticDescriptorSets.gBuffer, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2, &imageDescriptors[2]),
			vks::initializers::writeDescriptorSet(staticDescriptorSets.gBuffer, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 3, &imageDescriptors[3]),
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

		// Final fullscreen composition pass pipeline
		rasterizationState.cullMode = VK_CULL_MODE_FRONT_BIT;
		shaderStages[0] = loadShader(getShadersPath() + "deferredshadows/deferred.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
		shaderStages[1] = loadShader(getShadersPath() + "deferredshadows/deferred.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
		// Empty vertex input state, vertices are generated by the vertex shader
		VkPipelineVertexInputStateCreateInfo emptyInputState = vks::initializers::pipelineVertexInputStateCreateInfo();
		pipelineCI.pVertexInputState = &emptyInputState;
		VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCI, nullptr, &pipelines.deferred));

		// Vertex input state from glTF model for pipeline rendering models
		pipelineCI.pVertexInputState = vkglTF::Vertex::getPipelineVertexInputState({ vkglTF::VertexComponent::Position, vkglTF::VertexComponent::UV, vkglTF::VertexComponent::Color, vkglTF::VertexComponent::Normal, vkglTF::VertexComponent::Tangent });
		rasterizationState.cullMode = VK_CULL_MODE_BACK_BIT;

		// Offscreen pipeline
		// Separate render pass
		pipelineCI.renderPass = gBufferPass.renderPass;

		// Blend attachment states required for all color attachments
		// This is important, as color write mask will otherwise be 0x0 and you
		// won't see anything rendered to the attachment
		std::array<VkPipelineColorBlendAttachmentState, 3> blendAttachmentStates = {
			vks::initializers::pipelineColorBlendAttachmentState(0xf, VK_FALSE),
			vks::initializers::pipelineColorBlendAttachmentState(0xf, VK_FALSE),
			vks::initializers::pipelineColorBlendAttachmentState(0xf, VK_FALSE)
		};
		colorBlendState.attachmentCount = static_cast<uint32_t>(blendAttachmentStates.size());
		colorBlendState.pAttachments = blendAttachmentStates.data();

		shaderStages[0] = loadShader(getShadersPath() + "deferredshadows/mrt.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
		shaderStages[1] = loadShader(getShadersPath() + "deferredshadows/mrt.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
		VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCI, nullptr, &pipelines.offscreen));

		// Shadow mapping pipeline
		// The shadow mapping pipeline uses geometry shader instancing (invocations layout modifier) to output
		// shadow maps for multiple lights sources into the different shadow map layers in one single render pass
		std::array<VkPipelineShaderStageCreateInfo, 2> shadowStages;
		shadowStages[0] = loadShader(getShadersPath() + "deferredshadows/shadow.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
		shadowStages[1] = loadShader(getShadersPath() + "deferredshadows/shadow.geom.spv", VK_SHADER_STAGE_GEOMETRY_BIT);

		pipelineCI.pStages = shadowStages.data();
		pipelineCI.stageCount = static_cast<uint32_t>(shadowStages.size());

		// Shadow pass doesn't use any color attachments
		colorBlendState.attachmentCount = 0;
		colorBlendState.pAttachments = nullptr;
		// Cull front faces
		rasterizationState.cullMode = VK_CULL_MODE_FRONT_BIT;
		depthStencilState.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
		// Enable depth bias
		rasterizationState.depthBiasEnable = VK_TRUE;
		// Add depth bias to dynamic state, so we can change it at runtime
		dynamicStateEnables.push_back(VK_DYNAMIC_STATE_DEPTH_BIAS);
		dynamicState = vks::initializers::pipelineDynamicStateCreateInfo(dynamicStateEnables);
		// Reset blend attachment state
		pipelineCI.renderPass = shadowPass.renderPass;
		VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCI, nullptr, &pipelines.shadowpass));
	}

	Light initSpotlight(glm::vec3 pos, glm::vec3 target, glm::vec3 color)
	{
		Light light;
		light.position = glm::vec4(pos, 1.0f);
		light.target = glm::vec4(target, 0.0f);
		light.color = glm::vec4(color, 0.0f);
		return light;
	}

	void initUniformValues()
	{
		// Setup instanced model positions
		uniformData.instancePos[0] = glm::vec4(0.0f);
		uniformData.instancePos[1] = glm::vec4(-7.0f, 0.0, -4.0f, 0.0f);
		uniformData.instancePos[2] = glm::vec4(4.0f, 0.0, -6.0f, 0.0f);
		// Initial spotlight properites
		uniformData.lights[0] = initSpotlight(glm::vec3(-14.0f, -0.5f, 15.0f), glm::vec3(-2.0f, 0.0f, 0.0f), glm::vec3(1.0f, 0.5f, 0.5f));
		uniformData.lights[1] = initSpotlight(glm::vec3(14.0f, -4.0f, 12.0f), glm::vec3(2.0f, 0.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f));
		uniformData.lights[2] = initSpotlight(glm::vec3(0.0f, -10.0f, 4.0f), glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(1.0f, 1.0f, 1.0f));
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
		// On Andoid, we use the larger screen dimension for the attachment sizes
		renderTargetExtent = { std::max(width,height), std::max(width,height) };
		// Use a smaller shadow map
		shadowMapExtent = { 1024, 1024 };
#endif
		loadAssets();
		initUniformValues();
		createGBuffer();
		createLayeredShadowmap();
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
		uniformData.viewPos = glm::vec4(camera.position, 0.0f) * glm::vec4(-1.0f, 1.0f, -1.0f, 1.0f);;
		uniformData.debugDisplayTarget = debugDisplayTarget;
		// Animate the lights
		uniformData.lights[0].position.x = -14.0f + std::abs(sin(glm::radians(timer * 360.0f)) * 20.0f);
		uniformData.lights[0].position.z = 15.0f + cos(glm::radians(timer * 360.0f)) * 1.0f;
		uniformData.lights[1].position.x = 14.0f - std::abs(sin(glm::radians(timer * 360.0f)) * 2.5f);
		uniformData.lights[1].position.y = -4.0f - std::abs(sin(glm::radians(timer * 360.0f)) * 1.5f);
		uniformData.lights[1].position.z = 13.0f + cos(glm::radians(timer * 360.0f)) * 4.0f;
		uniformData.lights[2].position.x = 0.0f + sin(glm::radians(timer * 360.0f)) * 4.0f;
		uniformData.lights[2].position.z = 4.0f + cos(glm::radians(timer * 360.0f)) * 2.0f;
		// Calculate view matrices from each light's point-of-view, used for the shadow map calculations
		for (uint32_t i = 0; i < LIGHT_COUNT; i++) {
			glm::mat4 shadowProj = glm::perspective(glm::radians(lightFOV), 1.0f, zNear, zFar);
			glm::mat4 shadowView = glm::lookAt(glm::vec3(uniformData.lights[i].position), glm::vec3(uniformData.lights[i].target), glm::vec3(0.0f, 1.0f, 0.0f));
			glm::mat4 shadowModel = glm::mat4(1.0f);
			uniformData.lights[i].viewMatrix = shadowProj * shadowView * shadowModel;
		}
		memcpy(currentFrame.uniformBuffer.mapped, &uniformData, sizeof(uniformData));

		// Build the command buffer
		const VkCommandBuffer commandBuffer = currentFrame.commandBuffer;
		const VkCommandBufferBeginInfo commandBufferBeginInfo = getCommandBufferBeginInfo();
		VkRenderPassBeginInfo renderPassBeginInfo = vks::initializers::renderPassBeginInfo();
		VkViewport viewport{};
		VkRect2D renderArea{};
		std::array<VkClearValue, 4> clearValues{};
		VK_CHECK_RESULT(vkBeginCommandBuffer(commandBuffer, &commandBufferBeginInfo));

		// First render pass: Generate shadow maps for all light sources

		clearValues[0].depthStencil = { 1.0f, 0 };

		renderPassBeginInfo.renderPass = shadowPass.renderPass;
		renderPassBeginInfo.framebuffer = shadowPass.frameBuffer;
		renderPassBeginInfo.renderArea.extent = shadowMapExtent;
		renderPassBeginInfo.clearValueCount = 1;
		renderPassBeginInfo.pClearValues = clearValues.data();

		viewport = vks::initializers::viewport(shadowMapExtent, 0.0f, 1.0f);
		renderArea = vks::initializers::rect2D(shadowMapExtent);
		
		vkCmdBeginRenderPass(commandBuffer, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);
		vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
		vkCmdSetScissor(commandBuffer, 0, 1, &renderArea);
		// Set depth bias (aka "Polygon offset") to avoid shadow artefacts
		vkCmdSetDepthBias(commandBuffer, depthBiasConstant, 0.0f, depthBiasSlope);
		vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.shadowpass);
		renderScene(currentFrame);
		vkCmdEndRenderPass(commandBuffer);

		// Second render pass: Fill the G-Buffer attachments with positions, normals, albedo and specular values

		// We need to clear all attachments written in the fragment shader
		clearValues[0].color = { { 0.0f, 0.0f, 0.0f, 0.0f } };
		clearValues[1].color = { { 0.0f, 0.0f, 0.0f, 0.0f } };
		clearValues[2].color = { { 0.0f, 0.0f, 0.0f, 0.0f } };
		clearValues[3].depthStencil = { 1.0f, 0 };

		renderPassBeginInfo.renderPass = gBufferPass.renderPass;
		renderPassBeginInfo.framebuffer = gBufferPass.frameBuffer;
		renderPassBeginInfo.renderArea.extent = renderTargetExtent;
		renderPassBeginInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
		renderPassBeginInfo.pClearValues = clearValues.data();

		viewport = vks::initializers::viewport(renderTargetExtent, 0.0f, 1.0f);
		renderArea = vks::initializers::rect2D(renderTargetExtent);

		vkCmdBeginRenderPass(commandBuffer, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);
		vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
		vkCmdSetScissor(commandBuffer, 0, 1, &renderArea);
		vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.offscreen);
		renderScene(currentFrame);
		vkCmdEndRenderPass(commandBuffer);

		// Third render pass: Use the G-Buffer attachments to compose the final scene, applying lighting and shadows in screen space

		renderArea = getRenderArea();
		viewport = getViewport();
		renderPassBeginInfo = getRenderPassBeginInfo(renderPass, defaultClearValues);

		vkCmdBeginRenderPass(commandBuffer, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);
		vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
		vkCmdSetScissor(commandBuffer, 0, 1, &renderArea);
		vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &currentFrame.descriptorSet, 0, nullptr);
		vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 1, 1, &staticDescriptorSets.gBuffer, 0, nullptr);
		// Draw a screen covering triangle, composition is done in the fragment shader
		vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.deferred);
		vkCmdDraw(commandBuffer, 3, 1, 0, 0);
		drawUI(commandBuffer);
		vkCmdEndRenderPass(commandBuffer);

		VK_CHECK_RESULT(vkEndCommandBuffer(commandBuffer));

		VulkanExampleBase::submitFrame(currentFrame);
	}

	virtual void OnUpdateUIOverlay(vks::UIOverlay *overlay)
	{
		if (overlay->header("Settings")) {
			overlay->comboBox("Display", &debugDisplayTarget, { "Final composition", "Shadows", "Position", "Normals", "Albedo", "Specular" });
			bool shadows = (uniformData.useShadows == 1);
			if (overlay->checkBox("Shadows", &shadows)) {
				uniformData.useShadows = shadows;
			}
		}
	}
};

VULKAN_EXAMPLE_MAIN()
