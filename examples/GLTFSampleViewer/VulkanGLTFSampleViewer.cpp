#include "VulkanGLTFSampleViewer.h"

VulkanGLTFSampleViewer::VulkanGLTFSampleViewer() 
{
	title = "homework1";
	camera.type = Camera::CameraType::lookat;
	camera.flipY = true;
	camera.setPosition(glm::vec3(0.0f, 0.0f, -1.0f));
	camera.setRotation(glm::vec3(0.0f, 45.0f, 0.0f));
	camera.setPerspective(45, (float)width / (float)height, 0.1f, 256.0f);
	camera.movementSpeed = 0.1;
	camera.rotationSpeed = 0.1;
	timerSpeed = 1.0;

	settings.validation = true;

	modelShaderName_ = {
		"gltf.vert",
		"gltf2.frag"
	};
	skyboxShaderName_ = {
		"sky.vert",
		"sky.frag"
	};

	modelName_ = "ToyCar";
	enviromentName_ = "neutral";
}
void MultiSampleTarget::destroy(VkDevice device)
{
	vkSafeDestroyImage(device, this->color.image, nullptr);
	vkSafeDestroyImageView(device, this->color.view, nullptr);
	vkSafeFreeMemory(device, this->color.memory, nullptr);
	
	vkSafeDestroyImage(device, this->depth.image, nullptr);
	vkSafeDestroyImageView(device, this->depth.view, nullptr);
	vkSafeFreeMemory(device, this->depth.memory, nullptr);
}
VulkanGLTFSampleViewer::~VulkanGLTFSampleViewer()
{
	vkSafeDestroyPipelineLayout(this->device, skyboxPipelineLayout_, nullptr);
	vkSafeDestroyPipeline(this->device, skyboxPipeline_.solid, nullptr);
	vkSafeDestroyPipeline(this->device, skyboxPipeline_.wireframe, nullptr);
	vkSafeDestroyPipeline(this->device, skyboxLinearPipeline_.solid, nullptr);
	vkSafeDestroyPipeline(this->device, skyboxLinearPipeline_.wireframe, nullptr);

	vkSafeDestroyPipelineLayout(this->device, modelPipelineLayout_, nullptr);
	for (auto& it : modelPipelineByConstant_) {
		vkSafeDestroyPipeline(this->device, it.second.solid, nullptr);
		vkSafeDestroyPipeline(this->device, it.second.wireframe, nullptr);
	}
	modelPipelineByConstant_.clear();
	
	multisampleTarget_.destroy(this->device);
	if (opaqueFramebuffer_) opaqueFramebuffer_->destroy();

	if (model_) model_->destroy();
	if (skyBox_) skyBox_->destroy();

	if (enviroment_) enviroment_->destroy();
	if (lightMgr_) lightMgr_->destroy();
	if (cameraFac_) cameraFac_->destroy();
	if (mtlFac_) mtlFac_->destroy();
	if (userCamera_) userCamera_->destroy();
}
void VulkanGLTFSampleViewer::getEnabledFeatures()
{
	// Fill mode non solid is required for wireframe display
	if (deviceFeatures.fillModeNonSolid) enabledFeatures.fillModeNonSolid = VK_TRUE;
}
std::string VulkanGLTFSampleViewer::getSampleShadersPath() const
{
	return VulkanExampleBase::getShadersPath() + "GLTFSampleViewer/";
}

#pragma region prepare
static VkSampleCountFlagBits getMaxUsableSampleCount(VkPhysicalDevice physicalDevice, VkSampleCountFlagBits preferSample = VK_SAMPLE_COUNT_16_BIT)
{
	VkPhysicalDeviceProperties physicalDeviceProperties;
	vkGetPhysicalDeviceProperties(physicalDevice, &physicalDeviceProperties);
	VkSampleCountFlags supportedSampleCount = physicalDeviceProperties.limits.framebufferColorSampleCounts &
											  physicalDeviceProperties.limits.framebufferDepthSampleCounts;
	std::vector<VkSampleCountFlagBits> possibleSampleCounts{
		VK_SAMPLE_COUNT_64_BIT, VK_SAMPLE_COUNT_32_BIT, VK_SAMPLE_COUNT_16_BIT, VK_SAMPLE_COUNT_8_BIT, VK_SAMPLE_COUNT_4_BIT, VK_SAMPLE_COUNT_2_BIT
	};
	for (VkSampleCountFlagBits possibleSampleCount : possibleSampleCounts) {
		if (possibleSampleCount <= preferSample && (supportedSampleCount & possibleSampleCount)) {
			return possibleSampleCount;
		}
	}
	return VK_SAMPLE_COUNT_1_BIT;
}
void VulkanGLTFSampleViewer::initSettings()
{
	sampleCount_ = getMaxUsableSampleCount(physicalDevice);
	//sampleCount_ = VK_SAMPLE_COUNT_1_BIT;
	ui.rasterizationSamples = sampleCount_;
}

