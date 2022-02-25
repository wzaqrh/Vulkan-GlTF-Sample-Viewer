/*
 * Vulkan Example - Using input attachments
 *
 * Copyright (C) 2016-2022 by Sascha Willems - www.saschawillems.de
 *
 * This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
 */

 /*
  * This samples shows how to use input attachments. The first sub pass writes to color and depth attachments.
  * These are passed to the second sub pass as input attachments from which the sub pass reads. 
  * Reads with input attachment can't be random and are always read at the same pixel location of the generating sub pass ("pixel local storage").
  * See createDescriptors for how input attachments and check the render function to see how multiple sub passes work.
  */

#include "vulkanexamplebase.h"
#include "VulkanglTFModel.h"

#define ENABLE_VALIDATION false

class VulkanExample : public VulkanExampleBase
{
public:
	vkglTF::Model scene;

	struct UniformData {
		glm::mat4 projection;
		glm::mat4 model;
		glm::mat4 view;
		glm::vec2 brightnessContrast = glm::vec2(0.5f, 1.8f);
		glm::vec2 range = glm::vec2(0.6f, 1.0f);
		int32_t attachmentIndex = 1;
	} uniformData;

	struct FrameObjects : public VulkanFrameObjects {
		vks::Buffer uniformBuffer;
		VkDescriptorSet descriptorSet;
	};
	std::vector<FrameObjects> frameObjects;
	// Only one set of input attachments is used, so no need to have the descriptor per-frame
	VkDescriptorSet inputAttachmentsDescriptorSet;

	struct Pipelines {
		VkPipeline attachmentWrite;
		VkPipeline attachmentRead;
	} pipelines;

	struct PipelineLayouts {
		VkPipelineLayout attachmentWrite;
		VkPipelineLayout attachmentRead;
	} pipelineLayouts;

	struct DescriptorSetLayouts {
		VkDescriptorSetLayout uniformbuffers;
		VkDescriptorSetLayout attachmentRead;
	} descriptorSetLayouts;

	struct FrameBufferAttachment {
		VkImage image = VK_NULL_HANDLE;
		VkDeviceMemory memory = VK_NULL_HANDLE;
		VkImageView view = VK_NULL_HANDLE;
		VkFormat format;
	};
	struct Attachments {
		FrameBufferAttachment color, depth;
	} attachments;
	VkExtent2D attachmentSize;

	const VkFormat colorFormat = VK_FORMAT_R8G8B8A8_UNORM;

	VulkanExample() : VulkanExampleBase(ENABLE_VALIDATION)
	{
		title = "Input attachments";
		camera.type = Camera::CameraType::firstperson;
		camera.movementSpeed = 2.5f;
		camera.setPosition(glm::vec3(1.65f, 1.75f, -6.15f));
		camera.setRotation(glm::vec3(-12.75f, 380.0f, 0.0f));
		camera.setPerspective(60.0f, (float)width / (float)height, 0.1f, 256.0f);
		settings.overlay = true;
		// This sample uses multiple subpasses, so we need to let the UI overlay know that it's rendered in the second sub pass
		UIOverlay.subpass = 1;
	}

	~VulkanExample()
	{
		if (device) {
			vkDestroyImageView(device, attachments.color.view, nullptr);
			vkDestroyImage(device, attachments.color.image, nullptr);
			vkFreeMemory(device, attachments.color.memory, nullptr);
			vkDestroyImageView(device, attachments.depth.view, nullptr);
			vkDestroyImage(device, attachments.depth.image, nullptr);
			vkFreeMemory(device, attachments.depth.memory, nullptr);
			vkDestroyPipeline(device, pipelines.attachmentRead, nullptr);
			vkDestroyPipeline(device, pipelines.attachmentWrite, nullptr);
			vkDestroyPipelineLayout(device, pipelineLayouts.attachmentWrite, nullptr);
			vkDestroyPipelineLayout(device, pipelineLayouts.attachmentRead, nullptr);
			vkDestroyDescriptorSetLayout(device, descriptorSetLayouts.attachmentRead, nullptr);
			vkDestroyDescriptorSetLayout(device, descriptorSetLayouts.uniformbuffers, nullptr);
			for (FrameObjects& frame : frameObjects) {
				frame.uniformBuffer.destroy();
				destroyBaseFrameObjects(frame);
			}
		}
	}

