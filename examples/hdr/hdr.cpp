/*
 * Vulkan Example - High dynamic range rendering
 *
 * Copyright 2016-2022 by Sascha Willems - www.saschawillems.de
 *
 * This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
 */

/*
 * This sample implements a high dynamic range rendering pipeline with based on a deferred G-Buffer setup with a post-processing bloom effect
 * The scene is rendered to multiple render targets at once (MRT) to fill color and bright highlight (for bloom) targets
 * Those are then combined in a final composition pass for the scene visible on the screen
 */

#include "vulkanexamplebase.h"
#include "VulkanglTFModel.h"

#define ENABLE_VALIDATION false

class VulkanExample : public VulkanExampleBase
{
public:
	// The bloom filter will run at lower resolution
	VkExtent2D bloomExtent = { 256, 256 };

	bool bloom = true;
	bool displaySkybox = true;

	vks::TextureCubeMap cubemap;

	struct Models {
		vkglTF::Model skybox;
		std::vector<vkglTF::Model> objects;
		int32_t objectIndex = 1;
	} models;

	struct UniformData {
		glm::mat4 projection;
		glm::mat4 modelview;
		glm::mat4 inverseModelview;
		float exposure = 1.0f;
	} uniformData;

	struct FrameObjects : public VulkanFrameObjects {
		vks::Buffer uniformBuffer;
		VkDescriptorSet descriptorSet;
	};
	std::vector<FrameObjects> frameObjects;
	// The descriptors for the render targets and textures are static, and not required to be per-frame
	struct StaticDescriptorSets {
		VkDescriptorSet cubeMapImage;
		VkDescriptorSet offscreenImages;
		VkDescriptorSet compositionImages;
	} staticDescriptorSets;
	
	struct Pipelines {
		VkPipeline skybox;
		VkPipeline reflect;
		VkPipeline composition;
		VkPipeline bloom[2];
	} pipelines;

	struct PipelineLayouts {
		VkPipelineLayout models;
		VkPipelineLayout offscreenImages;
	} pipelineLayouts;

	struct DescriptorSetLayouts {
		VkDescriptorSetLayout uniformbuffers;
		VkDescriptorSetLayout cubeMapImage;
		VkDescriptorSetLayout offscreenImages;
	} descriptorSetLayouts;

	// Framebuffer for offscreen rendering
	struct FrameBufferAttachment {
		VkImage image;
		VkDeviceMemory memory;
		VkImageView view;
		VkFormat format;
		void destroy(VkDevice device)
		{
			vkDestroyImageView(device, view, nullptr);
			vkDestroyImage(device, image, nullptr);
			vkFreeMemory(device, memory, nullptr);
		}
	};
	struct OffscreenPass {
		int32_t width, height;
		VkFramebuffer frameBuffer;
		// This pass renders albedo and highlights (for the blur) in one pass into two different attachments
		FrameBufferAttachment albedo;
		FrameBufferAttachment highlights;
		FrameBufferAttachment depth;
		VkRenderPass renderPass;
	} offscreenPass;

	// Separable bloom filter pass objects
	struct FilterPass {
		VkFramebuffer frameBuffer;
		FrameBufferAttachment color;
		VkRenderPass renderPass;
	} filterPass;

	// Shared sampler used for all images
	VkSampler sampler;

	std::vector<std::string> objectNames;

	VulkanExample() : VulkanExampleBase(ENABLE_VALIDATION)
	{
		title = "High dynamic range rendering";
		camera.type = Camera::CameraType::lookat;
		camera.setPosition(glm::vec3(0.0f, 0.0f, -6.0f));
		camera.setRotation(glm::vec3(0.0f, 0.0f, 0.0f));
		camera.setPerspective(60.0f, (float)width / (float)height, 0.1f, 256.0f);
		settings.overlay = true;
	}