void VulkanGLTFSampleViewer::setupRenderPass()
{
	attachmentSize_ = { width, height };

	std::vector<VkAttachmentDescription> attachments;
	if (isMSAAEnabled())
	{
		attachments.resize(3, VkAttachmentDescription{});

		// Multisampled attachment that we render to
		auto& attachment_0 = attachments[0];
		attachment_0.format = swapChain.colorFormat;
		attachment_0.samples = sampleCount_;
		attachment_0.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		attachment_0.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		attachment_0.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		attachment_0.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		attachment_0.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		attachment_0.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

		// This is the frame buffer attachment to where the multisampled image
		// will be resolved to and which will be presented to the swapchain
		auto& attachment_1 = attachments[1];
		attachment_1.format = swapChain.colorFormat;
		attachment_1.samples = VK_SAMPLE_COUNT_1_BIT;
		attachment_1.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		attachment_1.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		attachment_1.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		attachment_1.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		attachment_1.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		attachment_1.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

		// Multisampled depth attachment we render to
		auto& attachment_2 = attachments[2];
		attachment_2.format = depthFormat;
		attachment_2.samples = sampleCount_;
		attachment_2.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		attachment_2.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		attachment_2.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		attachment_2.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		attachment_2.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		attachment_2.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
	}
	else
	{
		attachments.resize(2, VkAttachmentDescription{});

		auto& attachment_0 = attachments[0];
		attachment_0.format = swapChain.colorFormat;
		attachment_0.samples = VK_SAMPLE_COUNT_1_BIT;
		attachment_0.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		attachment_0.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		attachment_0.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		attachment_0.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		attachment_0.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		attachment_0.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

		// Depth attachment
		auto& attachment_1 = attachments[1];
		attachment_1.format = depthFormat;
		attachment_1.samples = VK_SAMPLE_COUNT_1_BIT;
		attachment_1.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		attachment_1.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		attachment_1.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		attachment_1.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		attachment_1.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		attachment_1.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
	}

	VkSubpassDescription subpass = {};
	VkAttachmentReference colorReference = {};
	VkAttachmentReference resolveReference = {};
	VkAttachmentReference depthReference = {};
	if (isMSAAEnabled())
	{
		colorReference.attachment = 0;
		colorReference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

		resolveReference.attachment = 1;
		resolveReference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

		depthReference.attachment = 2;
		depthReference.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

		subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
		subpass.colorAttachmentCount = 1;
		subpass.pColorAttachments = &colorReference;
		subpass.pResolveAttachments = &resolveReference;
		subpass.pDepthStencilAttachment = &depthReference;
	}
	else
	{
		colorReference.attachment = 0;
		colorReference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

		depthReference.attachment = 1;
		depthReference.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

		subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
		subpass.colorAttachmentCount = 1;
		subpass.pColorAttachments = &colorReference;
		subpass.pDepthStencilAttachment = &depthReference;
	}

	std::array<VkSubpassDependency, 2> dependencies{};
	{
		// Depth attachment
		auto& dependencies_0 = dependencies[0];
		dependencies_0.srcSubpass = VK_SUBPASS_EXTERNAL;
		dependencies_0.dstSubpass = 0;
		dependencies_0.srcStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
		dependencies_0.dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
		dependencies_0.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
		dependencies_0.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
		dependencies_0.dependencyFlags = 0;

		// Color attachment
		auto& dependencies_1 = dependencies[1];
		dependencies_1.srcSubpass = VK_SUBPASS_EXTERNAL;
		dependencies_1.dstSubpass = 0;
		dependencies_1.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		dependencies_1.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		dependencies_1.srcAccessMask = 0;
		dependencies_1.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT;
		dependencies_1.dependencyFlags = 0;
	}

	VkRenderPassCreateInfo renderPassInfo = vks::initializers::renderPassCreateInfo();
	renderPassInfo.attachmentCount = attachments.size();
	renderPassInfo.pAttachments = attachments.data();
	renderPassInfo.subpassCount = 1;
	renderPassInfo.pSubpasses = &subpass;
	renderPassInfo.dependencyCount = dependencies.size();
	renderPassInfo.pDependencies = dependencies.data();
	VK_CHECK_RESULT(vkCreateRenderPass(device, &renderPassInfo, nullptr, &renderPass));
}
void VulkanGLTFSampleViewer::setupMultisampleTarget()
{
	// Check if device supports requested sample count for color and depth frame buffer
	assert((deviceProperties.limits.framebufferColorSampleCounts & sampleCount_) && (deviceProperties.limits.framebufferDepthSampleCounts & sampleCount_));

	// Color target
	VkImageCreateInfo info = vks::initializers::imageCreateInfo();
	info.imageType = VK_IMAGE_TYPE_2D;
	info.format = swapChain.colorFormat;
	info.extent.width = width;
	info.extent.height = height;
	info.extent.depth = 1;
	info.mipLevels = 1;
	info.arrayLayers = 1;
	info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	info.tiling = VK_IMAGE_TILING_OPTIMAL;
	info.samples = sampleCount_;
	// Image will only be used as a transient target
	info.usage = VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
	info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	VK_CHECK_RESULT(vkCreateImage(device, &info, nullptr, &multisampleTarget_.color.image));

	VkMemoryRequirements memReqs;
	vkGetImageMemoryRequirements(device, multisampleTarget_.color.image, &memReqs);
	VkMemoryAllocateInfo memAlloc = vks::initializers::memoryAllocateInfo();
	memAlloc.allocationSize = memReqs.size;
	// We prefer a lazily allocated memory type
	// This means that the memory gets allocated when the implementation sees fit, e.g. when first using the images
	VkBool32 lazyMemTypePresent;
	memAlloc.memoryTypeIndex = vulkanDevice->getMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT, &lazyMemTypePresent);
	if (!lazyMemTypePresent)
	{
		// If this is not available, fall back to device local memory
		memAlloc.memoryTypeIndex = vulkanDevice->getMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	}
	VK_CHECK_RESULT(vkAllocateMemory(device, &memAlloc, nullptr, &multisampleTarget_.color.memory));
	vkBindImageMemory(device, multisampleTarget_.color.image, multisampleTarget_.color.memory, 0);

	// Create image view for the MSAA target
	VkImageViewCreateInfo viewInfo = vks::initializers::imageViewCreateInfo();
	viewInfo.image = multisampleTarget_.color.image;
	viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
	viewInfo.format = swapChain.colorFormat;
	viewInfo.components.r = VK_COMPONENT_SWIZZLE_R;
	viewInfo.components.g = VK_COMPONENT_SWIZZLE_G;
	viewInfo.components.b = VK_COMPONENT_SWIZZLE_B;
	viewInfo.components.a = VK_COMPONENT_SWIZZLE_A;
	viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	viewInfo.subresourceRange.levelCount = 1;
	viewInfo.subresourceRange.layerCount = 1;
	VK_CHECK_RESULT(vkCreateImageView(device, &viewInfo, nullptr, &multisampleTarget_.color.view));

	// Depth target
	info.imageType = VK_IMAGE_TYPE_2D;
	info.format = depthFormat;
	info.extent.width = width;
	info.extent.height = height;
	info.extent.depth = 1;
	info.mipLevels = 1;
	info.arrayLayers = 1;
	info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	info.tiling = VK_IMAGE_TILING_OPTIMAL;
	info.samples = sampleCount_;
	// Image will only be used as a transient target
	info.usage = VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT | VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
	info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	VK_CHECK_RESULT(vkCreateImage(device, &info, nullptr, &multisampleTarget_.depth.image));

	vkGetImageMemoryRequirements(device, multisampleTarget_.depth.image, &memReqs);
	memAlloc = vks::initializers::memoryAllocateInfo();
	memAlloc.allocationSize = memReqs.size;
	memAlloc.memoryTypeIndex = vulkanDevice->getMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT, &lazyMemTypePresent);
	if (!lazyMemTypePresent) memAlloc.memoryTypeIndex = vulkanDevice->getMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	VK_CHECK_RESULT(vkAllocateMemory(device, &memAlloc, nullptr, &multisampleTarget_.depth.memory));
	vkBindImageMemory(device, multisampleTarget_.depth.image, multisampleTarget_.depth.memory, 0);

	// Create image view for the MSAA target
	viewInfo.image = multisampleTarget_.depth.image;
	viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
	viewInfo.format = depthFormat;
	viewInfo.components.r = VK_COMPONENT_SWIZZLE_R;
	viewInfo.components.g = VK_COMPONENT_SWIZZLE_G;
	viewInfo.components.b = VK_COMPONENT_SWIZZLE_B;
	viewInfo.components.a = VK_COMPONENT_SWIZZLE_A;
	viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
	if (depthFormat >= VK_FORMAT_D16_UNORM_S8_UINT) viewInfo.subresourceRange.aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
	viewInfo.subresourceRange.levelCount = 1;
	viewInfo.subresourceRange.layerCount = 1;
	VK_CHECK_RESULT(vkCreateImageView(device, &viewInfo, nullptr, &multisampleTarget_.depth.view));
}
void VulkanGLTFSampleViewer::setupFrameBuffer()
{
	// SRS - If the window is resized, the MSAA attachments need to be released and recreated
	if (attachmentSize_.width != width || attachmentSize_.height != height)
	{
		attachmentSize_ = { width, height };
		multisampleTarget_.destroy(this->device);
	}

	setupMultisampleTarget();

	VkFramebufferCreateInfo frameBufferCreateInfo = {};
	frameBufferCreateInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
	frameBufferCreateInfo.pNext = NULL;
	frameBufferCreateInfo.renderPass = renderPass;
	frameBufferCreateInfo.width = width;
	frameBufferCreateInfo.height = height;
	frameBufferCreateInfo.layers = 1;
	// Create frame buffers for every swap chain image
	frameBuffers.resize(swapChain.images.size());
	for (uint32_t i = 0; i < frameBuffers.size(); i++) 
	{
		std::vector<VkImageView> attachments;
		if (isMSAAEnabled())
		{
			attachments.resize(3);
			attachments[0] = multisampleTarget_.color.view;
			attachments[1] = swapChain.imageViews[i];
			attachments[2] = multisampleTarget_.depth.view;
		}
		else
		{
			attachments.resize(2);
			attachments[0] = swapChain.imageViews[i];
			attachments[1] = multisampleTarget_.depth.view;
		}
		frameBufferCreateInfo.attachmentCount = attachments.size();
		frameBufferCreateInfo.pAttachments = attachments.data();	
		VK_CHECK_RESULT(vkCreateFramebuffer(device, &frameBufferCreateInfo, nullptr, &frameBuffers[i]));
	}
}