	// Create a frame buffer attachment
	void createAttachment(VkFormat format, VkImageUsageFlags usage, FrameBufferAttachment *attachment)
	{
		VkImageAspectFlags aspectMask = 0;
		VkImageLayout imageLayout;

		attachment->format = format;

		if (usage & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) {
			aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		}
		if (usage & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT) {
			aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
			imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
		}

		VkImageCreateInfo imageCI = vks::initializers::imageCreateInfo();
		imageCI.imageType = VK_IMAGE_TYPE_2D;
		imageCI.format = format;
		imageCI.extent.width = width;
		imageCI.extent.height = height;
		imageCI.extent.depth = 1;
		imageCI.mipLevels = 1;
		imageCI.arrayLayers = 1;
		imageCI.samples = VK_SAMPLE_COUNT_1_BIT;
		imageCI.tiling = VK_IMAGE_TILING_OPTIMAL;
		// VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT flag is required for input attachments;
		imageCI.usage = usage | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT;
		imageCI.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		VK_CHECK_RESULT(vkCreateImage(device, &imageCI, nullptr, &attachment->image));

		VkMemoryAllocateInfo memAlloc = vks::initializers::memoryAllocateInfo();
		VkMemoryRequirements memReqs;
		vkGetImageMemoryRequirements(device, attachment->image, &memReqs);
		memAlloc.allocationSize = memReqs.size;
		memAlloc.memoryTypeIndex = vulkanDevice->getMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
		VK_CHECK_RESULT(vkAllocateMemory(device, &memAlloc, nullptr, &attachment->memory));
		VK_CHECK_RESULT(vkBindImageMemory(device, attachment->image, attachment->memory, 0));

		VkImageViewCreateInfo imageViewCI = vks::initializers::imageViewCreateInfo();
		imageViewCI.viewType = VK_IMAGE_VIEW_TYPE_2D;
		imageViewCI.format = format;
		imageViewCI.subresourceRange = {};
		imageViewCI.subresourceRange.aspectMask = aspectMask;
		imageViewCI.subresourceRange.baseMipLevel = 0;
		imageViewCI.subresourceRange.levelCount = 1;
		imageViewCI.subresourceRange.baseArrayLayer = 0;
		imageViewCI.subresourceRange.layerCount = 1;
		imageViewCI.image = attachment->image;
		VK_CHECK_RESULT(vkCreateImageView(device, &imageViewCI, nullptr, &attachment->view));
	}

	void clearAttachment(FrameBufferAttachment* attachment)
	{
		vkDestroyImageView(device, attachment->view, nullptr);
		vkDestroyImage(device, attachment->image, nullptr);
		vkFreeMemory(device, attachment->memory, nullptr);
	}

	// Override framebuffer setup from base class
	void setupFrameBuffer()
	{
		// If the window is resized, all the framebuffers/attachments used in our composition passes need to be recreated
		if (attachmentSize.width != width || attachmentSize.height != height)
		{
			attachmentSize = { width, height };
			clearAttachment(&attachments.color);
			clearAttachment(&attachments.depth);
			createAttachment(colorFormat, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, &attachments.color);
			createAttachment(depthFormat, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, &attachments.depth);
			// Since the framebuffers/attachments are referred in the descriptor sets, these need to be updated too
			updateAttachmentReadDescriptors();
		}

		VkImageView views[3];

		VkFramebufferCreateInfo frameBufferCI{};
		frameBufferCI.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
		frameBufferCI.renderPass = renderPass;
		frameBufferCI.attachmentCount = 3;
		frameBufferCI.pAttachments = views;
		frameBufferCI.width = width;
		frameBufferCI.height = height;
		frameBufferCI.layers = 1;

		frameBuffers.resize(swapChain.imageCount);
		for (uint32_t i = 0; i < frameBuffers.size(); i++)
		{
			views[0] = swapChain.buffers[i].view;
			views[1] = attachments.color.view;
			views[2] = attachments.depth.view;
			VK_CHECK_RESULT(vkCreateFramebuffer(device, &frameBufferCI, nullptr, &frameBuffers[i]));
		}
	}