	~VulkanExample()
	{
		if (device) {
			vkDestroyPipeline(device, pipelines.skybox, nullptr);
			vkDestroyPipeline(device, pipelines.reflect, nullptr);
			vkDestroyPipeline(device, pipelines.composition, nullptr);
			vkDestroyPipeline(device, pipelines.bloom[0], nullptr);
			vkDestroyPipeline(device, pipelines.bloom[1], nullptr);
			vkDestroyPipelineLayout(device, pipelineLayouts.models, nullptr);
			vkDestroyPipelineLayout(device, pipelineLayouts.offscreenImages, nullptr);
			vkDestroyDescriptorSetLayout(device, descriptorSetLayouts.cubeMapImage, nullptr);
			vkDestroyDescriptorSetLayout(device, descriptorSetLayouts.uniformbuffers, nullptr);
			vkDestroyDescriptorSetLayout(device, descriptorSetLayouts.offscreenImages, nullptr);
			vkDestroyRenderPass(device, offscreenPass.renderPass, nullptr);
			vkDestroyRenderPass(device, filterPass.renderPass, nullptr);
			vkDestroyFramebuffer(device, offscreenPass.frameBuffer, nullptr);
			vkDestroyFramebuffer(device, filterPass.frameBuffer, nullptr);
			vkDestroySampler(device, sampler, nullptr);
			offscreenPass.depth.destroy(device);
			offscreenPass.albedo.destroy(device);
			offscreenPass.highlights.destroy(device);
			filterPass.color.destroy(device);
			cubemap.destroy();
			for (FrameObjects& frame : frameObjects) {
				frame.uniformBuffer.destroy();
				destroyBaseFrameObjects(frame);
			}
		}
	}

	void createAttachment(VkFormat format, VkImageUsageFlagBits usage, FrameBufferAttachment& attachment, VkExtent2D size)
	{
		VkImageAspectFlags aspectMask = 0;
		VkImageLayout imageLayout;

		attachment.format = format;

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
		image.extent.width = size.width;
		image.extent.height = size.height;
		image.extent.depth = 1;
		image.mipLevels = 1;
		image.arrayLayers = 1;
		image.samples = VK_SAMPLE_COUNT_1_BIT;
		image.tiling = VK_IMAGE_TILING_OPTIMAL;
		image.usage = usage | VK_IMAGE_USAGE_SAMPLED_BIT;
		VK_CHECK_RESULT(vkCreateImage(device, &image, nullptr, &attachment.image));

		VkMemoryAllocateInfo memAlloc = vks::initializers::memoryAllocateInfo();
		VkMemoryRequirements memReqs;
		vkGetImageMemoryRequirements(device, attachment.image, &memReqs);
		memAlloc.allocationSize = memReqs.size;
		memAlloc.memoryTypeIndex = vulkanDevice->getMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
		VK_CHECK_RESULT(vkAllocateMemory(device, &memAlloc, nullptr, &attachment.memory));
		VK_CHECK_RESULT(vkBindImageMemory(device, attachment.image, attachment.memory, 0));

		VkImageViewCreateInfo imageView = vks::initializers::imageViewCreateInfo();
		imageView.viewType = VK_IMAGE_VIEW_TYPE_2D;
		imageView.format = format;
		imageView.subresourceRange = {};
		imageView.subresourceRange.aspectMask = aspectMask;
		imageView.subresourceRange.baseMipLevel = 0;
		imageView.subresourceRange.levelCount = 1;
		imageView.subresourceRange.baseArrayLayer = 0;
		imageView.subresourceRange.layerCount = 1;
		imageView.image = attachment.image;
		VK_CHECK_RESULT(vkCreateImageView(device, &imageView, nullptr, &attachment.view));
	}

