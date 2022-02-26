/*
 * Vulkan Example - Indirect drawing
 *
 * Copyright (C) 2016-2022 by Sascha Willems - www.saschawillems.de
 *
 * This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
 *
 * Summary:
 * Use a device local buffer that stores draw commands for instanced rendering of different meshes stored
 * in the same buffer.
 *
 * Indirect drawing offloads draw command generation and offers the ability to update them on the GPU
 * without the CPU having to touch the buffer again, also reducing the number of drawcalls.
 *
 * The example shows how to setup and fill such a buffer on the CPU side, stages it to the device and
 * shows how to render it using only one draw command.
 *
 * See readme.md for details
 *
 */

/* 
 * @todo
 */

#include "vulkanexamplebase.h"
#include "VulkanglTFModel.h"

#define ENABLE_VALIDATION false

// Number of instances per object
#if defined(__ANDROID__)
#define OBJECT_INSTANCE_COUNT 1024
// Circular range of plant distribution
#define PLANT_RADIUS 20.0f
#else
#define OBJECT_INSTANCE_COUNT 2048
// Circular range of plant distribution
#define PLANT_RADIUS 25.0f
#endif

class VulkanExample : public VulkanExampleBase
{
public:
	struct Textures {
		vks::Texture2DArray plants;
		vks::Texture2D ground;
	} textures;

	struct Models {
		vkglTF::Model plants;
		vkglTF::Model ground;
		vkglTF::Model skysphere;
	} models;

	// Per-instance data block
	struct InstanceData {
		glm::vec3 pos;
		glm::vec3 rot;
		float scale;
		uint32_t texIndex;
	};

	// Contains the instanced data
	vks::Buffer instanceBuffer;
	// Contains the indirect drawing commands
	vks::Buffer indirectCommandsBuffer;
	uint32_t indirectDrawCount;

	struct UniformData {
		glm::mat4 projection;
		glm::mat4 view;
	} uniformData;

	struct FrameObjects : public VulkanFrameObjects {
		vks::Buffer uniformBuffer;
		VkDescriptorSet descriptorSet;
	};
	std::vector<FrameObjects> frameObjects;

	struct Pipelines {
		VkPipeline plants;
		VkPipeline ground;
		VkPipeline skysphere;
	} pipelines;

	VkPipelineLayout pipelineLayout;

	struct DescriptorSetLayouts {
		VkDescriptorSetLayout uniformbuffers;
		VkDescriptorSetLayout images;
	} descriptorSetLayouts;
	// The descriptor set for the images is static, and not required to be per-frame
	VkDescriptorSet imagesDescriptorSet;

	VkSampler samplerRepeat;

	uint32_t objectCount = 0;

	// Store the indirect draw commands containing index offsets and instance count per object
	std::vector<VkDrawIndexedIndirectCommand> indirectCommands;

	VulkanExample() : VulkanExampleBase(ENABLE_VALIDATION)
	{
		title = "Indirect rendering";
		camera.setType(Camera::CameraType::firstperson);
		camera.setPerspective(60.0f, (float)width / (float)height, 0.1f, 512.0f);
		camera.setRotation(glm::vec3(-12.0f, 159.0f, 0.0f));
		camera.setTranslation(glm::vec3(0.4f, 1.25f, 0.0f));
		camera.setMovementSpeed(5.0f);
		settings.overlay = true;
	}

	~VulkanExample()
	{
		if (device) {
			vkDestroyPipeline(device, pipelines.plants, nullptr);
			vkDestroyPipeline(device, pipelines.ground, nullptr);
			vkDestroyPipeline(device, pipelines.skysphere, nullptr);
			vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
			vkDestroyDescriptorSetLayout(device, descriptorSetLayouts.uniformbuffers, nullptr);
			vkDestroyDescriptorSetLayout(device, descriptorSetLayouts.images, nullptr);
			textures.plants.destroy();
			textures.ground.destroy();
			instanceBuffer.destroy();
			indirectCommandsBuffer.destroy();
			for (FrameObjects& frame : frameObjects) {
				frame.uniformBuffer.destroy();
				destroyBaseFrameObjects(frame);
			}
		}
	}