static bool LoadGltfModelFromFile(tinygltf::Model& gltfMdl, const std::string& filename)
{
	gltfMdl = tinygltf::Model();

	tinygltf::TinyGLTF gltfContext;
	std::string error, warning;
	bool result;
	if (vks::tools::getFileNameExtension(filename) == "gltf") {
		result = gltfContext.LoadASCIIFromFile(&gltfMdl, &error, &warning, filename);
	}
	else {
		result = gltfContext.LoadBinaryFromFile(&gltfMdl, &error, &warning, filename);
	}
	if (!result) {
		MessageBox(NULL, ("load model file " + filename + " failed!").c_str(), "LoadGltfModelFromFile error", MB_OK);
	}
	return result;
}
static EnviromentImagesPath MakeEnvImgsPath(std::string envDir, std::string envName) 
{
	if (!envDir.empty() && envDir.back() != '/') envDir.push_back('/');
	EnviromentImagesPath eip;
	eip.lambertEnvPath = envDir + envName + "/lambertian/diffuse.ktx2";

	eip.ggxEnvPath = envDir + envName + "/ggx/specular.ktx2";
	eip.ggxLutPath = envDir + "lut_ggx.png";

	eip.charlieEnvPath = envDir + envName + "/charlie/sheen.ktx2";
	eip.charlieLutPath = envDir + "lut_charlie.png";

	eip.sheenLutPath = envDir + "lut_sheen_E.png";
	return eip;
};
bool VulkanGLTFSampleViewer::initScene()
{
	const int uniformAllocCount = 128;
	const int samplerAllocCount = 128;
	std::vector<VkDescriptorPoolSize> poolSizes = {
		vks::initializers::descriptorPoolSize(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, uniformAllocCount),
		vks::initializers::descriptorPoolSize(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, samplerAllocCount),
	};
	const int maxSetCount = uniformAllocCount + samplerAllocCount;
	VkDescriptorPoolCreateInfo descriptorPoolInfo = vks::initializers::descriptorPoolCreateInfo(poolSizes, maxSetCount);
	VK_CHECK_RESULT(vkCreateDescriptorPool(device, &descriptorPoolInfo, nullptr, &descriptorPool));

	enviroment_ = std::make_shared<Enviroment>(vulkanDevice, descriptorPool, queue);
	lightMgr_ = std::make_shared<LightManager>(vulkanDevice, descriptorPool);
	cameraFac_ = std::make_shared<CameraFactory>(vulkanDevice, descriptorPool);
	mtlFac_ = std::make_shared<MaterialFactory>(vulkanDevice, descriptorPool);

	skyBox_ = std::make_shared<AnimatedModel>(vulkanDevice, descriptorPool, queue);
	model_ = std::make_shared<AnimatedModel>(vulkanDevice, descriptorPool, queue);

	tinygltf::Model gltfMdl, gltfCamera, gltfSkybox;
	tinygltf::TinyGLTF gltfContext;
	if (!LoadGltfModelFromFile(gltfSkybox, getAssetPath() + "models/cube.gltf")) return false;
#if 1
	if (modelGlb_) {
		if (!LoadGltfModelFromFile(gltfMdl, getModelAssetPath() + "Models/" + modelName_ + "/glTF-Binary/" + modelName_ + ".glb")) 
			return false;
	}
	else {
		if (!LoadGltfModelFromFile(gltfMdl, getModelAssetPath() + "Models/" + modelName_ + "/glTF/" + modelName_ + ".gltf"))
			return false;
	}
	gltfCamera = gltfMdl;
#elif 0
	if (!LoadGltfModelFromFile(gltfMdl, getModelAssetPath() + "Models/BusterDrone/glTF-Binary/BusterDrone.glb")) return false;
	if (!LoadGltfModelFromFile(gltfCamera, getModelAssetPath() + "Models/BusterDrone/glTF/camera.gltf")) return false;
	gltfCamera = gltfMdl;
#elif 0
	if (!LoadGltfModelFromFile(gltfMdl, getModelAssetPath() + "Models/BoxAnimated/glTF/BoxAnimated.gltf")) return false;
	if (!LoadGltfModelFromFile(gltfCamera, getModelAssetPath() + "Models/BoxAnimated/glTF/camera.gltf")) return false;
	gltfCamera = gltfMdl;
#elif 0
	if (!LoadGltfModelFromFile(gltfMdl, getModelAssetPath() + "Models/CesiumMilkTruck/glTF/CesiumMilkTruck.gltf")) return false;
	gltfCamera = gltfMdl;
#elif 0
	if (!LoadGltfModelFromFile(gltfMdl, getModelAssetPath() + "Models/CesiumMan/glTF/CesiumMan.gltf")) return false;
	gltfCamera = gltfMdl;
#elif 0
	if (!LoadGltfModelFromFile(gltfMdl, getModelAssetPath() + "Models/BrainStem/glTF/BrainStem.gltf")) return false;
	gltfCamera = gltfMdl;
#elif 1
	if (!LoadGltfModelFromFile(gltfMdl, getModelAssetPath() + "Models/ToyCar/glTF/ToyCar.gltf")) return false;
	if (!LoadGltfModelFromFile(gltfCamera, getModelAssetPath() + "Models/ToyCar/glTF/camera.gltf")) return false;
	gltfCamera = gltfMdl;
#endif

	skyBox_->load(gltfSkybox, *mtlFac_, true);
	lightMgr_->load(gltfMdl);

	model_->load(gltfMdl, *mtlFac_);
	constantValue_.USE_SKELETON = model_->hasSkin();

	BoundingBox bbox = model_->getWorldBBox();
	userCamera_ = cameraFac_->creatCamera((float)width / (float)height, bbox.min, bbox.max, gltfCamera);

	DrawableQueueGroup dqg;
	model_->getDrawableQueueGroup(dqg, *userCamera_);
	hasTransmission_ = !dqg.transmissionQueue_.empty();
	if (hasTransmission_ && !opaqueFramebuffer_) createOpaqueFramebuffer();

	// however, the environment resource "https://raw.githubusercontent.com/KhronosGroup/glTF-Sample-Environments/low_resolution_hdrs/neutral.hdr" glTF-Sample-Viewer used 
	// is more lighter than https://github.com/KhronosGroup/glTF-Sample-Environments our used
	enviroment_->load(MakeEnvImgsPath(getEnviromentAssetPath(), enviromentName_), hasTransmission_ ? &opaqueFramebuffer_->attachments[isMSAAEnabled() ? 1 : 0] : nullptr);

	// write-to descriptor-sets
	std::vector<VkWriteDescriptorSet> writeDescriptorSet;
	userCamera_->uploadDescriptorSet2Gpu(writeDescriptorSet);
	lightMgr_->uploadDescriptorSet2Gpu(writeDescriptorSet);
	enviroment_->uploadDescriptorSet2Gpu(writeDescriptorSet);
	model_->uploadDescriptorSet2Gpu(writeDescriptorSet);
	vkUpdateDescriptorSets(device, writeDescriptorSet.size(), writeDescriptorSet.data(), 0, nullptr);
	return true;
}
void VulkanGLTFSampleViewer::createOpaqueFramebuffer()
{
	if (!opaqueFramebuffer_) opaqueFramebuffer_ = std::make_shared<vks::Framebuffer>(vulkanDevice);
	opaqueFramebuffer_->destroy();

	opaqueFramebuffer_->width = width;
	opaqueFramebuffer_->height = height;
	if (isMSAAEnabled())
	{
		opaqueFramebuffer_->addAttachment(vks::makeAttachmentCreateInfo(
			width, height, swapChain.colorFormat, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT, sampleCount_,
			VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_DONT_CARE,
			VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
		));
		opaqueFramebuffer_->addAttachment(vks::makeAttachmentCreateInfo(
			width, height, swapChain.colorFormat, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_SAMPLE_COUNT_1_BIT,
			VK_ATTACHMENT_LOAD_OP_DONT_CARE, VK_ATTACHMENT_STORE_OP_STORE,
			VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
		), true);
		opaqueFramebuffer_->addAttachment(vks::makeAttachmentCreateInfo(
			width, height, depthFormat, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, sampleCount_,
			VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_DONT_CARE,
			VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
		));
	}
	else
	{
		opaqueFramebuffer_->addAttachment(vks::makeAttachmentCreateInfo(
			width, height, swapChain.colorFormat, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, sampleCount_,
			VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE,
			VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
		));
		opaqueFramebuffer_->addAttachment(vks::makeAttachmentCreateInfo(
			width, height, depthFormat, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, sampleCount_,
			VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_DONT_CARE,
			VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
		));
	}

	std::vector<VkSubpassDependency> dependencies(2);
	{
		// Depth attachment
		auto& dependencies_0 = dependencies[0];
		dependencies_0.srcSubpass = VK_SUBPASS_EXTERNAL;
		dependencies_0.dstSubpass = 0;
		dependencies_0.srcStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
		dependencies_0.dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
		dependencies_0.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
		dependencies_0.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
		dependencies_0.dependencyFlags = 0;

		// Color attachment
		auto& dependencies_1 = dependencies[1];
		dependencies_1.srcSubpass = VK_SUBPASS_EXTERNAL;
		dependencies_1.dstSubpass = 0;
		dependencies_1.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		dependencies_1.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		dependencies_1.srcAccessMask = 0;
		dependencies_1.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT;
		dependencies_1.dependencyFlags = 0;
	}
	opaqueFramebuffer_->createRenderPass(dependencies);
}
bool VulkanGLTFSampleViewer::reloadEnviroment()
{
	enviroment_->load(MakeEnvImgsPath(getEnviromentAssetPath(), enviromentName_), hasTransmission_ ? &opaqueFramebuffer_->attachments[isMSAAEnabled() ? 1 : 0] : nullptr);

	std::vector<VkWriteDescriptorSet> writeDescriptorSet;
	enviroment_->uploadDescriptorSet2Gpu(writeDescriptorSet);
	vkUpdateDescriptorSets(device, writeDescriptorSet.size(), writeDescriptorSet.data(), 0, nullptr);
	return true;
}
bool VulkanGLTFSampleViewer::reloadModel()
{
	tinygltf::Model gltfMdl, gltfCamera;
	{
		tinygltf::TinyGLTF gltfContext;
		if (modelGlb_) {
			if (!LoadGltfModelFromFile(gltfMdl, getModelAssetPath() + "Models/" + modelName_ + "/glTF-Binary/" + modelName_ + ".glb"))
				return false;
		}
		else {
			if (!LoadGltfModelFromFile(gltfMdl, getModelAssetPath() + "Models/" + modelName_ + "/glTF/" + modelName_ + ".gltf"))
				return false;
		}
		gltfCamera = gltfMdl;
	}

	lightMgr_->load(gltfMdl);

	model_->load(gltfMdl, *mtlFac_);

	constantValue_.USE_SKELETON = model_->hasSkin();
	if (!modelPipelineByConstant_.count(constantValue_)) {
		createModelPipeline(constantValue_, modelPipelineByConstant_[constantValue_]);
	}

	BoundingBox bbox = model_->getWorldBBox();
	userCamera_ = cameraFac_->creatCamera((float)width / (float)height, bbox.min, bbox.max, gltfCamera);

	DrawableQueueGroup dqg;
	model_->getDrawableQueueGroup(dqg, *userCamera_);
	bool hasTransmission = !dqg.transmissionQueue_.empty();
	if (hasTransmission && !opaqueFramebuffer_) {
		createOpaqueFramebuffer();
	}
	if (hasTransmission != hasTransmission_) {
		enviroment_->load(MakeEnvImgsPath(getEnviromentAssetPath(), enviromentName_), hasTransmission_ ? &opaqueFramebuffer_->attachments[isMSAAEnabled() ? 1 : 0] : nullptr);
	}
	hasTransmission_ = hasTransmission;

	// write-to descriptor-sets
	std::vector<VkWriteDescriptorSet> writeDescriptorSet;
	userCamera_->uploadDescriptorSet2Gpu(writeDescriptorSet);
	lightMgr_->uploadDescriptorSet2Gpu(writeDescriptorSet);
	enviroment_->uploadDescriptorSet2Gpu(writeDescriptorSet);
	model_->uploadDescriptorSet2Gpu(writeDescriptorSet);
	vkUpdateDescriptorSets(device, writeDescriptorSet.size(), writeDescriptorSet.data(), 0, nullptr);
	return true;
}