	// Override render pass setup from base class
	void setupRenderPass()
	{
		attachmentSize = { width, height };

		createAttachment(colorFormat, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, &attachments.color);
		createAttachment(depthFormat, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, &attachments.depth);

		std::array<VkAttachmentDescription, 3> attachments{};

		// Swap chain image color attachment
		// Will be transitioned to present layout
		attachments[0].format = swapChain.colorFormat;
		attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
		attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		attachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		attachments[0].finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

		// Input attachments
		// These will be written in the first subpass, transitioned to input attachments
		// and then read in the secod subpass

		// Color
		attachments[1].format = colorFormat;
		attachments[1].samples = VK_SAMPLE_COUNT_1_BIT;
		attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		attachments[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		attachments[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		attachments[1].finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		// Depth
		attachments[2].format = depthFormat;
		attachments[2].samples = VK_SAMPLE_COUNT_1_BIT;
		attachments[2].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		attachments[2].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		attachments[2].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		attachments[2].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		attachments[2].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		attachments[2].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

		std::array<VkSubpassDescription,2> subpassDescriptions{};

		// First subpass: Fill the color and depth attachments
		VkAttachmentReference colorReference = { 1, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
		VkAttachmentReference depthReference = { 2, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };

		subpassDescriptions[0].pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
		subpassDescriptions[0].colorAttachmentCount = 1;
		subpassDescriptions[0].pColorAttachments = &colorReference;
		subpassDescriptions[0].pDepthStencilAttachment = &depthReference;

		// Second subpass: Input attachment read and swap chain color attachment write

		// Color reference (target) for this sub pass is the swap chain color attachment
		VkAttachmentReference colorReferenceSwapchain = { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };

		subpassDescriptions[1].pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
		subpassDescriptions[1].colorAttachmentCount = 1;
		subpassDescriptions[1].pColorAttachments = &colorReferenceSwapchain;

		// Color and depth attachment written to in first sub pass will be used as input attachments to be read in the fragment shader
		VkAttachmentReference inputReferences[2];
		inputReferences[0] = { 1, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
		inputReferences[1] = { 2, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };

		// Use the attachments filled in the first pass as input attachments
		subpassDescriptions[1].inputAttachmentCount = 2;
		subpassDescriptions[1].pInputAttachments = inputReferences;

		// Subpass dependencies for layout transitions
		std::array<VkSubpassDependency, 3> dependencies;

		dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
		dependencies[0].dstSubpass = 0;
		dependencies[0].srcStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
		dependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		dependencies[0].srcAccessMask = VK_ACCESS_MEMORY_READ_BIT;
		dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		dependencies[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

		// This dependency transitions the input attachment from color attachment to shader read
		dependencies[1].srcSubpass = 0;
		dependencies[1].dstSubpass = 1;
		dependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		dependencies[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
		dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		dependencies[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
		dependencies[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

		dependencies[2].srcSubpass = 0;
		dependencies[2].dstSubpass = VK_SUBPASS_EXTERNAL;
		dependencies[2].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		dependencies[2].dstStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
		dependencies[2].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		dependencies[2].dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
		dependencies[2].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

		VkRenderPassCreateInfo renderPassInfoCI{};
		renderPassInfoCI.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
		renderPassInfoCI.attachmentCount = static_cast<uint32_t>(attachments.size());
		renderPassInfoCI.pAttachments = attachments.data();
		renderPassInfoCI.subpassCount = static_cast<uint32_t>(subpassDescriptions.size());
		renderPassInfoCI.pSubpasses = subpassDescriptions.data();
		renderPassInfoCI.dependencyCount = static_cast<uint32_t>(dependencies.size());
		renderPassInfoCI.pDependencies = dependencies.data();
		VK_CHECK_RESULT(vkCreateRenderPass(device, &renderPassInfoCI, nullptr, &renderPass));
	}

	void loadAssets()
	{
		const uint32_t glTFLoadingFlags = vkglTF::FileLoadingFlags::PreTransformVertices | vkglTF::FileLoadingFlags::PreMultiplyVertexColors | vkglTF::FileLoadingFlags::FlipY;
		scene.loadFromFile(getAssetPath() + "models/treasure_smooth.gltf", vulkanDevice, queue, glTFLoadingFlags);
	}

	void updateAttachmentReadDescriptors()
	{
		// Image descriptors for the input attachments read by the shader
		std::vector<VkDescriptorImageInfo> descriptors = {
			vks::initializers::descriptorImageInfo(VK_NULL_HANDLE, attachments.color.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL),
			vks::initializers::descriptorImageInfo(VK_NULL_HANDLE, attachments.depth.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
		};
		std::vector<VkWriteDescriptorSet> writeDescriptorSets = {
			// Binding 0: Color input attachment
			vks::initializers::writeDescriptorSet(inputAttachmentsDescriptorSet, VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 0, &descriptors[0]),
			// Binding 1: Depth input attachment
			vks::initializers::writeDescriptorSet(inputAttachmentsDescriptorSet, VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1, &descriptors[1]),
		};
		vkUpdateDescriptorSets(device, static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, nullptr);
	}

	void createDescriptors()
	{
		// Pool
		std::vector<VkDescriptorPoolSize> poolSizes = {
			vks::initializers::descriptorPoolSize(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, getFrameCount()),
			vks::initializers::descriptorPoolSize(VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 2),
		};
		VkDescriptorPoolCreateInfo descriptorPoolInfo = vks::initializers::descriptorPoolCreateInfo(poolSizes, getFrameCount() + 1);
		VK_CHECK_RESULT(vkCreateDescriptorPool(device, &descriptorPoolInfo, nullptr, &descriptorPool));

		// Layouts
		VkDescriptorSetLayoutCreateInfo descriptorSetLayoutCI{};
		VkDescriptorSetLayoutBinding setLayoutBinding{};

		// Layout for the per-frame uniform buffers
		setLayoutBinding = vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0);
		descriptorSetLayoutCI = vks::initializers::descriptorSetLayoutCreateInfo(setLayoutBinding);
		VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &descriptorSetLayoutCI, nullptr, &descriptorSetLayouts.uniformbuffers));

		// Sets
		VkDescriptorSetAllocateInfo allocInfo{};

		// Per-frame for dynamic uniform buffers
		for (FrameObjects& frame : frameObjects) {
			allocInfo = vks::initializers::descriptorSetAllocateInfo(descriptorPool, &descriptorSetLayouts.uniformbuffers, 1);
			VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &allocInfo, &frame.descriptorSet));
			VkWriteDescriptorSet writeDescriptorSet = vks::initializers::writeDescriptorSet(frame.descriptorSet, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 0, &frame.uniformBuffer.descriptor);
			vkUpdateDescriptorSets(device, 1, &writeDescriptorSet, 0, nullptr);
		}

		// Attachment read using color and/or depth as input attachments
		std::vector<VkDescriptorSetLayoutBinding> setLayoutBindings = {
			// Binding 0: Color input attachment
			vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, VK_SHADER_STAGE_FRAGMENT_BIT, 0),
			// Binding 1: Depth input attachment
			vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, VK_SHADER_STAGE_FRAGMENT_BIT, 1),
		};
		VkDescriptorSetLayoutCreateInfo descriptorLayoutCI = vks::initializers::descriptorSetLayoutCreateInfo(setLayoutBindings);
		VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &descriptorLayoutCI, nullptr, &descriptorSetLayouts.attachmentRead));

		allocInfo = vks::initializers::descriptorSetAllocateInfo(descriptorPool, &descriptorSetLayouts.attachmentRead, 1);
		VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &allocInfo, &inputAttachmentsDescriptorSet));
		
		updateAttachmentReadDescriptors();
	}

	void createPipelines()
	{
		// Layouts
		// Layout for reading the color and depth attachments as input attachments in the scene composition pass
		std::vector<VkDescriptorSetLayout> setLayouts = { descriptorSetLayouts.attachmentRead, descriptorSetLayouts.uniformbuffers };
		VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo = vks::initializers::pipelineLayoutCreateInfo(setLayouts.data(), 2);
		VK_CHECK_RESULT(vkCreatePipelineLayout(device, &pipelineLayoutCreateInfo, nullptr, &pipelineLayouts.attachmentRead));
		// Layout for rendering to the color and depth attachments, only requires a uniform buffer, attachment information is passed via the render pass
		pipelineLayoutCreateInfo = vks::initializers::pipelineLayoutCreateInfo(&descriptorSetLayouts.uniformbuffers, 1);
		VK_CHECK_RESULT(vkCreatePipelineLayout(device, &pipelineLayoutCreateInfo, nullptr, &pipelineLayouts.attachmentWrite));

		// Pipelines
		VkPipelineInputAssemblyStateCreateInfo inputAssemblyStateCI = vks::initializers::pipelineInputAssemblyStateCreateInfo(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, 0, VK_FALSE);
		VkPipelineRasterizationStateCreateInfo rasterizationStateCI = vks::initializers::pipelineRasterizationStateCreateInfo(VK_POLYGON_MODE_FILL, VK_CULL_MODE_BACK_BIT, VK_FRONT_FACE_COUNTER_CLOCKWISE, 0);
		VkPipelineColorBlendAttachmentState blendAttachmentState = vks::initializers::pipelineColorBlendAttachmentState(0xf, VK_FALSE);
		VkPipelineColorBlendStateCreateInfo colorBlendStateCI = vks::initializers::pipelineColorBlendStateCreateInfo(1, &blendAttachmentState);
		VkPipelineDepthStencilStateCreateInfo depthStencilStateCI = vks::initializers::pipelineDepthStencilStateCreateInfo(VK_TRUE, VK_TRUE, VK_COMPARE_OP_LESS_OR_EQUAL);
		VkPipelineViewportStateCreateInfo viewportStateCI = vks::initializers::pipelineViewportStateCreateInfo(1, 1, 0);
		VkPipelineMultisampleStateCreateInfo multisampleStateCI = vks::initializers::pipelineMultisampleStateCreateInfo(VK_SAMPLE_COUNT_1_BIT, 0);
		std::vector<VkDynamicState> dynamicStateEnables = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
		VkPipelineDynamicStateCreateInfo dynamicStateCI = vks::initializers::pipelineDynamicStateCreateInfo(dynamicStateEnables);
		VkGraphicsPipelineCreateInfo pipelineCI = vks::initializers::pipelineCreateInfo();
		std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages;

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

		// Attachment write
		// Pipeline will be used in first sub pass
		pipelineCI.subpass = 0;
		pipelineCI.layout = pipelineLayouts.attachmentWrite;
		pipelineCI.pVertexInputState = vkglTF::Vertex::getPipelineVertexInputState({vkglTF::VertexComponent::Position, vkglTF::VertexComponent::Color, vkglTF::VertexComponent::Normal});
		shaderStages[0] = loadShader(getShadersPath() + "inputattachments/attachmentwrite.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
		shaderStages[1] = loadShader(getShadersPath() + "inputattachments/attachmentwrite.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
		VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCI, nullptr, &pipelines.attachmentWrite));

		// Attachment read
		// Pipeline will be used in second sub pass
		pipelineCI.subpass = 1;
		pipelineCI.layout = pipelineLayouts.attachmentRead;
		// Empty pipeline state, as this pipeline only draws a full-screen triangle generated in the vertex shader
		VkPipelineVertexInputStateCreateInfo emptyInputStateCI{};
		emptyInputStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
		pipelineCI.pVertexInputState = &emptyInputStateCI;
		colorBlendStateCI.attachmentCount = 1;
		rasterizationStateCI.cullMode = VK_CULL_MODE_NONE;
		depthStencilStateCI.depthWriteEnable = VK_FALSE;
		shaderStages[0] = loadShader(getShadersPath() + "inputattachments/attachmentread.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
		shaderStages[1] = loadShader(getShadersPath() + "inputattachments/attachmentread.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
		VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCI, nullptr, &pipelines.attachmentRead));
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
		memcpy(currentFrame.uniformBuffer.mapped, &uniformData, sizeof(uniformData));

		// Build the command buffer
		const VkCommandBuffer commandBuffer = currentFrame.commandBuffer;
		const VkCommandBufferBeginInfo commandBufferBeginInfo = getCommandBufferBeginInfo();
		VK_CHECK_RESULT(vkBeginCommandBuffer(commandBuffer, &commandBufferBeginInfo));

		VkClearValue clearValues[3];
		clearValues[0].color = { { 0.0f, 0.0f, 0.2f, 0.0f } };
		clearValues[1].color = { { 0.0f, 0.0f, 0.2f, 0.0f } };
		clearValues[2].depthStencil = { 1.0f, 0 };
		const VkRenderPassBeginInfo renderPassBeginInfo = getRenderPassBeginInfo(renderPass, clearValues, 3);
		vkCmdBeginRenderPass(commandBuffer, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

		const VkRect2D renderArea = getRenderArea();
		const VkViewport viewport = getViewport();
		vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
		vkCmdSetScissor(commandBuffer, 0, 1, &renderArea);

		// First sub pass: Fills the attachments
		vks::debugmarker::beginRegion(commandBuffer, "Subpass 0: Writing attachments", glm::vec4(1.0f, 1.0f, 1.0f, 1.0f));
		vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.attachmentWrite);
		vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayouts.attachmentWrite, 0, 1, &currentFrame.descriptorSet, 0, nullptr);
		scene.draw(commandBuffer);
		vks::debugmarker::endRegion(commandBuffer);

		// Second sub pass: Render a full screen quad, reading from the previously written attachments as input attachments
		vkCmdNextSubpass(commandBuffer, VK_SUBPASS_CONTENTS_INLINE);
		vks::debugmarker::beginRegion(commandBuffer, "Subpass 1: Reading attachments", glm::vec4(1.0f, 1.0f, 1.0f, 1.0f));
		vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.attachmentRead);
		vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayouts.attachmentRead, 0, 1, &inputAttachmentsDescriptorSet, 0, nullptr);
		vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayouts.attachmentRead, 1, 1, &currentFrame.descriptorSet, 0, nullptr);
		vkCmdDraw(commandBuffer, 3, 1, 0, 0);
		vks::debugmarker::endRegion(commandBuffer);

		drawUI(commandBuffer);

		vkCmdEndRenderPass(commandBuffer);

		VK_CHECK_RESULT(vkEndCommandBuffer(commandBuffer));

		VulkanExampleBase::submitFrame(currentFrame);
	}

	virtual void OnUpdateUIOverlay(vks::UIOverlay *overlay)
	{
		if (overlay->header("Settings")) {
			overlay->text("Input attachment");
			overlay->comboBox("##attachment", &uniformData.attachmentIndex, { "color", "depth" });
			switch (uniformData.attachmentIndex) {
			case 0:
				overlay->text("Brightness");
				overlay->sliderFloat("##b", &uniformData.brightnessContrast[0], 0.0f, 2.0f);
				overlay->text("Contrast");
				overlay->sliderFloat("##c", &uniformData.brightnessContrast[1], 0.0f, 4.0f);
				break;
			case 1:
				overlay->text("Visible range");
				overlay->sliderFloat("min", &uniformData.range[0], 0.0f, uniformData.range[1]);
				overlay->sliderFloat("max", &uniformData.range[1], uniformData.range[0], 1.0f);
				break;
			}
		}
	}
};

VULKAN_EXAMPLE_MAIN()