	virtual void getEnabledFeatures()
	{
		// Example uses multi draw indirect if available to lower the number of draw calls
		enabledFeatures.multiDrawIndirect = deviceFeatures.multiDrawIndirect;
		// Enable anisotropic filtering if supported
		enabledFeatures.samplerAnisotropy = deviceFeatures.samplerAnisotropy;
	};

	void loadAssets()
	{
		const uint32_t glTFLoadingFlags = vkglTF::FileLoadingFlags::PreTransformVertices | vkglTF::FileLoadingFlags::PreMultiplyVertexColors | vkglTF::FileLoadingFlags::FlipY;
		models.plants.loadFromFile(getAssetPath() + "models/plants.gltf", vulkanDevice, queue, glTFLoadingFlags);
		models.ground.loadFromFile(getAssetPath() + "models/plane_circle.gltf", vulkanDevice, queue, glTFLoadingFlags);
		models.skysphere.loadFromFile(getAssetPath() + "models/sphere.gltf", vulkanDevice, queue, glTFLoadingFlags);
		textures.plants.loadFromFile(getAssetPath() + "textures/texturearray_plants_rgba.ktx", VK_FORMAT_R8G8B8A8_UNORM, vulkanDevice, queue);
		textures.ground.loadFromFile(getAssetPath() + "textures/ground_dry_rgba.ktx", VK_FORMAT_R8G8B8A8_UNORM, vulkanDevice, queue);
	}