bool VulkanGLTFSampleViewer::createSkyboxPipeline(int TONEMAP, ModelPipeline& mdlpipe)
{
	mdlpipe.layout = skyboxPipelineLayout_;

	// Vertex input bindings and attributes
	VkPipelineVertexInputStateCreateInfo vertexInputStateCI = vks::initializers::pipelineVertexInputStateCreateInfo();
	const std::vector<VkVertexInputBindingDescription> vertexInputBindings = {
		vks::initializers::vertexInputBindingDescription(0, sizeof(AnimatedModel::Vertex), VK_VERTEX_INPUT_RATE_VERTEX),
	};
	vertexInputStateCI.vertexBindingDescriptionCount = static_cast<uint32_t>(vertexInputBindings.size());
	vertexInputStateCI.pVertexBindingDescriptions = vertexInputBindings.data();
	const std::vector<VkVertexInputAttributeDescription> vertexInputAttributes = model_->getVertexAttributesDesc();
	vertexInputStateCI.vertexAttributeDescriptionCount = static_cast<uint32_t>(vertexInputAttributes.size());
	vertexInputStateCI.pVertexAttributeDescriptions = vertexInputAttributes.data();

	VkPipelineInputAssemblyStateCreateInfo inputAssemblyStateCI = vks::initializers::pipelineInputAssemblyStateCreateInfo(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, 0, VK_FALSE);
	VkPipelineRasterizationStateCreateInfo rasterizationStateCI = vks::initializers::pipelineRasterizationStateCreateInfo(VK_POLYGON_MODE_FILL, VK_CULL_MODE_BACK_BIT, VK_FRONT_FACE_COUNTER_CLOCKWISE, 0);
	VkPipelineColorBlendAttachmentState blendAttachmentStateCI = vks::initializers::pipelineColorBlendAttachmentState(0xf, VK_FALSE);
	VkPipelineColorBlendStateCreateInfo colorBlendStateCI = vks::initializers::pipelineColorBlendStateCreateInfo(1, &blendAttachmentStateCI);
	VkPipelineDepthStencilStateCreateInfo depthStencilStateCI = vks::initializers::pipelineDepthStencilStateCreateInfo(VK_TRUE, VK_TRUE, VK_COMPARE_OP_LESS_OR_EQUAL);
	VkPipelineViewportStateCreateInfo viewportStateCI = vks::initializers::pipelineViewportStateCreateInfo(1, 1, 0);
	VkPipelineMultisampleStateCreateInfo multisampleStateCI = vks::initializers::pipelineMultisampleStateCreateInfo(sampleCount_, 0);
	const std::vector<VkDynamicState> dynamicStateEnables = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
	VkPipelineDynamicStateCreateInfo dynamicStateCI = vks::initializers::pipelineDynamicStateCreateInfo(dynamicStateEnables.data(), static_cast<uint32_t>(dynamicStateEnables.size()), 0);

	std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages = {
		loadShader(getSampleShadersPath() + skyboxShaderName_.vertex + ".spv", VK_SHADER_STAGE_VERTEX_BIT),
		loadShader(getSampleShadersPath() + skyboxShaderName_.pixel + ".spv", VK_SHADER_STAGE_FRAGMENT_BIT)
	};
	std::vector<VkSpecializationMapEntry> constantEntries;
	for (size_t constantId = 0; constantId < 1; ++constantId) {
		constantEntries.push_back(vks::initializers::specializationMapEntry(constantId, sizeof(int) * constantId, sizeof(int)));
	}
	VkSpecializationInfo psSpec = vks::initializers::specializationInfo(constantEntries, sizeof(TONEMAP), &TONEMAP);
	shaderStages[0].pSpecializationInfo = &psSpec;
	shaderStages[1].pSpecializationInfo = &psSpec;

	// mdlpipe.solid
	// mdlpipe.wireframe
	VkGraphicsPipelineCreateInfo pipelineCI = vks::initializers::pipelineCreateInfo(mdlpipe.layout, renderPass, 0);
	pipelineCI.pVertexInputState = &vertexInputStateCI;
	pipelineCI.pInputAssemblyState = &inputAssemblyStateCI;
	pipelineCI.pRasterizationState = &rasterizationStateCI;
	pipelineCI.pColorBlendState = &colorBlendStateCI;
	pipelineCI.pMultisampleState = &multisampleStateCI;
	pipelineCI.pViewportState = &viewportStateCI;
	pipelineCI.pDepthStencilState = &depthStencilStateCI;
	pipelineCI.pDynamicState = &dynamicStateCI;
	pipelineCI.stageCount = static_cast<uint32_t>(shaderStages.size());
	pipelineCI.pStages = shaderStages.data();

	// mdlpipe.solid
	rasterizationStateCI.polygonMode = VK_POLYGON_MODE_FILL;
	VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCI, nullptr, &mdlpipe.solid));

	// mdlpipe.wireframe
	if (deviceFeatures.fillModeNonSolid) {
		rasterizationStateCI.polygonMode = VK_POLYGON_MODE_LINE;
		VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCI, nullptr, &mdlpipe.wireframe));
	}
	return true;
}
bool VulkanGLTFSampleViewer::createModelPipeline(const ConstantValue& cv, ModelPipeline& mdlpipe)
{
	mdlpipe.layout = modelPipelineLayout_;

	// Vertex input bindings and attributes
	VkPipelineVertexInputStateCreateInfo vertexInputStateCI = vks::initializers::pipelineVertexInputStateCreateInfo();
	const std::vector<VkVertexInputBindingDescription> vertexInputBindings = {
		vks::initializers::vertexInputBindingDescription(0, sizeof(AnimatedModel::Vertex), VK_VERTEX_INPUT_RATE_VERTEX),
	};
	vertexInputStateCI.vertexBindingDescriptionCount = static_cast<uint32_t>(vertexInputBindings.size());
	vertexInputStateCI.pVertexBindingDescriptions = vertexInputBindings.data();
	const std::vector<VkVertexInputAttributeDescription> vertexInputAttributes = model_->getVertexAttributesDesc();
	vertexInputStateCI.vertexAttributeDescriptionCount = static_cast<uint32_t>(vertexInputAttributes.size());
	vertexInputStateCI.pVertexAttributeDescriptions = vertexInputAttributes.data();

	VkPipelineInputAssemblyStateCreateInfo inputAssemblyStateCI = vks::initializers::pipelineInputAssemblyStateCreateInfo(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, 0, VK_FALSE);
	VkPipelineRasterizationStateCreateInfo rasterizationStateCI = vks::initializers::pipelineRasterizationStateCreateInfo(VK_POLYGON_MODE_FILL, VK_CULL_MODE_BACK_BIT, VK_FRONT_FACE_COUNTER_CLOCKWISE, 0);
	VkPipelineColorBlendAttachmentState blendAttachmentStateCI = vks::initializers::pipelineColorBlendAttachmentState(0xf, VK_FALSE);
	VkPipelineColorBlendStateCreateInfo colorBlendStateCI = vks::initializers::pipelineColorBlendStateCreateInfo(1, &blendAttachmentStateCI);
	VkPipelineDepthStencilStateCreateInfo depthStencilStateCI = vks::initializers::pipelineDepthStencilStateCreateInfo(VK_TRUE, VK_TRUE, VK_COMPARE_OP_LESS_OR_EQUAL);
	VkPipelineViewportStateCreateInfo viewportStateCI = vks::initializers::pipelineViewportStateCreateInfo(1, 1, 0);
	VkPipelineMultisampleStateCreateInfo multisampleStateCI = vks::initializers::pipelineMultisampleStateCreateInfo(sampleCount_, 0);
	const std::vector<VkDynamicState> dynamicStateEnables = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
	VkPipelineDynamicStateCreateInfo dynamicStateCI = vks::initializers::pipelineDynamicStateCreateInfo(dynamicStateEnables.data(), static_cast<uint32_t>(dynamicStateEnables.size()), 0);

	std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages = {
		loadShader(getSampleShadersPath() + modelShaderName_.vertex + ".spv", VK_SHADER_STAGE_VERTEX_BIT),
		loadShader(getSampleShadersPath() + modelShaderName_.pixel + ".spv", VK_SHADER_STAGE_FRAGMENT_BIT)
	};
	std::vector<VkSpecializationMapEntry> constantEntries;
	for (size_t constantId = 0; constantId < sizeof(cv) / sizeof(int); ++constantId) {
		constantEntries.push_back(vks::initializers::specializationMapEntry(constantId, sizeof(int) * constantId, sizeof(int)));
	}
	VkSpecializationInfo psSpec = vks::initializers::specializationInfo(constantEntries, sizeof(cv), &cv);
	shaderStages[0].pSpecializationInfo = &psSpec;
	shaderStages[1].pSpecializationInfo = &psSpec;

	// mdlpipe.solid
	// mdlpipe.wireframe
	VkGraphicsPipelineCreateInfo pipelineCI = vks::initializers::pipelineCreateInfo(mdlpipe.layout, renderPass, 0);
	pipelineCI.pVertexInputState = &vertexInputStateCI;
	pipelineCI.pInputAssemblyState = &inputAssemblyStateCI;
	pipelineCI.pRasterizationState = &rasterizationStateCI;
	pipelineCI.pColorBlendState = &colorBlendStateCI;
	pipelineCI.pMultisampleState = &multisampleStateCI;
	pipelineCI.pViewportState = &viewportStateCI;
	pipelineCI.pDepthStencilState = &depthStencilStateCI;
	pipelineCI.pDynamicState = &dynamicStateCI;
	pipelineCI.stageCount = static_cast<uint32_t>(shaderStages.size());
	pipelineCI.pStages = shaderStages.data();

	// mdlpipe.solid
	rasterizationStateCI.polygonMode = VK_POLYGON_MODE_FILL;
	VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCI, nullptr, &mdlpipe.solid));

	// mdlpipe.wireframe
	if (deviceFeatures.fillModeNonSolid) {
		rasterizationStateCI.polygonMode = VK_POLYGON_MODE_LINE;
		VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCI, nullptr, &mdlpipe.wireframe));
	}
	return true;
}
void VulkanGLTFSampleViewer::preparePipelines()
{
	if (!modelPipelineLayout_)
	{
		std::array<VkDescriptorSetLayout, SET_COUNT> dsLayouts = {
			enviroment_->descriptorSetLayout,
			cameraFac_->descriptorSetLayout,
			lightMgr_->descriptorSetLayout,
			mtlFac_->getDescriptorSetLayout(),
			model_->skeletonDSLayout
		};
		VkPipelineLayoutCreateInfo pipelineLayoutCI = vks::initializers::pipelineLayoutCreateInfo(dsLayouts.data(), dsLayouts.size());
		std::vector<VkPushConstantRange> pushConstantRanges = {
			vks::initializers::pushConstantRange(VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, sizeof(PushConsts), 0),
		};
		pipelineLayoutCI.pushConstantRangeCount = pushConstantRanges.size();
		pipelineLayoutCI.pPushConstantRanges = pushConstantRanges.data();
		VK_CHECK_RESULT(vkCreatePipelineLayout(device, &pipelineLayoutCI, nullptr, &modelPipelineLayout_));
	}
	createModelPipeline(constantValue_, modelPipelineByConstant_[constantValue_]);
	auto constantValueLinear = constantValue_; constantValueLinear.TONEMAP = TONEMAP_LINEAR;
	createModelPipeline(constantValueLinear, modelPipelineByConstant_[constantValueLinear]);

	if (!skyboxPipelineLayout_)
	{
		std::array<VkDescriptorSetLayout, SET_COUNT> dsLayouts = {
			enviroment_->descriptorSetLayout,
			cameraFac_->descriptorSetLayout,
			lightMgr_->descriptorSetLayout,
			mtlFac_->getDescriptorSetLayout(),
			model_->skeletonDSLayout
		};
		VkPipelineLayoutCreateInfo pipelineLayoutCI = vks::initializers::pipelineLayoutCreateInfo(dsLayouts.data(), dsLayouts.size());
		std::vector<VkPushConstantRange> pushConstantRanges = {
			vks::initializers::pushConstantRange(VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, sizeof(PushConsts), 0),
		};
		pipelineLayoutCI.pushConstantRangeCount = pushConstantRanges.size();
		pipelineLayoutCI.pPushConstantRanges = pushConstantRanges.data();
		VK_CHECK_RESULT(vkCreatePipelineLayout(device, &pipelineLayoutCI, nullptr, &skyboxPipelineLayout_));
	}
	if (!skyboxPipeline_.layout) createSkyboxPipeline(constantValue_.TONEMAP, skyboxPipeline_);
	if (!skyboxLinearPipeline_.layout) createSkyboxPipeline(TONEMAP_LINEAR, skyboxLinearPipeline_);
}

