/*
 * Vulkan Example - Using descriptor sets for passing dynamic and static data to shaders
 *
 * Copyright (C) 2018-2022 by Sascha Willems - www.saschawillems.de
 *
 * This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
 */

/*
 * This sample shows how to use descriptor sets for displaying multiple animated objects
 * It demonstrates per-frame descriptor sets for dynamically changing data (rotations, camera) as well as static sets for images
 */

#include "vulkanexamplebase.h"
#include "VulkanglTFModel.h"

#define ENABLE_VALIDATION false

class VulkanExample : public VulkanExampleBase
{
public:
	bool animate = true;

	struct Cube {
		glm::vec3 position{};
		glm::vec3 rotation{};
		void init(glm::vec3 pos, glm::vec3 rot) {
			position = pos;
			rotation = rot;
		}
	};
	std::array<Cube, 2> cubes;

	vkglTF::Model model;
	std::array<vks::Texture2D, 2> textures;

	// To keep things simple, we put both gobal matrices (projection and view) and local (model) matrices into one structure
	struct UniformData {
		glm::mat4 projection;
		glm::mat4 view;
		glm::mat4 model;
	};

	// Dynamic objects need to be duplicated per frame so we can have frames in flight
	struct FrameObjects : public VulkanFrameObjects {
		std::array<vks::Buffer, 2> uniformBuffers;
		std::array<VkDescriptorSet, 2> descriptorSets;
	};
	std::vector<FrameObjects> frameObjects;

	VkPipeline pipeline;
	VkPipelineLayout pipelineLayout;
	VkDescriptorSetLayout descriptorSetLayout;

	VulkanExample() : VulkanExampleBase(ENABLE_VALIDATION)
	{
		title = "Using descriptor Sets";
		settings.overlay = true;
		camera.setType(Camera::CameraType::lookat);
		camera.setPerspective(60.0f, (float)width / (float)height, 0.1f, 512.0f);
		camera.setRotation(glm::vec3(0.0f, 0.0f, 0.0f));
		camera.setTranslation(glm::vec3(0.0f, 0.0f, -5.0f));
		// Init two cubes
		cubes[0].init(glm::vec3(-2.0f, 0.0f, 0.0f), glm::vec3(0.0f));
		cubes[1].init(glm::vec3(1.5f, 0.5f, 0.0f), glm::vec3(0.0f));
	}

	~VulkanExample()
	{
		if (device) {
			vkDestroyPipeline(device, pipeline, nullptr);
			vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
			vkDestroyDescriptorSetLayout(device, descriptorSetLayout, nullptr);
			for (FrameObjects& frame : frameObjects) {
				for (size_t i = 0; i < cubes.size(); i++) {
					frame.uniformBuffers[i].destroy();
				}
				destroyBaseFrameObjects(frame);
			}
			for (auto texture : textures) {
				texture.destroy();
			}
		}
	}

	virtual void getEnabledFeatures()
	{
		if (deviceFeatures.samplerAnisotropy) {
			enabledFeatures.samplerAnisotropy = VK_TRUE;
		};
	}

	void loadAssets()
	{
		const uint32_t glTFLoadingFlags = vkglTF::FileLoadingFlags::PreTransformVertices | vkglTF::FileLoadingFlags::PreMultiplyVertexColors | vkglTF::FileLoadingFlags::FlipY;
		model.loadFromFile(getAssetPath() + "models/cube.gltf", vulkanDevice, queue, glTFLoadingFlags);
		textures[0].loadFromFile(getAssetPath() + "textures/crate01_color_height_rgba.ktx", VK_FORMAT_R8G8B8A8_UNORM, vulkanDevice, queue);
		textures[1].loadFromFile(getAssetPath() + "textures/crate02_color_height_rgba.ktx", VK_FORMAT_R8G8B8A8_UNORM, vulkanDevice, queue);
	}