	void createDescriptors()
	{
		// Pool
		std::vector<VkDescriptorPoolSize> poolSizes = {
			vks::initializers::descriptorPoolSize(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, getFrameCount()),
			vks::initializers::descriptorPoolSize(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2),
		};
		VkDescriptorPoolCreateInfo descriptorPoolInfo = vks::initializers::descriptorPoolCreateInfo(poolSizes, getFrameCount() + 1);
		VK_CHECK_RESULT(vkCreateDescriptorPool(device, &descriptorPoolInfo, nullptr, &descriptorPool));

		// Layouts
		std::vector<VkDescriptorSetLayoutBinding> setLayoutBindings;
		VkDescriptorSetLayoutBinding setLayoutBinding{};
		VkDescriptorSetLayoutCreateInfo descriptorSetLayoutCI{};
		// One layout for the per-frame uniform buffers
		setLayoutBinding = vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0);
		descriptorSetLayoutCI = vks::initializers::descriptorSetLayoutCreateInfo(setLayoutBinding);
		VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &descriptorSetLayoutCI, nullptr, &descriptorSetLayouts.uniformbuffers));
		// One layout for the images used in the scene
		setLayoutBindings = {
			vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 0),
			vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 1),
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
		// Global set for the images used in the scene
		VkDescriptorSetAllocateInfo allocInfo = vks::initializers::descriptorSetAllocateInfo(descriptorPool, &descriptorSetLayouts.images, 1);
		VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &allocInfo, &imagesDescriptorSet));
		std::vector<VkWriteDescriptorSet> writeDescriptorSets = {
			// Binding 0 contains the texture array with all plant textures
			vks::initializers::writeDescriptorSet(imagesDescriptorSet, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 0, &textures.plants.descriptor),
			vks::initializers::writeDescriptorSet(imagesDescriptorSet, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, &textures.ground.descriptor),
		};
		vkUpdateDescriptorSets(device, static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, nullptr);


	}

	void createPipelines()
	{
		// Layout 
		const std::vector<VkDescriptorSetLayout> setLayouts = { descriptorSetLayouts.uniformbuffers, descriptorSetLayouts.images };
		VkPipelineLayoutCreateInfo pipelineLayoutCI = vks::initializers::pipelineLayoutCreateInfo(setLayouts.data(), 2);
		VK_CHECK_RESULT(vkCreatePipelineLayout(device, &pipelineLayoutCI, nullptr, &pipelineLayout));

		// Pipelines
		VkPipelineInputAssemblyStateCreateInfo inputAssemblyState = vks::initializers::pipelineInputAssemblyStateCreateInfo(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, 0, VK_FALSE);
		VkPipelineRasterizationStateCreateInfo rasterizationState = vks::initializers::pipelineRasterizationStateCreateInfo(VK_POLYGON_MODE_FILL, VK_CULL_MODE_NONE, VK_FRONT_FACE_COUNTER_CLOCKWISE, 0);
		VkPipelineColorBlendAttachmentState blendAttachmentState = vks::initializers::pipelineColorBlendAttachmentState(0xf, VK_FALSE);
		VkPipelineColorBlendStateCreateInfo colorBlendState = vks::initializers::pipelineColorBlendStateCreateInfo(1, &blendAttachmentState);
		VkPipelineDepthStencilStateCreateInfo depthStencilState = vks::initializers::pipelineDepthStencilStateCreateInfo(VK_TRUE, VK_TRUE, VK_COMPARE_OP_LESS_OR_EQUAL);
		VkPipelineViewportStateCreateInfo viewportState = vks::initializers::pipelineViewportStateCreateInfo(1, 1, 0);
		VkPipelineMultisampleStateCreateInfo multisampleState = vks::initializers::pipelineMultisampleStateCreateInfo(VK_SAMPLE_COUNT_1_BIT, 0);
		std::vector<VkDynamicState> dynamicStateEnables = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
		VkPipelineDynamicStateCreateInfo dynamicState = vks::initializers::pipelineDynamicStateCreateInfo(dynamicStateEnables);
		std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages;

		VkGraphicsPipelineCreateInfo pipelineCI = vks::initializers::pipelineCreateInfo(pipelineLayout, renderPass);
		pipelineCI.pInputAssemblyState = &inputAssemblyState;
		pipelineCI.pRasterizationState = &rasterizationState;
		pipelineCI.pColorBlendState = &colorBlendState;
		pipelineCI.pMultisampleState = &multisampleState;
		pipelineCI.pViewportState = &viewportState;
		pipelineCI.pDepthStencilState = &depthStencilState;
		pipelineCI.pDynamicState = &dynamicState;
		pipelineCI.stageCount = static_cast<uint32_t>(shaderStages.size());
		pipelineCI.pStages = shaderStages.data();

		// This example uses two different input states, one for the instanced part and one for non-instanced rendering
		VkPipelineVertexInputStateCreateInfo inputState = vks::initializers::pipelineVertexInputStateCreateInfo();
		std::vector<VkVertexInputBindingDescription> bindingDescriptions;
		std::vector<VkVertexInputAttributeDescription> attributeDescriptions;

		// Vertex input bindings
		// The instancing pipeline uses a vertex input state with two bindings
		bindingDescriptions = {
		    // Binding point 0: Mesh vertex layout description at per-vertex rate
		    vks::initializers::vertexInputBindingDescription(0, sizeof(vkglTF::Vertex), VK_VERTEX_INPUT_RATE_VERTEX),
		    // Binding point 1: Instanced data at per-instance rate
		    vks::initializers::vertexInputBindingDescription(1, sizeof(InstanceData), VK_VERTEX_INPUT_RATE_INSTANCE)
		};

		// Vertex attribute bindings
		// Note that the shader declaration for per-vertex and per-instance attributes is the same, the different input rates are only stored in the bindings:
		// instanced.vert:
		//	layout (location = 0) in vec3 inPos;		Per-Vertex
		//	...
		//	layout (location = 4) in vec3 instancePos;	Per-Instance
		attributeDescriptions = {
		    // Per-vertex attributes
		    // These are advanced for each vertex fetched by the vertex shader
		    vks::initializers::vertexInputAttributeDescription(0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0),								// Location 0: Position
		    vks::initializers::vertexInputAttributeDescription(0, 1, VK_FORMAT_R32G32B32_SFLOAT, sizeof(float) * 3),				// Location 1: Normal
		    vks::initializers::vertexInputAttributeDescription(0, 2, VK_FORMAT_R32G32_SFLOAT, sizeof(float) * 6),					// Location 2: Texture coordinates
		    vks::initializers::vertexInputAttributeDescription(0, 3, VK_FORMAT_R32G32B32_SFLOAT, sizeof(float) * 8),				// Location 3: Color
		    // Per-Instance attributes
		    // These are fetched for each instance rendered
		    vks::initializers::vertexInputAttributeDescription(1, 4, VK_FORMAT_R32G32B32_SFLOAT, offsetof(InstanceData, pos)),	// Location 4: Position
		    vks::initializers::vertexInputAttributeDescription(1, 5, VK_FORMAT_R32G32B32_SFLOAT, offsetof(InstanceData, rot)),	// Location 5: Rotation
		    vks::initializers::vertexInputAttributeDescription(1, 6, VK_FORMAT_R32_SFLOAT, offsetof(InstanceData, scale)),		// Location 6: Scale
		    vks::initializers::vertexInputAttributeDescription(1, 7, VK_FORMAT_R32_SINT, offsetof(InstanceData, texIndex)),		// Location 7: Texture array layer index
		};
		inputState.pVertexBindingDescriptions = bindingDescriptions.data();
		inputState.pVertexAttributeDescriptions = attributeDescriptions.data();
		inputState.vertexBindingDescriptionCount   = static_cast<uint32_t>(bindingDescriptions.size());
		inputState.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size());

		pipelineCI.pVertexInputState = &inputState;

		// Indirect (and instanced) pipeline for the plants
		shaderStages[0] = loadShader(getShadersPath() + "indirectdraw/indirectdraw.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
		shaderStages[1] = loadShader(getShadersPath() + "indirectdraw/indirectdraw.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
		VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCI, nullptr, &pipelines.plants));

		// Only use non-instanced vertex attributes for models rendered without instancing
		inputState.vertexBindingDescriptionCount = 1;
		inputState.vertexAttributeDescriptionCount = 4;

		// Ground
		shaderStages[0] = loadShader(getShadersPath() + "indirectdraw/ground.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
		shaderStages[1] = loadShader(getShadersPath() + "indirectdraw/ground.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
		rasterizationState.cullMode = VK_CULL_MODE_BACK_BIT;
		VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCI, nullptr, &pipelines.ground));

		// Skysphere
		shaderStages[0] = loadShader(getShadersPath() + "indirectdraw/skysphere.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
		shaderStages[1] = loadShader(getShadersPath() + "indirectdraw/skysphere.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
		depthStencilState.depthWriteEnable = VK_FALSE;
		rasterizationState.cullMode = VK_CULL_MODE_FRONT_BIT;
		VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCI, nullptr, &pipelines.skysphere));
	}

	// Prepare (and stage) a buffer containing the indirect draw commands
	void createIndirectBuffer()
	{
		indirectCommands.clear();

		// @todo: comment
		// Create one indirect command for node in the scene with a mesh attached to it
		uint32_t m = 0;
		for (auto &node : models.plants.nodes)
		{
			if (node->mesh)
			{
				VkDrawIndexedIndirectCommand indirectCmd{};
				indirectCmd.instanceCount = OBJECT_INSTANCE_COUNT;
				indirectCmd.firstInstance = m * OBJECT_INSTANCE_COUNT;
				// @todo: Multiple primitives
				// A glTF node may consist of multiple primitives, so we may have to do multiple commands per mesh
				indirectCmd.firstIndex = node->mesh->primitives[0]->firstIndex;
				indirectCmd.indexCount = node->mesh->primitives[0]->indexCount;

				indirectCommands.push_back(indirectCmd);

				m++;
			}
		}

		indirectDrawCount = static_cast<uint32_t>(indirectCommands.size());

		objectCount = 0;
		for (auto indirectCmd : indirectCommands)
		{
			objectCount += indirectCmd.instanceCount;
		}

		// Copy the buffer to the GPU
		vks::Buffer stagingBuffer;
		VK_CHECK_RESULT(vulkanDevice->createBuffer(
			VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
			&stagingBuffer,
			indirectCommands.size() * sizeof(VkDrawIndexedIndirectCommand),
			indirectCommands.data()));
		VK_CHECK_RESULT(vulkanDevice->createBuffer(
			VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
			&indirectCommandsBuffer,
			stagingBuffer.size));
		vulkanDevice->copyBuffer(&stagingBuffer, &indirectCommandsBuffer, queue);
		stagingBuffer.destroy();
	}

	// Prepare (and stage) a buffer containing instanced data for the mesh draws
	void createInstanceBuffer()
	{
		std::vector<InstanceData> instanceData;
		instanceData.resize(objectCount);

		std::default_random_engine rndEngine(benchmark.active ? 0 : (unsigned)time(nullptr));
		std::uniform_real_distribution<float> uniformDist(0.0f, 1.0f);

		for (uint32_t i = 0; i < objectCount; i++) {
			float theta = 2 * float(M_PI) * uniformDist(rndEngine);
			float phi = acos(1 - 2 * uniformDist(rndEngine));
			instanceData[i].rot = glm::vec3(0.0f, float(M_PI) * uniformDist(rndEngine), 0.0f);
			instanceData[i].pos = glm::vec3(sin(phi) * cos(theta), 0.0f, cos(phi)) * PLANT_RADIUS;
			instanceData[i].scale = 1.0f + uniformDist(rndEngine) * 2.0f;
			instanceData[i].texIndex = i / OBJECT_INSTANCE_COUNT;
		}

		vks::Buffer stagingBuffer;
		VK_CHECK_RESULT(vulkanDevice->createBuffer(
			VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
			&stagingBuffer,
			instanceData.size() * sizeof(InstanceData),
			instanceData.data()));

		VK_CHECK_RESULT(vulkanDevice->createBuffer(
			VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
			&instanceBuffer,
			stagingBuffer.size));

		vulkanDevice->copyBuffer(&stagingBuffer, &instanceBuffer, queue);

		stagingBuffer.destroy();
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
		createIndirectBuffer();
		createInstanceBuffer();
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
		memcpy(currentFrame.uniformBuffer.mapped, &uniformData, sizeof(uniformData));

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

		// @todo: comment
		vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &currentFrame.descriptorSet, 0, nullptr);
		vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 1, 1, &imagesDescriptorSet, 0, nullptr);

		// Skysphere
		vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.skysphere);
		models.skysphere.draw(commandBuffer);
		// Ground
		vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.ground);
		models.ground.draw(commandBuffer);

		VkDeviceSize offsets[1] = { 0 };
		
		// Start the instanced multi draw rendering of the plants
		vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.plants);
		
		// Binding point 0 : Mesh vertex buffer
		vkCmdBindVertexBuffers(commandBuffer, 0, 1, &models.plants.vertices.buffer, offsets);
		// Binding point 1 : Instance data buffer
		vkCmdBindVertexBuffers(commandBuffer, 1, 1, &instanceBuffer.buffer, offsets);

		vkCmdBindIndexBuffer(commandBuffer, models.plants.indices.buffer, 0, VK_INDEX_TYPE_UINT32);

		if (vulkanDevice->features.multiDrawIndirect) {
			// If the multi draw feature is supported we can draw all objects with a single call being sourced from the indirect buffer
			vkCmdDrawIndexedIndirect(commandBuffer, indirectCommandsBuffer.buffer, 0, indirectDrawCount, sizeof(VkDrawIndexedIndirectCommand));
		} else {
			// If multi draw is not available, we must issue separate draw commands
			for (auto j = 0; j < indirectCommands.size(); j++) {
				vkCmdDrawIndexedIndirect(commandBuffer, indirectCommandsBuffer.buffer, j * sizeof(VkDrawIndexedIndirectCommand), 1, sizeof(VkDrawIndexedIndirectCommand));
			}
		}

		drawUI(commandBuffer);
		vkCmdEndRenderPass(commandBuffer);
		VK_CHECK_RESULT(vkEndCommandBuffer(commandBuffer));

		VulkanExampleBase::submitFrame(currentFrame);
	}

	virtual void OnUpdateUIOverlay(vks::UIOverlay *overlay)
	{
		if (!vulkanDevice->features.multiDrawIndirect) {
			if (overlay->header("Info")) {
				overlay->text("multiDrawIndirect not supported");
			}
		}
		if (overlay->header("Statistics")) {
			overlay->text("Objects: %d", objectCount);
		}
	}
};

VULKAN_EXAMPLE_MAIN()