void VulkanGLTFSampleViewer::drawScene(int currentBuffer)
{
	VkCommandBufferBeginInfo cmdBufInfo = vks::initializers::commandBufferBeginInfo();

	std::vector<VkClearValue> clearValues;
	defaultClearColor = { { 0.886, 0.886, 0.886, 1.0f } };
	if (isMSAAEnabled())
	{
		clearValues.resize(3);
		clearValues[0].color = defaultClearColor;
		clearValues[1].color = defaultClearColor;
		clearValues[2].depthStencil = { 1.0f, 0 };
	}
	else
	{
		clearValues.resize(2);
		clearValues[0].color = defaultClearColor;
		clearValues[1].depthStencil = { 1.0f, 0 };
	}

	DrawableQueueGroup modelDQG, skyDQG;
	model_->getDrawableQueueGroup(modelDQG, *userCamera_); 
	modelDQG.sortTransparentQueueByDepth(); 
	modelDQG.sortTransmissionQueueByDepth();
	skyBox_->getDrawableQueueGroup(skyDQG, *userCamera_);

	VK_CHECK_RESULT(vkBeginCommandBuffer(drawCmdBuffers[currentBuffer], &cmdBufInfo));
	{
		const VkViewport viewport = vks::initializers::viewport((float)width, (float)height, 0.0f, 1.0f);
		vkCmdSetViewport(drawCmdBuffers[currentBuffer], 0, 1, &viewport);

		const VkRect2D scissor = vks::initializers::rect2D(width, height, 0, 0);
		vkCmdSetScissor(drawCmdBuffers[currentBuffer], 0, 1, &scissor);

		// descriptor set
		std::array<VkDescriptorSet, 3> descriptorSets = {
			enviroment_->descriptorSet,
			userCamera_->descriptorSet,
			lightMgr_->descriptorSet
		};
		
		auto func_bindVboIbo = [&](AnimatedModel& mdl) {
			VkDeviceSize offsets[1] = { 0 };
			vkCmdBindVertexBuffers(drawCmdBuffers[currentBuffer], 0, 1, &mdl.vertices.buffer, offsets);
			vkCmdBindIndexBuffer(drawCmdBuffers[currentBuffer], mdl.indices.buffer, 0, VK_INDEX_TYPE_UINT32);
		};

		auto& mdl_pipe = modelPipelineByConstant_[constantValue_];
		auto constantValueLinear = constantValue_; constantValueLinear.TONEMAP = TONEMAP_LINEAR;
		auto& mdl_linear_pipe = modelPipelineByConstant_[constantValueLinear];
		
		auto& sky_pipe = skyboxPipeline_;
		auto& sky_linear_pipe = skyboxLinearPipeline_;

		auto func_bindPipeline = [&](ModelPipeline& mpipe) {
			// bind pipeline
			vkCmdBindPipeline(drawCmdBuffers[currentBuffer], VK_PIPELINE_BIND_POINT_GRAPHICS, wireframe_ ? mpipe.wireframe : mpipe.solid);
			// bind descriptor set
			vkCmdBindDescriptorSets(drawCmdBuffers[currentBuffer], VK_PIPELINE_BIND_POINT_GRAPHICS, mpipe.layout, ENVIROMENT_SET, descriptorSets.size(), descriptorSets.data(), 0, nullptr);
		};
		auto func_beginPass = [&](VkRenderPass pass, int width, int height, VkFramebuffer framebuffer){
			VkRenderPassBeginInfo renderPassBeginInfo = vks::initializers::renderPassBeginInfo();
			renderPassBeginInfo.renderPass = pass;
			renderPassBeginInfo.renderArea.offset.x = 0;
			renderPassBeginInfo.renderArea.offset.y = 0;
			renderPassBeginInfo.renderArea.extent.width = width;
			renderPassBeginInfo.renderArea.extent.height = height;
			renderPassBeginInfo.clearValueCount = clearValues.size();
			renderPassBeginInfo.pClearValues = clearValues.data();
			renderPassBeginInfo.framebuffer = framebuffer;
			vkCmdBeginRenderPass(drawCmdBuffers[currentBuffer], &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);
		};
		auto func_endPass = [&](){
			vkCmdEndRenderPass(drawCmdBuffers[currentBuffer]);
		};

		auto func_draw = [&](Drawable& drawable, ModelPipeline& mpipe) {
			drawable.draw(drawCmdBuffers[currentBuffer], mpipe.layout);
		};
		if (hasTransmission_) 
		{
			func_beginPass(opaqueFramebuffer_->renderPass, opaqueFramebuffer_->width, opaqueFramebuffer_->height, opaqueFramebuffer_->framebuffer);
			{
				if (showEnviromentMap_) {
					func_bindPipeline(sky_linear_pipe);
					func_bindVboIbo(*skyBox_);
					for (auto& drawable : skyDQG.opaqueQueue_)
						func_draw(drawable, sky_linear_pipe);
				}

				func_bindPipeline(mdl_linear_pipe);
				func_bindVboIbo(*model_);
				for (auto& drawable : modelDQG.opaqueQueue_)
					func_draw(drawable, mdl_linear_pipe);
				for (auto& drawable : modelDQG.transparentQueue_)
					func_draw(drawable, mdl_linear_pipe);
			}
			func_endPass();

			vks::tools::insertImageMemoryBarrier(
				drawCmdBuffers[currentBuffer],
				opaqueFramebuffer_->attachments[isMSAAEnabled() ? 1 : 0].image,
				VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, 
				VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 
				VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
				VkImageSubresourceRange{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 }
			);

			func_beginPass(renderPass, width, height, frameBuffers[currentBuffer]);
			{
				if (showEnviromentMap_) {
					func_bindPipeline(sky_pipe);
					func_bindVboIbo(*skyBox_);
					for (auto& drawable : skyDQG.opaqueQueue_)
						func_draw(drawable, sky_pipe);
				}

				func_bindPipeline(mdl_pipe);
				func_bindVboIbo(*model_);
				for (auto& drawable : modelDQG.opaqueQueue_)
					func_draw(drawable, mdl_pipe);
				for (auto& drawable : modelDQG.transmissionQueue_)
					func_draw(drawable, mdl_pipe);
				for (auto& drawable : modelDQG.transparentQueue_)
					func_draw(drawable, mdl_pipe);

				drawUI(drawCmdBuffers[currentBuffer]);
			}
			func_endPass();
		}
		else
		{
			func_beginPass(renderPass, width, height, frameBuffers[currentBuffer]);
			{
				if (showEnviromentMap_) {
					func_bindPipeline(sky_pipe);
					func_bindVboIbo(*skyBox_);
					for (auto& drawable : skyDQG.opaqueQueue_)
						func_draw(drawable, sky_pipe);
				}

				func_bindPipeline(mdl_pipe);
				func_bindVboIbo(*model_);
				for (auto& drawable : modelDQG.opaqueQueue_)
					func_draw(drawable, mdl_pipe);
				for (auto& drawable : modelDQG.transmissionQueue_)
					func_draw(drawable, mdl_pipe);
				for (auto& drawable : modelDQG.transparentQueue_)
					func_draw(drawable, mdl_pipe);
				drawUI(drawCmdBuffers[currentBuffer]);
			}
			func_endPass();
		}
	}
	VK_CHECK_RESULT(vkEndCommandBuffer(drawCmdBuffers[currentBuffer]));
}
void VulkanGLTFSampleViewer::buildCommandBuffers()
{
	for (int i = 0; i < drawCmdBuffers.size(); ++i) {
		drawScene(i);
	}
}