	/** Set up descriptor sets and set layout */
	void createDescriptors()
	{
		// Descriptor pool
		// Actual descriptors are allocated from a descriptor pool telling the driver what types and how many descriptors this application will use
		// An application can have multiple pools (e.g. for multiple threads) with any number of descriptor types as long as device limits are not surpassed
		std::array<VkDescriptorPoolSize, 2> descriptorPoolSizes{};

		// Uniform buffers : One per cube per frame
		descriptorPoolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		descriptorPoolSizes[0].descriptorCount = 1 + static_cast<uint32_t>(cubes.size()) * getFrameCount();

		// Combined image samples : One per cube per frame
		descriptorPoolSizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		descriptorPoolSizes[1].descriptorCount = static_cast<uint32_t>(cubes.size()) * getFrameCount();

		// Create the global descriptor pool
		VkDescriptorPoolCreateInfo descriptorPoolCI = {};
		descriptorPoolCI.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
		descriptorPoolCI.poolSizeCount = static_cast<uint32_t>(descriptorPoolSizes.size());
		descriptorPoolCI.pPoolSizes = descriptorPoolSizes.data();
		// Max. number of descriptor sets that can be allocated from this pool
		// We need one per cube per frame
		descriptorPoolCI.maxSets = static_cast<uint32_t>(cubes.size() * getFrameCount());

		VK_CHECK_RESULT(vkCreateDescriptorPool(device, &descriptorPoolCI, nullptr, &descriptorPool));

		// Descriptor set layout
		// The layout describes the shader bindings and types used for a certain descriptor layout and as such must match the shader bindings
		// Shader bindings used in this example:
		// Vertex shader:
		//	layout (set = 0, binding = 0) uniform UBOMatrices ...
		// Fragment shader:
		//	layout (set = 0, binding = 1) uniform sampler2D ...;
		std::array<VkDescriptorSetLayoutBinding,2> setLayoutBindings{};

		// Binding 0: Uniform buffers (used to pass matrices)
		setLayoutBindings[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		// Shader binding point
		setLayoutBindings[0].binding = 0;
		// Accessible from the vertex shader only (flags can be combined to make it accessible to multiple shader stages)
		setLayoutBindings[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
		// Binding contains one element (can be used for array bindings)
		setLayoutBindings[0].descriptorCount = 1;

		// Binding 1: Combined image sampler (used to pass per object texture information)
		setLayoutBindings[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		setLayoutBindings[1].binding = 1;
		// Accessible from the fragment shader only
		setLayoutBindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
		setLayoutBindings[1].descriptorCount = 1;

		// Create the descriptor set layout
		VkDescriptorSetLayoutCreateInfo descriptorLayoutCI{};
		descriptorLayoutCI.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
		descriptorLayoutCI.bindingCount = static_cast<uint32_t>(setLayoutBindings.size());
		descriptorLayoutCI.pBindings = setLayoutBindings.data();

		VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &descriptorLayoutCI, nullptr, &descriptorSetLayout));

		// Descriptor sets
		// Using the shared descriptor set layout and the descriptor pool we will now allocate the descriptor sets.
		// Descriptor sets contain the actual descriptor for the objects (buffers, images) used at render time.
		// We create one set per object per frame, so we can update uniform buffers for frame B while frame A is still in flight.
		for (FrameObjects& frame : frameObjects) {
			for (size_t i = 0; i < cubes.size(); i++) {
				// Allocates an empty descriptor set without actual descriptors from the pool using the set layout
				VkDescriptorSetAllocateInfo allocateInfo{};
				allocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
				allocateInfo.descriptorPool = descriptorPool;
				allocateInfo.descriptorSetCount = 1;
				allocateInfo.pSetLayouts = &descriptorSetLayout;
				VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &allocateInfo, &frame.descriptorSets[i]));

				// Update the descriptor set with the actual descriptors matching shader bindings set in the layout

				std::array<VkWriteDescriptorSet, 2> writeDescriptorSets{};

				// Binding 0: Uniform buffer containing the matrices for the current cube and frame
				writeDescriptorSets[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
				writeDescriptorSets[0].dstSet = frame.descriptorSets[i];
				writeDescriptorSets[0].dstBinding = 0;
				writeDescriptorSets[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
				writeDescriptorSets[0].pBufferInfo = &frame.uniformBuffers[i].descriptor;
				writeDescriptorSets[0].descriptorCount = 1;

				// Binding 1: Image containing the texture for the current cube, same for all frames
				writeDescriptorSets[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
				writeDescriptorSets[1].dstSet = frame.descriptorSets[i];
				writeDescriptorSets[1].dstBinding = 1;
				writeDescriptorSets[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
				// Images use a different descriptor structure, so we use pImageInfo instead of pBufferInfo
				writeDescriptorSets[1].pImageInfo = &textures[i].descriptor;
				writeDescriptorSets[1].descriptorCount = 1;

				// Execute the writes to update descriptors for this set
				// Note that it's also possible to gather all writes and only run updates once, even for multiple sets
				// This is possible because each VkWriteDescriptorSet also contains the destination set to be updated
				// For simplicity we will update once per set instead
				vkUpdateDescriptorSets(device, static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, nullptr);
			}
		}

	}

	void createPipelines()
	{
		// [POI] Create a pipeline layout used for our graphics pipeline
		VkPipelineLayoutCreateInfo pipelineLayoutCI{};
		pipelineLayoutCI.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
		// The pipeline layout is based on the descriptor set layout shared for all cubes we created earlier
		pipelineLayoutCI.setLayoutCount = 1;
		pipelineLayoutCI.pSetLayouts = &descriptorSetLayout;
		VK_CHECK_RESULT(vkCreatePipelineLayout(device, &pipelineLayoutCI, nullptr, &pipelineLayout));

		// Create the pipeline
		VkPipelineInputAssemblyStateCreateInfo inputAssemblyStateCI = vks::initializers::pipelineInputAssemblyStateCreateInfo(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, 0, VK_FALSE);
		VkPipelineRasterizationStateCreateInfo rasterizationStateCI = vks::initializers::pipelineRasterizationStateCreateInfo(VK_POLYGON_MODE_FILL, VK_CULL_MODE_BACK_BIT, VK_FRONT_FACE_COUNTER_CLOCKWISE, 0);
		VkPipelineColorBlendAttachmentState blendAttachmentState = vks::initializers::pipelineColorBlendAttachmentState(0xf, VK_FALSE);
		VkPipelineColorBlendStateCreateInfo colorBlendStateCI = vks::initializers::pipelineColorBlendStateCreateInfo(1, &blendAttachmentState);
		VkPipelineDepthStencilStateCreateInfo depthStencilStateCI = vks::initializers::pipelineDepthStencilStateCreateInfo(VK_TRUE, VK_TRUE, VK_COMPARE_OP_LESS_OR_EQUAL);
		VkPipelineViewportStateCreateInfo viewportStateCI = vks::initializers::pipelineViewportStateCreateInfo(1, 1, 0);
		VkPipelineMultisampleStateCreateInfo multisampleStateCI = vks::initializers::pipelineMultisampleStateCreateInfo(VK_SAMPLE_COUNT_1_BIT, 0);
		const std::vector<VkDynamicState> dynamicStateEnables = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
		VkPipelineDynamicStateCreateInfo dynamicStateCI = vks::initializers::pipelineDynamicStateCreateInfo(dynamicStateEnables.data(), static_cast<uint32_t>(dynamicStateEnables.size()),0);
		std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages;

		VkGraphicsPipelineCreateInfo pipelineCI = vks::initializers::pipelineCreateInfo(pipelineLayout, renderPass, 0);
		pipelineCI.pInputAssemblyState = &inputAssemblyStateCI;
		pipelineCI.pRasterizationState = &rasterizationStateCI;
		pipelineCI.pColorBlendState = &colorBlendStateCI;
		pipelineCI.pMultisampleState = &multisampleStateCI;
		pipelineCI.pViewportState = &viewportStateCI;
		pipelineCI.pDepthStencilState = &depthStencilStateCI;
		pipelineCI.pDynamicState = &dynamicStateCI;
		pipelineCI.stageCount = static_cast<uint32_t>(shaderStages.size());
		pipelineCI.pStages = shaderStages.data();
		pipelineCI.pVertexInputState  = vkglTF::Vertex::getPipelineVertexInputState({vkglTF::VertexComponent::Position, vkglTF::VertexComponent::Normal, vkglTF::VertexComponent::UV, vkglTF::VertexComponent::Color});

	    shaderStages[0] = loadShader(getShadersPath() + "descriptorsets/cube.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
		shaderStages[1] = loadShader(getShadersPath() + "descriptorsets/cube.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
		VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCI, nullptr, &pipeline));
	}

	void prepare()
	{
		VulkanExampleBase::prepare();
		// Prepare per-frame resources
		frameObjects.resize(getFrameCount());
		for (FrameObjects& frame : frameObjects) {
			createBaseFrameObjects(frame);
			// Create one uniform buffer per frame per cube
			// An alternative to this would be creating only one buffer per frame and using dynamic offsets per cube (see the dynamic uniform buffer for such an example)
			for (size_t i = 0; i < cubes.size(); i++) {
				VK_CHECK_RESULT(vulkanDevice->createAndMapBuffer(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &frame.uniformBuffers[i], sizeof(UniformData)));
			}
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

		// Update uniform data for the next frame
		if (!paused) {
			// Animate the cubes at different rates
			if (animate) {
				cubes[0].rotation.x += 2.5f * frameTimer;
				if (cubes[0].rotation.x > 360.0f) {
					cubes[0].rotation.x -= 360.0f;
				}
				cubes[1].rotation.y += 2.0f * frameTimer;
				if (cubes[1].rotation.x > 360.0f) {
					cubes[1].rotation.x -= 360.0f;
				}
			}
			// Update the uniform buffers
			for (size_t i = 0; i < cubes.size(); i++) {
				UniformData uniformData{};
				// Position
				uniformData.model = glm::translate(glm::mat4(1.0f), cubes[i].position);
				uniformData.projection = camera.matrices.perspective;
				uniformData.view = camera.matrices.view;
				// Rotation
				uniformData.model = glm::rotate(uniformData.model, glm::radians(cubes[i].rotation.x), glm::vec3(1.0f, 0.0f, 0.0f));
				uniformData.model = glm::rotate(uniformData.model, glm::radians(cubes[i].rotation.y), glm::vec3(0.0f, 1.0f, 0.0f));
				uniformData.model = glm::rotate(uniformData.model, glm::radians(cubes[i].rotation.z), glm::vec3(0.0f, 0.0f, 1.0f));
				uniformData.model = glm::scale(uniformData.model, glm::vec3(0.25f));
				memcpy(currentFrame.uniformBuffers[i].mapped, &uniformData, sizeof(uniformData));
			}
		}

		// Build the command buffer
		const VkCommandBuffer commandBuffer = currentFrame.commandBuffer;
		const VkCommandBufferBeginInfo commandBufferBeginInfo = getCommandBufferBeginInfo();
		const VkRect2D renderArea = getRenderArea();
		const VkViewport viewport = getViewport();
		const VkRenderPassBeginInfo renderPassBeginInfo = getRenderPassBeginInfo(renderPass, defaultClearValues);
		VK_CHECK_RESULT(vkBeginCommandBuffer(commandBuffer, &commandBufferBeginInfo));
		vkCmdBeginRenderPass(commandBuffer, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);
		vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
		vkCmdSetScissor(commandBuffer, 0, 1, &renderArea);
		vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

		// Render the cubes with separate descriptor sets
		for (size_t i = 0; i < cubes.size(); i++) {
			// Bind the cube's descriptor set for the current frame. 
			// This tells the command buffer to use the uniform buffer and image set for this cube and frame
			vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &currentFrame.descriptorSets[i], 0, nullptr);
			model.draw(commandBuffer);
		}

		drawUI(commandBuffer);
		vkCmdEndRenderPass(commandBuffer);
		VK_CHECK_RESULT(vkEndCommandBuffer(commandBuffer));

		VulkanExampleBase::submitFrame(currentFrame);
	}

	virtual void OnUpdateUIOverlay(vks::UIOverlay *overlay)
	{
		if (overlay->header("Settings")) {
			overlay->checkBox("Animate", &animate);
		}
	}
};

VULKAN_EXAMPLE_MAIN()