	// Prepare the framebuffers and attachments for offscreen rendering (G-Buffer)
	void createOffscreenObjects()
	{
		// We use a floating point format for the color attachments to store the high dynamic range values
		const VkFormat colorFormat = VK_FORMAT_R32G32B32A32_SFLOAT;

		{
			offscreenPass.width = width;
			offscreenPass.height = height;

			// Color attachments
			createAttachment(colorFormat, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, offscreenPass.albedo, { (uint32_t)offscreenPass.width, (uint32_t)offscreenPass.height });
			createAttachment(colorFormat, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, offscreenPass.highlights, { (uint32_t)offscreenPass.width, (uint32_t)offscreenPass.height });
			// Depth attachment
			createAttachment(depthFormat, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, offscreenPass.depth, { (uint32_t)offscreenPass.width, (uint32_t)offscreenPass.height });

			// Set up separate renderpass with references to the color and depth attachments
			std::array<VkAttachmentDescription, 3> attachmentDescs = {};

			// Init attachment properties
			for (uint32_t i = 0; i < 3; ++i)
			{
				attachmentDescs[i].samples = VK_SAMPLE_COUNT_1_BIT;
				attachmentDescs[i].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
				attachmentDescs[i].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
				attachmentDescs[i].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
				attachmentDescs[i].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
				if (i == 2)
				{
					attachmentDescs[i].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
					attachmentDescs[i].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
				}
				else
				{
					attachmentDescs[i].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
					attachmentDescs[i].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
				}
			}

			// Formats
			attachmentDescs[0].format = offscreenPass.albedo.format;
			attachmentDescs[1].format = offscreenPass.highlights.format;
			attachmentDescs[2].format = offscreenPass.depth.format;

			std::vector<VkAttachmentReference> colorReferences;
			colorReferences.push_back({ 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL });
			colorReferences.push_back({ 1, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL });

			VkAttachmentReference depthReference = {};
			depthReference.attachment = 2;
			depthReference.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

			VkSubpassDescription subpass = {};
			subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
			subpass.pColorAttachments = colorReferences.data();
			subpass.colorAttachmentCount = 2;
			subpass.pDepthStencilAttachment = &depthReference;

			// Use subpass dependencies for attachment layout transitions
			std::array<VkSubpassDependency, 2> dependencies;

			dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
			dependencies[0].dstSubpass = 0;
			dependencies[0].srcStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
			dependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
			dependencies[0].srcAccessMask = VK_ACCESS_MEMORY_READ_BIT;
			dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
			dependencies[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

			dependencies[1].srcSubpass = 0;
			dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
			dependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
			dependencies[1].dstStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
			dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
			dependencies[1].dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
			dependencies[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

			VkRenderPassCreateInfo renderPassInfo = {};
			renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
			renderPassInfo.pAttachments = attachmentDescs.data();
			renderPassInfo.attachmentCount = static_cast<uint32_t>(attachmentDescs.size());
			renderPassInfo.subpassCount = 1;
			renderPassInfo.pSubpasses = &subpass;
			renderPassInfo.dependencyCount = 2;
			renderPassInfo.pDependencies = dependencies.data();

			VK_CHECK_RESULT(vkCreateRenderPass(device, &renderPassInfo, nullptr, &offscreenPass.renderPass));

			std::array<VkImageView, 3> attachments;
			attachments[0] = offscreenPass.albedo.view;
			attachments[1] = offscreenPass.highlights.view;
			attachments[2] = offscreenPass.depth.view;

			VkFramebufferCreateInfo fbufCreateInfo = {};
			fbufCreateInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
			fbufCreateInfo.renderPass = offscreenPass.renderPass;
			fbufCreateInfo.pAttachments = attachments.data();
			fbufCreateInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
			fbufCreateInfo.width = offscreenPass.width;
			fbufCreateInfo.height = offscreenPass.height;
			fbufCreateInfo.layers = 1;
			VK_CHECK_RESULT(vkCreateFramebuffer(device, &fbufCreateInfo, nullptr, &offscreenPass.frameBuffer));
		}

		// Bloom separable filter pass
		{
			// High dynamic range color attachment using a floating point buffer
			createAttachment(colorFormat, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, filterPass.color, bloomExtent);

			// Set up separate renderpass with references to the color attachment
			std::array<VkAttachmentDescription, 1> attachmentDescs = {};

			// Init attachment properties
			attachmentDescs[0].samples = VK_SAMPLE_COUNT_1_BIT;
			attachmentDescs[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
			attachmentDescs[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
			attachmentDescs[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
			attachmentDescs[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
			attachmentDescs[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			attachmentDescs[0].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			attachmentDescs[0].format = filterPass.color.format;

			std::vector<VkAttachmentReference> colorReferences;
			colorReferences.push_back({ 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL });

			VkSubpassDescription subpass = {};
			subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
			subpass.pColorAttachments = colorReferences.data();
			subpass.colorAttachmentCount = 1;

			// Use subpass dependencies for attachment layout transitions
			std::array<VkSubpassDependency, 2> dependencies;

			dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
			dependencies[0].dstSubpass = 0;
			dependencies[0].srcStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
			dependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
			dependencies[0].srcAccessMask = VK_ACCESS_MEMORY_READ_BIT;
			dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
			dependencies[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

			dependencies[1].srcSubpass = 0;
			dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
			dependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
			dependencies[1].dstStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
			dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
			dependencies[1].dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
			dependencies[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

			VkRenderPassCreateInfo renderPassInfo = {};
			renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
			renderPassInfo.pAttachments = attachmentDescs.data();
			renderPassInfo.attachmentCount = static_cast<uint32_t>(attachmentDescs.size());
			renderPassInfo.subpassCount = 1;
			renderPassInfo.pSubpasses = &subpass;
			renderPassInfo.dependencyCount = 2;
			renderPassInfo.pDependencies = dependencies.data();

			VK_CHECK_RESULT(vkCreateRenderPass(device, &renderPassInfo, nullptr, &filterPass.renderPass));

			std::array<VkImageView, 1> attachments;
			attachments[0] = filterPass.color.view;

			VkFramebufferCreateInfo fbufCreateInfo = {};
			fbufCreateInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
			fbufCreateInfo.renderPass = filterPass.renderPass;
			fbufCreateInfo.pAttachments = attachments.data();
			fbufCreateInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
			fbufCreateInfo.width = bloomExtent.width;
			fbufCreateInfo.height = bloomExtent.height;
			fbufCreateInfo.layers = 1;
			VK_CHECK_RESULT(vkCreateFramebuffer(device, &fbufCreateInfo, nullptr, &filterPass.frameBuffer));
		}

		// Create a shared sampler for all color attachments (G-Buffer, bloom)
		VkSamplerCreateInfo samplerCI = vks::initializers::samplerCreateInfo();
		samplerCI.magFilter = VK_FILTER_LINEAR;
		samplerCI.minFilter = VK_FILTER_LINEAR;
		samplerCI.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
		samplerCI.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		samplerCI.addressModeV = samplerCI.addressModeU;
		samplerCI.addressModeW = samplerCI.addressModeU;
		samplerCI.mipLodBias = 0.0f;
		samplerCI.maxAnisotropy = 1.0f;
		samplerCI.minLod = 0.0f;
		samplerCI.maxLod = 1.0f;
		samplerCI.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
		VK_CHECK_RESULT(vkCreateSampler(device, &samplerCI, nullptr, &sampler));

		// Name some important Vulkan objects for debugging tools
		vks::debugmarker::setImageName(device, offscreenPass.albedo.image, "G-Buffer color");
		vks::debugmarker::setImageName(device, offscreenPass.highlights.image, "G-Buffer highlights");
		vks::debugmarker::setImageName(device, filterPass.color.image, "Bloom target");
	}

	void loadAssets()
	{
		// Load glTF models
		const uint32_t glTFLoadingFlags = vkglTF::FileLoadingFlags::PreTransformVertices | vkglTF::FileLoadingFlags::FlipY;
		models.skybox.loadFromFile(getAssetPath() + "models/cube.gltf", vulkanDevice, queue, glTFLoadingFlags);
		std::vector<std::string> filenames = { "sphere.gltf", "teapot.gltf", "torusknot.gltf", "venus.gltf" };
		objectNames = { "Sphere", "Teapot", "Torusknot", "Venus" };
		models.objects.resize(filenames.size());
		for (size_t i = 0; i < filenames.size(); i++) {
			models.objects[i].loadFromFile(getAssetPath() + "models/" + filenames[i], vulkanDevice, queue, glTFLoadingFlags);
		}
		// Load HDR cube map
		cubemap.loadFromFile(getAssetPath() + "textures/hdr/uffizi_cube.ktx", VK_FORMAT_R16G16B16A16_SFLOAT, vulkanDevice, queue);
	}

	void createDescriptors()
	{
		// Pool
		std::vector<VkDescriptorPoolSize> poolSizes = {
			vks::initializers::descriptorPoolSize(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, getFrameCount()),
			vks::initializers::descriptorPoolSize(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 5)
		};
		VkDescriptorPoolCreateInfo descriptorPoolInfo = vks::initializers::descriptorPoolCreateInfo(poolSizes, getFrameCount() + 3);
		VK_CHECK_RESULT(vkCreateDescriptorPool(device, &descriptorPoolInfo, nullptr, &descriptorPool));

		// Layouts
		VkDescriptorSetLayoutCreateInfo descriptorSetLayoutCI{};
		VkDescriptorSetLayoutBinding setLayoutBinding{};
		VkDescriptorSetAllocateInfo allocInfo{};
		VkWriteDescriptorSet writeDescriptorSet{};

		// Layout for the per-frame uniform buffers
		setLayoutBinding = vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0);
		descriptorSetLayoutCI = vks::initializers::descriptorSetLayoutCreateInfo(setLayoutBinding);
		VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &descriptorSetLayoutCI, nullptr, &descriptorSetLayouts.uniformbuffers));

		// Layout for the cube map image
		setLayoutBinding = vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 0);
		descriptorSetLayoutCI = vks::initializers::descriptorSetLayoutCreateInfo(setLayoutBinding);
		VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &descriptorSetLayoutCI, nullptr, &descriptorSetLayouts.cubeMapImage));

		// Layout for the offscreen images
		std::vector<VkDescriptorSetLayoutBinding> setLayoutBindings = {
			vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 0),
			vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 1),
		};
		descriptorSetLayoutCI = vks::initializers::descriptorSetLayoutCreateInfo(setLayoutBindings);
		VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &descriptorSetLayoutCI, nullptr, &descriptorSetLayouts.offscreenImages));