void VulkanGLTFSampleViewer::prepare()
{
	initSettings();
	VulkanExampleBase::prepare();
	if (!initScene()) return;
	preparePipelines();
	buildCommandBuffers();
	prepared = true;
}
#pragma endregion

#pragma region render
void VulkanGLTFSampleViewer::updateScene()
{
	if (!paused) animationTime_ += timerSpeed * frameTimer;
	model_->setAnimationTime(animationTime_);

	userCamera_->uploadParams2Gpu();
	lightMgr_->uploadParams2Gpu();
	enviroment_->uploadParams2Gpu();
}
void VulkanGLTFSampleViewer::myRrenderFrame()
{
	VulkanExampleBase::prepareFrame();

	updateScene();
	drawScene(currentBuffer);

	submitInfo.commandBufferCount = 1;
	submitInfo.pCommandBuffers = &drawCmdBuffers[currentBuffer];
	VK_CHECK_RESULT(vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE));
	VulkanExampleBase::submitFrame();
}

void VulkanGLTFSampleViewer::render()
{
	if (!prepared)
		return;
	myRrenderFrame();
}
#pragma endregion

void VulkanGLTFSampleViewer::windowResized()
{
	if (hasTransmission_) createOpaqueFramebuffer();
}

void VulkanGLTFSampleViewer::mouseWheeled(short wheelDelta, bool& handled)
{
	if (cameraFixed_) {
		handled = true;
		return;
	}
#if 0
	camera.translate(glm::vec3(0.0f, 0.0f, (float)wheelDelta * 0.0005f));
#else
	userCamera_->zoomBy(wheelDelta);
#endif
}
void VulkanGLTFSampleViewer::mouseMoved(double x, double y, int32_t mouseFlag, bool& handled)
{
	if (cameraFixed_) {
		handled = true;
		return;
	}
#if 0
	int32_t dx = (int32_t)mousePos.x - x;
	int32_t dy = (int32_t)mousePos.y - y;

	if (mouseButtons.left) {
		camera.rotate(glm::vec3(dy * camera.rotationSpeed, -dx * camera.rotationSpeed, 0.0f));
		viewUpdated = true;
	}
	if (mouseButtons.right) {
		camera.translate(glm::vec3(-0.0f, 0.0f, dy * .005f * camera.movementSpeed));
		viewUpdated = true;
	}
	if (mouseButtons.middle) {
		camera.translate(glm::vec3(-dx * 0.005f * camera.movementSpeed, -dy * 0.005f * camera.movementSpeed, 0.0f));
		viewUpdated = true;
	}
	mousePos = glm::vec2((float)x, (float)y);
#else
	glm::vec2 newMouse(x, y);
	if (mouseFlag & MK_LBUTTON)
	{
		float deltaPhi = newMouse.x - mouseState.position.x;
		float deltaTheta = newMouse.y - mouseState.position.y;
		userCamera_->orbit(deltaPhi, deltaTheta);
		handled = true;
	}
	else if (mouseFlag & MK_RBUTTON)
	{
		float deltaX = newMouse.x - mouseState.position.x;
		float deltaY = -(newMouse.y - mouseState.position.y);
		userCamera_->pan(deltaX, deltaY);
		handled = true;
	}
#endif
}