		// Sets
		// Per-frame for dynamic uniform buffers
		for (FrameObjects& frame : frameObjects) {
			allocInfo = vks::initializers::descriptorSetAllocateInfo(descriptorPool, &descriptorSetLayouts.uniformbuffers, 1);
			VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &allocInfo, &frame.descriptorSet));
			writeDescriptorSet = vks::initializers::writeDescriptorSet(frame.descriptorSet, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 0, &frame.uniformBuffer.descriptor);
			vkUpdateDescriptorSets(device, 1, &writeDescriptorSet, 0, nullptr);
		}

		// Global set for cube map
		allocInfo = vks::initializers::descriptorSetAllocateInfo(descriptorPool, &descriptorSetLayouts.cubeMapImage, 1);
		VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &allocInfo, &staticDescriptorSets.cubeMapImage));
		writeDescriptorSet = vks::initializers::writeDescriptorSet(staticDescriptorSets.cubeMapImage, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 0, &cubemap.descriptor);
		vkUpdateDescriptorSets(device, 1, &writeDescriptorSet, 0, nullptr);

		// Global sets for the offscreen images
		VkDescriptorImageInfo albedoDescriptor = vks::initializers::descriptorImageInfo(sampler, offscreenPass.albedo.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
		VkDescriptorImageInfo highlightsDescriptor = vks::initializers::descriptorImageInfo(sampler, offscreenPass.highlights.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
		VkDescriptorImageInfo filterDescriptor = vks::initializers::descriptorImageInfo(sampler, filterPass.color.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

		allocInfo = vks::initializers::descriptorSetAllocateInfo(descriptorPool, &descriptorSetLayouts.offscreenImages, 1);
		VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &allocInfo, &staticDescriptorSets.offscreenImages));
		std::vector<VkWriteDescriptorSet> writeDescriptorSets = {
			vks::initializers::writeDescriptorSet(staticDescriptorSets.offscreenImages, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 0, &albedoDescriptor),
			vks::initializers::writeDescriptorSet(staticDescriptorSets.offscreenImages, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, &highlightsDescriptor),
		};
		vkUpdateDescriptorSets(device, static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, nullptr);

		allocInfo = vks::initializers::descriptorSetAllocateInfo(descriptorPool, &descriptorSetLayouts.offscreenImages, 1);
		VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &allocInfo, &staticDescriptorSets.compositionImages));
		writeDescriptorSets = {
			vks::initializers::writeDescriptorSet(staticDescriptorSets.compositionImages, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 0, &albedoDescriptor),
			vks::initializers::writeDescriptorSet(staticDescriptorSets.compositionImages, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, &filterDescriptor),
		};
		vkUpdateDescriptorSets(device, static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, nullptr);
	}

	void createPipelines()
	{
		// Layouts
		// Layout with uniform buffer and single image
		std::vector<VkDescriptorSetLayout> setLayouts = { descriptorSetLayouts.uniformbuffers, descriptorSetLayouts.cubeMapImage };
		VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo = vks::initializers::pipelineLayoutCreateInfo(setLayouts.data(), 2);
		VK_CHECK_RESULT(vkCreatePipelineLayout(device, &pipelineLayoutCreateInfo, nullptr, &pipelineLayouts.models));
		// Layout with offscreen images
		setLayouts = { descriptorSetLayouts.offscreenImages };
		pipelineLayoutCreateInfo = vks::initializers::pipelineLayoutCreateInfo(setLayouts.data(), 1);
		VK_CHECK_RESULT(vkCreatePipelineLayout(device, &pipelineLayoutCreateInfo, nullptr, &pipelineLayouts.offscreenImages));

		// Pipelines
		VkPipelineInputAssemblyStateCreateInfo inputAssemblyStateCI = vks::initializers::pipelineInputAssemblyStateCreateInfo(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, 0, VK_FALSE);
		VkPipelineRasterizationStateCreateInfo rasterizationStateCI = vks::initializers::pipelineRasterizationStateCreateInfo(VK_POLYGON_MODE_FILL, VK_CULL_MODE_BACK_BIT, VK_FRONT_FACE_COUNTER_CLOCKWISE, 0);
		VkPipelineColorBlendAttachmentState blendAttachmentState = vks::initializers::pipelineColorBlendAttachmentState(0xf, VK_FALSE);
		VkPipelineColorBlendStateCreateInfo colorBlendStateCI = vks::initializers::pipelineColorBlendStateCreateInfo(1, &blendAttachmentState);
		VkPipelineDepthStencilStateCreateInfo depthStencilStateCI = vks::initializers::pipelineDepthStencilStateCreateInfo(VK_FALSE, VK_FALSE, VK_COMPARE_OP_LESS_OR_EQUAL);
		VkPipelineViewportStateCreateInfo viewportStateCI = vks::initializers::pipelineViewportStateCreateInfo(1, 1, 0);
		VkPipelineMultisampleStateCreateInfo multisampleStateCI = vks::initializers::pipelineMultisampleStateCreateInfo(VK_SAMPLE_COUNT_1_BIT, 0);
		std::vector<VkDynamicState> dynamicStateEnables = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
		VkPipelineDynamicStateCreateInfo dynamicStateCI = vks::initializers::pipelineDynamicStateCreateInfo(dynamicStateEnables);
		std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages;

		VkGraphicsPipelineCreateInfo pipelineCI = vks::initializers::pipelineCreateInfo();
		pipelineCI.renderPass = renderPass;
		pipelineCI.pInputAssemblyState = &inputAssemblyStateCI;
		pipelineCI.pRasterizationState = &rasterizationStateCI;
		pipelineCI.pColorBlendState = &colorBlendStateCI;
		pipelineCI.pMultisampleState = &multisampleStateCI;
		pipelineCI.pViewportState = &viewportStateCI;
		pipelineCI.pDepthStencilState = &depthStencilStateCI;
		pipelineCI.pDynamicState = &dynamicStateCI;
		pipelineCI.stageCount = static_cast<uint32_t>(shaderStages.size());
		pipelineCI.pStages = shaderStages.data();

		VkSpecializationInfo specializationInfo;
		std::array<VkSpecializationMapEntry, 1> specializationMapEntries;

		// Full screen pipelines

		// Empty vertex input state, full screen triangles are generated by the vertex shader
		VkPipelineVertexInputStateCreateInfo emptyInputState = vks::initializers::pipelineVertexInputStateCreateInfo();
		pipelineCI.pVertexInputState = &emptyInputState;

		// Final fullscreen composition pass pipeline
		std::vector<VkPipelineColorBlendAttachmentState> blendAttachmentStates = {
			vks::initializers::pipelineColorBlendAttachmentState(0xf, VK_FALSE),
			vks::initializers::pipelineColorBlendAttachmentState(0xf, VK_FALSE),
		};
		pipelineCI.layout = pipelineLayouts.offscreenImages;
		pipelineCI.renderPass = renderPass;
		rasterizationStateCI.cullMode = VK_CULL_MODE_NONE;
		colorBlendStateCI.attachmentCount = 1;
		colorBlendStateCI.pAttachments = blendAttachmentStates.data();
		shaderStages[0] = loadShader(getShadersPath() + "hdr/composition.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
		shaderStages[1] = loadShader(getShadersPath() + "hdr/composition.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
		VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCI, nullptr, &pipelines.composition));

		// Bloom post processing
		shaderStages[0] = loadShader(getShadersPath() + "hdr/bloom.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
		shaderStages[1] = loadShader(getShadersPath() + "hdr/bloom.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
		colorBlendStateCI.pAttachments = &blendAttachmentState;
		blendAttachmentState.colorWriteMask = 0xF;
		blendAttachmentState.blendEnable = VK_TRUE;
		blendAttachmentState.colorBlendOp = VK_BLEND_OP_ADD;
		blendAttachmentState.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
		blendAttachmentState.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
		blendAttachmentState.alphaBlendOp = VK_BLEND_OP_ADD;
		blendAttachmentState.srcAlphaBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
		blendAttachmentState.dstAlphaBlendFactor = VK_BLEND_FACTOR_DST_ALPHA;
		// Set constant parameters via specialization constants
		uint32_t dir{};
		specializationMapEntries[0] = vks::initializers::specializationMapEntry(0, 0, sizeof(uint32_t));
		specializationInfo = vks::initializers::specializationInfo(1, specializationMapEntries.data(), sizeof(dir), &dir);
		shaderStages[1].pSpecializationInfo = &specializationInfo;

		// First vertical bloom pass
		pipelineCI.renderPass = filterPass.renderPass;
		dir = 0;
		VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCI, nullptr, &pipelines.bloom[0]));

		// Second horizontal blur pass
		pipelineCI.renderPass = renderPass;
		dir = 1;
		VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCI, nullptr, &pipelines.bloom[1]));


		// Object rendering pipelines
		// Use vertex input state from glTF model setup
		pipelineCI.pVertexInputState = vkglTF::Vertex::getPipelineVertexInputState({ vkglTF::VertexComponent::Position, vkglTF::VertexComponent::Normal });

		blendAttachmentState.blendEnable = VK_FALSE;
		pipelineCI.layout = pipelineLayouts.models;
		pipelineCI.renderPass = offscreenPass.renderPass;
		colorBlendStateCI.attachmentCount = 2;
		colorBlendStateCI.pAttachments = blendAttachmentStates.data();
		shaderStages[0] = loadShader(getShadersPath() + "hdr/gbuffer.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
		shaderStages[1] = loadShader(getShadersPath() + "hdr/gbuffer.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
		// Set constant parameters via specialization constants
		specializationMapEntries[0] = vks::initializers::specializationMapEntry(0, 0, sizeof(uint32_t));
		uint32_t shadertype = 0;
		specializationInfo = vks::initializers::specializationInfo(1, specializationMapEntries.data(), sizeof(shadertype), &shadertype);
		shaderStages[0].pSpecializationInfo = &specializationInfo;
		shaderStages[1].pSpecializationInfo = &specializationInfo;
		// Skybox pipeline (background cube)
		rasterizationStateCI.cullMode = VK_CULL_MODE_FRONT_BIT;
		VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCI, nullptr, &pipelines.skybox));

		// Object rendering pipeline
		shadertype = 1;
		// Enable depth test and write
		depthStencilStateCI.depthWriteEnable = VK_TRUE;
		depthStencilStateCI.depthTestEnable = VK_TRUE;
		// Flip cull mode
		rasterizationStateCI.cullMode = VK_CULL_MODE_BACK_BIT;
		VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCI, nullptr, &pipelines.reflect));
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
		createOffscreenObjects();
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
		uniformData.modelview = camera.matrices.view;
		uniformData.inverseModelview = glm::inverse(camera.matrices.view);
		memcpy(currentFrame.uniformBuffer.mapped, &uniformData, sizeof(uniformData));

		// Build the command buffer
		const VkCommandBuffer commandBuffer = currentFrame.commandBuffer;
		const VkCommandBufferBeginInfo commandBufferBeginInfo = getCommandBufferBeginInfo();
		VK_CHECK_RESULT(vkBeginCommandBuffer(commandBuffer, &commandBufferBeginInfo));

		// First render pass:
		// Render scene to offscreen framebuffer, filling the color (albedo) and bloom highlights attachments
		{
			std::array<VkClearValue, 3> clearValues;
			clearValues[0].color = { { 0.0f, 0.0f, 0.0f, 0.0f } };
			clearValues[1].color = { { 0.0f, 0.0f, 0.0f, 0.0f } };
			clearValues[2].depthStencil = { 1.0f, 0 };

			VkRenderPassBeginInfo renderPassBeginInfo = vks::initializers::renderPassBeginInfo();
			renderPassBeginInfo.renderPass = offscreenPass.renderPass;
			renderPassBeginInfo.framebuffer = offscreenPass.frameBuffer;
			renderPassBeginInfo.renderArea.extent.width = offscreenPass.width;
			renderPassBeginInfo.renderArea.extent.height = offscreenPass.height;
			renderPassBeginInfo.clearValueCount = 3;
			renderPassBeginInfo.pClearValues = clearValues.data();

			VkViewport viewport = vks::initializers::viewport((float)offscreenPass.width, (float)offscreenPass.height, 0.0f, 1.0f);
			VkRect2D scissor = vks::initializers::rect2D(offscreenPass.width, offscreenPass.height, 0, 0);

			vkCmdBeginRenderPass(commandBuffer, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);
			vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
			vkCmdSetScissor(commandBuffer, 0, 1, &scissor);
			vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayouts.models, 0, 1, &frameObjects[0].descriptorSet, 0, nullptr);
			vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayouts.models, 1, 1, &staticDescriptorSets.cubeMapImage, 0, nullptr);
			// Skybox
			if (displaySkybox) {
				vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.skybox);
				models.skybox.draw(commandBuffer);
			}
			// 3D object
			vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayouts.models, 0, 1, &frameObjects[0].descriptorSet, 0, nullptr);
			vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.reflect);
			models.objects[models.objectIndex].draw(commandBuffer);
			vkCmdEndRenderPass(commandBuffer);
		}

		// Second render pass: 
		// First bloom pass
		if (bloom) {
			VkClearValue clearValues[2];
			clearValues[0].color = { { 0.0f, 0.0f, 0.0f, 0.0f } };
			clearValues[1].depthStencil = { 1.0f, 0 };

			// Bloom filter
			VkRenderPassBeginInfo renderPassBeginInfo = vks::initializers::renderPassBeginInfo();
			renderPassBeginInfo.framebuffer = filterPass.frameBuffer;
			renderPassBeginInfo.renderPass = filterPass.renderPass;
			renderPassBeginInfo.clearValueCount = 1;
			renderPassBeginInfo.renderArea.extent = bloomExtent;
			renderPassBeginInfo.pClearValues = clearValues;

			VkRect2D scissor = vks::initializers::rect2D(bloomExtent.width, bloomExtent.height, 0, 0);
			VkViewport viewport = vks::initializers::viewport((float)bloomExtent.width, (float)bloomExtent.height, 0.0f, 1.0f);

			vkCmdBeginRenderPass(commandBuffer, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);
			vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
			vkCmdSetScissor(commandBuffer, 0, 1, &scissor);
			vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayouts.offscreenImages, 0, 1, &staticDescriptorSets.offscreenImages, 0, nullptr);
			vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.bloom[0]);
			vkCmdDraw(commandBuffer, 3, 1, 0, 0);
			vkCmdEndRenderPass(commandBuffer);
		}

		// Third render pass: 
		// Scene rendering with applied second bloom pass (when enabled)
		// Note: Explicit synchronization is not required between the render pass, as this is done implicit via sub pass dependencies
		{
			const VkRect2D renderArea = getRenderArea();
			const VkViewport viewport = getViewport();
			const VkRenderPassBeginInfo renderPassBeginInfo = getRenderPassBeginInfo(renderPass, defaultClearValues);

			vkCmdBeginRenderPass(commandBuffer, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);
			vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
			vkCmdSetScissor(commandBuffer, 0, 1, &renderArea);
			vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayouts.offscreenImages, 0, 1, &staticDescriptorSets.compositionImages, 0, nullptr);
			// Draw the scene from the offscreen color attachment using a fullscreen quad
			vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.composition);
			vkCmdDraw(commandBuffer, 3, 1, 0, 0);
			// Blend bloom image on top of the scene
			if (bloom) {
				vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.bloom[1]);
				vkCmdDraw(commandBuffer, 3, 1, 0, 0);
			}
			drawUI(commandBuffer);
			vkCmdEndRenderPass(commandBuffer);
		}

		VK_CHECK_RESULT(vkEndCommandBuffer(commandBuffer));

		VulkanExampleBase::submitFrame(currentFrame);
	}

	virtual void OnUpdateUIOverlay(vks::UIOverlay *overlay)
	{
		if (overlay->header("Settings")) {
			overlay->comboBox("Object type", &models.objectIndex, objectNames);
			overlay->inputFloat("Exposure", &uniformData.exposure, 0.025f, 3);
			overlay->checkBox("Bloom", &bloom);
			overlay->checkBox("Skybox", &displaySkybox);
		}
	}
};

VULKAN_EXAMPLE_MAIN()
