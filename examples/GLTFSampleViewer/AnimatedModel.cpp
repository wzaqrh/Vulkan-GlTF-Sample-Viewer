#include "AnimatedModel.h"
#include "Camera.h"

AnimatedModel::AnimatedModel(vks::VulkanDevice* vulkanDevice, VkDescriptorPool descriptorPool, VkQueue queue)
{
	vulkanDevice_ = vulkanDevice;
	descriptorPool_ = descriptorPool;
	queue_ = queue;

	std::vector<VkDescriptorSetLayoutBinding> setLayoutBinding(1, vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_VERTEX_BIT, SKELETON_BINDING));
	VkDescriptorSetLayoutCreateInfo descriptorSetLayoutCI_Params = vks::initializers::descriptorSetLayoutCreateInfo(setLayoutBinding.data(), setLayoutBinding.size());

	VkDescriptorSetLayoutBindingFlagsCreateInfo dsLayoutBindingFCInfo = {};
	std::vector<VkDescriptorBindingFlags> bindingFlags;
	{
		bindingFlags.push_back(VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT);
		dsLayoutBindingFCInfo.bindingCount = bindingFlags.size();
		dsLayoutBindingFCInfo.pBindingFlags = bindingFlags.data();
		dsLayoutBindingFCInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO;
		dsLayoutBindingFCInfo.pNext = nullptr;
		descriptorSetLayoutCI_Params.pNext = &dsLayoutBindingFCInfo;
	}

	VK_CHECK_RESULT(vkCreateDescriptorSetLayout(vulkanDevice_->logicalDevice, &descriptorSetLayoutCI_Params, nullptr, &skeletonDSLayout));
}
AnimatedModel::~AnimatedModel()
{
	for (auto node : nodes_) {
		delete node;
	}
	nodes_.clear();

	destroy();
}
void AnimatedModel::destroy()
{
	vkSafeDestroyDescriptorSetLayout(vulkanDevice_->logicalDevice, skeletonDSLayout);
	reset();
}
void AnimatedModel::reset()
{
	for (auto& mtl : materials_) {
		mtl.dispose();
	}
	materials_.clear();

	for (auto& skin : skins_) {
		//vkSafeFreeDescriptorSets(device, descriptorPool_, 1, skin.descriptorSet);
		skin.destroy();
	}
	skins_.clear();
	dummySkin_.destroy();

	for (Image& image : images_) {
		image.destroy();
	}
	images_.clear();

	vertices.destroy(vulkanDevice_->logicalDevice);
	indices.destroy(vulkanDevice_->logicalDevice);

	{
		images_.clear();
		materials_.clear();
		nodes_.clear(); nodeByIndex_.clear();
		skins_.clear();
		animations_.clear();
		animationIndex_ = 0;
	}
}

void VertexBuffer::destroy(VkDevice device)
{
	vkSafeDestroyBuffer(device, this->buffer);
	vkSafeFreeMemory(device, this->memory);
}
void IndexBuffer::destroy(VkDevice device)
{
	vkSafeDestroyBuffer(device, this->buffer);
	vkSafeFreeMemory(device, this->memory);
}

void AnimatedModel::loadTextures(tinygltf::Model& input)
{
	images_.resize(input.textures.size());
	for (size_t i = 0; i < input.textures.size(); i++) {
		tinygltf::Texture& gltfTexture = input.textures[i];
		int imageIndex = gltfTexture.source;
		if (imageIndex >= input.images.size()) 
			continue;

		tinygltf::Image& glTFImage = input.images[imageIndex];
		// Get the image data from the glTF loader
		unsigned char* buffer = nullptr;
		VkDeviceSize bufferSize = 0;
		bool deleteBuffer = false;
		// We convert RGB-only images to RGBA, as most devices don't support RGB-formats in Vulkan
		if (glTFImage.component == 3) {
			bufferSize = glTFImage.width * glTFImage.height * 4;
			buffer = new unsigned char[bufferSize];
			unsigned char* rgba = buffer;
			unsigned char* rgb = &glTFImage.image[0];
			for (size_t i = 0; i < glTFImage.width * glTFImage.height; ++i) {
				memcpy(rgba, rgb, sizeof(unsigned char) * 3);
				rgba += 4;
				rgb += 3;
			}
			deleteBuffer = true;
		}
		else {
			buffer = &glTFImage.image[0];
			bufferSize = glTFImage.image.size();
		}

		// Load texture from image buffer
		vks::Texture2D::SamplerOption samplerOpt;
		int samplerIndex = gltfTexture.sampler;
		if (samplerIndex < input.samplers.size()) {
			tinygltf::Sampler& gltfSampler = input.samplers[samplerIndex];
			OglToVulkan::convertSamplerWrap(gltfSampler.wrapS, samplerOpt.addressModeU);
			OglToVulkan::convertSamplerWrap(gltfSampler.wrapT, samplerOpt.addressModeV);
			OglToVulkan::convertSamplerWrap(gltfSampler.wrapR, samplerOpt.addressModeW);
			OglToVulkan::convertSamplerFilter(gltfSampler.magFilter, samplerOpt.magFilter, samplerOpt.mipmapMode);
			OglToVulkan::convertSamplerFilter(gltfSampler.minFilter, samplerOpt.minFilter, samplerOpt.mipmapMode);
		}

		images_[i].fromBuffer(buffer, bufferSize, VK_FORMAT_R8G8B8A8_UNORM, glTFImage.width, glTFImage.height,
			vulkanDevice_, queue_, VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 
			true, samplerOpt
		);

		if (deleteBuffer) delete[] buffer;
	}
}
void AnimatedModel::loadMaterials(tinygltf::Model& input, MaterialFactory& mtlFac)
{
	materials_.clear();
	for (size_t i = 0; i < input.materials.size(); i++) {
		tinygltf::Material glTFMaterial = input.materials[i];
		materials_.push_back(mtlFac.createMaterial(glTFMaterial));
	}
}

static void generateNormals(std::vector<AnimatedModel::Vertex>& vertex, uint32_t vertexStart, uint32_t vertexEnd,
	const std::vector<uint32_t>& indices, uint32_t indexStart, uint32_t indexEnd)
{
	for (int i = vertexStart; i < vertexEnd; ++i) {
		vertex[i].normal = glm::vec3(0);
	}

	for (size_t i = indexStart; i < indexEnd; i += 3) {
		uint32_t i0 = indices[i];
		uint32_t i1 = indices[i + 1];
		uint32_t i2 = indices[i + 2];

		const vec3& p0 = vertex[i0].pos;
		const vec3& p1 = vertex[i1].pos;;
		const vec3& p2 = vertex[i2].pos;;

		vec3 v10 = p1 - p0;
		vec3 v20 = p2 - p0;
		vec3 normal = glm::cross(v20, v10);

		vertex[i0].normal += normal;
		vertex[i1].normal += normal;
		vertex[i2].normal += normal;
	}

	for (int i = vertexStart; i < vertexEnd; ++i) {
		vertex[i].normal = glm::normalize(vertex[i].normal);
	}
}
static void generateTangents(std::vector<AnimatedModel::Vertex>& vertex, uint32_t vertexStart, uint32_t vertexEnd,
	const std::vector<uint32_t>& indices, uint32_t indexStart, uint32_t indexEnd) 
{
	for (int i = vertexStart; i < vertexEnd; ++i) {
		vertex[i].tangent = glm::vec3(0);
		vertex[i].tangent_w = 1;
	}

	for (size_t i = indexStart; i < indexEnd; i += 3) {
		uint32_t i0 = indices[i];
		uint32_t i1 = indices[i + 1];
		uint32_t i2 = indices[i + 2];

		const vec3& p0 = vertex[i0].pos;
		const vec3& p1 = vertex[i1].pos;;
		const vec3& p2 = vertex[i2].pos;;

		const vec2& uv0 = vertex[i0].uv;
		const vec2& uv1 = vertex[i1].uv;
		const vec2& uv2 = vertex[i2].uv;

		vec3 deltaPos1 = p1 - p0;
		vec3 deltaPos2 = p2 - p0;
		vec2 deltaUV1 = uv1 - uv0;
		vec2 deltaUV2 = uv2 - uv0;

		float f = 1.0f / (deltaUV1.x * deltaUV2.y - deltaUV1.y * deltaUV2.x);

		vec3 tangent = f * (deltaUV2.y * deltaPos1 - deltaUV1.y * deltaPos2);
		vertex[i0].tangent += tangent;
		vertex[i1].tangent += tangent;
		vertex[i2].tangent += tangent;
	}

	for (int i = vertexStart; i < vertexEnd; ++i) {
		vertex[i].tangent = glm::normalize(vertex[i].tangent);
		vertex[i].tangent_w = 1;
	}
}

void AnimatedModel::loadNode(const tinygltf::Node& inputNode, int nodeIndex, const tinygltf::Model& input, AnimatedModelNode* parent,
	std::vector<uint32_t>& indexBuffer, std::vector<Vertex>& vertexBuffer, bool flipY)
{
	AnimatedModelNode* node = new AnimatedModelNode{};
	node->parent = parent;
	node->bbox.reset();

	node->position = glm::vec3(0);
	node->rotation = glm::quat();
	node->scale = glm::vec3(1,1,1);
	node->matrix = glm::mat4(1.0);
	if (inputNode.translation.size() == 3) node->position = glm::make_vec3(inputNode.translation.data());
	if (inputNode.rotation.size() == 4) node->rotation = glm::make_quat(inputNode.rotation.data());
	if (inputNode.scale.size() == 3) node->scale = glm::make_vec3(inputNode.scale.data());
	if (inputNode.matrix.size() == 16) node->matrix = glm::make_mat4x4(inputNode.matrix.data());
	node->localMatrix = Transform(node->position, node->rotation, node->scale).getMatrix() * node->matrix;
	node->bindMatrix = node->localMatrix;

	node->skinIndex = inputNode.skin;

	// Load node's children
	if (inputNode.children.size() > 0) {
		for (size_t i = 0; i < inputNode.children.size(); i++) {
			loadNode(input.nodes[inputNode.children[i]], inputNode.children[i], input, node, indexBuffer, vertexBuffer, flipY);
		}
	}

	// If the node contains mesh data, we load vertices and indices from the buffers
	// In glTF this is done via accessors and buffer views
	if (inputNode.mesh > -1) {
		const tinygltf::Mesh mesh = input.meshes[inputNode.mesh];
		// Iterate through all primitives of this node's mesh
		for (size_t i = 0; i < mesh.primitives.size(); i++) {
			const tinygltf::Primitive& glTFPrimitive = mesh.primitives[i];
			uint32_t firstIndex = static_cast<uint32_t>(indexBuffer.size());
			uint32_t vertexStart = static_cast<uint32_t>(vertexBuffer.size());
			uint32_t indexCount = 0;
			const float* normalsBuffer = nullptr;
			const float* tangentsBuffer = nullptr;
			std::vector<double> vertexMin, vertexMax;
			// Vertices
			{
				const float* positionBuffer = nullptr;
				const float* texCoordsBuffer = nullptr;
				const float* texCoordsBuffer1 = nullptr;
				const uint16_t* jointIndicesBuffer = nullptr;
				const float* jointWeightsBuffer = nullptr;
				size_t tangentsStride = 0;
				size_t vertexCount = 0;

				// Get buffer data for vertex positions
				if (glTFPrimitive.attributes.find("POSITION") != glTFPrimitive.attributes.end()) {
					const tinygltf::Accessor& accessor = input.accessors[glTFPrimitive.attributes.find("POSITION")->second];
					const tinygltf::BufferView& view = input.bufferViews[accessor.bufferView];
					positionBuffer = reinterpret_cast<const float*>(&(input.buffers[view.buffer].data[accessor.byteOffset + view.byteOffset]));
					vertexCount = accessor.count;
					for (auto v : accessor.minValues)
						vertexMin.push_back(v);
					for (auto v : accessor.maxValues)
						vertexMax.push_back(v);
				}
				// Get buffer data for vertex normals
				if (glTFPrimitive.attributes.find("NORMAL") != glTFPrimitive.attributes.end()) {
					const tinygltf::Accessor& accessor = input.accessors[glTFPrimitive.attributes.find("NORMAL")->second];
					const tinygltf::BufferView& view = input.bufferViews[accessor.bufferView];
					normalsBuffer = reinterpret_cast<const float*>(&(input.buffers[view.buffer].data[accessor.byteOffset + view.byteOffset]));
				}
				// Get buffer data for vertex tangents
				if (glTFPrimitive.attributes.find("TANGENT") != glTFPrimitive.attributes.end()) {
					const tinygltf::Accessor& accessor = input.accessors[glTFPrimitive.attributes.find("TANGENT")->second];
					const tinygltf::BufferView& view = input.bufferViews[accessor.bufferView];
					if (view.byteStride % 4 == 0) {
						tangentsStride = view.byteStride / 4;
						tangentsBuffer = reinterpret_cast<const float*>(&(input.buffers[view.buffer].data[accessor.byteOffset + view.byteOffset]));
					}
				}
				// Get buffer data for vertex texture coordinates
				// glTF supports multiple sets, we only load the first one
				if (glTFPrimitive.attributes.find("TEXCOORD_0") != glTFPrimitive.attributes.end()) {
					const tinygltf::Accessor& accessor = input.accessors[glTFPrimitive.attributes.find("TEXCOORD_0")->second];
					const tinygltf::BufferView& view = input.bufferViews[accessor.bufferView];
					texCoordsBuffer = reinterpret_cast<const float*>(&(input.buffers[view.buffer].data[accessor.byteOffset + view.byteOffset]));
				}
				if (glTFPrimitive.attributes.find("TEXCOORD_1") != glTFPrimitive.attributes.end()) {
					const tinygltf::Accessor& accessor = input.accessors[glTFPrimitive.attributes.find("TEXCOORD_1")->second];
					const tinygltf::BufferView& view = input.bufferViews[accessor.bufferView];
					texCoordsBuffer1 = reinterpret_cast<const float*>(&(input.buffers[view.buffer].data[accessor.byteOffset + view.byteOffset]));
				}

				if (glTFPrimitive.attributes.find("JOINTS_0") != glTFPrimitive.attributes.end())
				{
					const tinygltf::Accessor& accessor = input.accessors[glTFPrimitive.attributes.find("JOINTS_0")->second];
					const tinygltf::BufferView& view = input.bufferViews[accessor.bufferView];
					jointIndicesBuffer = reinterpret_cast<const uint16_t*>(&(input.buffers[view.buffer].data[accessor.byteOffset + view.byteOffset]));
				}
				// Get vertex joint weights
				if (glTFPrimitive.attributes.find("WEIGHTS_0") != glTFPrimitive.attributes.end())
				{
					const tinygltf::Accessor& accessor = input.accessors[glTFPrimitive.attributes.find("WEIGHTS_0")->second];
					const tinygltf::BufferView& view = input.bufferViews[accessor.bufferView];
					jointWeightsBuffer = reinterpret_cast<const float*>(&(input.buffers[view.buffer].data[accessor.byteOffset + view.byteOffset]));
				}

				// Append data to model's vertex buffer
				bool hasSkin = (jointIndicesBuffer && jointWeightsBuffer);
				for (size_t v = 0; v < vertexCount; v++) {
					Vertex vert{};
					vert.pos = glm::vec4(glm::make_vec3(&positionBuffer[v * 3]), 1.0f);
					if (normalsBuffer) vert.normal = glm::normalize(glm::make_vec3(&normalsBuffer[v * 3]));
					if (tangentsBuffer) {
						vert.tangent = glm::normalize(glm::make_vec3(&tangentsBuffer[v * tangentsStride]));
						if (tangentsStride >= 4) vert.tangent_w = tangentsBuffer[v * tangentsStride + 3];
						else vert.tangent_w = 1;
					}
					vert.color = -1;
					vert.uv = texCoordsBuffer ? glm::make_vec2(&texCoordsBuffer[v * 2]) : glm::vec3(0.0f);
					vert.uv1 = texCoordsBuffer1 ? glm::make_vec2(&texCoordsBuffer1[v * 2]) : glm::vec3(0.0f);
					vert.blendIndex = hasSkin ? uchar4(jointIndicesBuffer[v * 4], jointIndicesBuffer[v * 4 + 1], jointIndicesBuffer[v * 4 + 2], jointIndicesBuffer[v * 4 + 3]) : uchar4(0);
					vert.blendWeight = hasSkin ? glm::make_vec4(&jointWeightsBuffer[v * 4]) : glm::vec4(0.0f);
					if (flipY) {
						vert.pos.y = -vert.pos.y;
						vert.normal.y = -vert.normal.y;
					}
					vertexBuffer.push_back(vert);
				}
			}
			// Indices
			{
				const tinygltf::Accessor& accessor = input.accessors[glTFPrimitive.indices];
				const tinygltf::BufferView& bufferView = input.bufferViews[accessor.bufferView];
				const tinygltf::Buffer& buffer = input.buffers[bufferView.buffer];

				indexCount += static_cast<uint32_t>(accessor.count);

				// glTF supports different component types of indices
				switch (accessor.componentType) {
				case TINYGLTF_PARAMETER_TYPE_UNSIGNED_INT: {
					const uint32_t* buf = reinterpret_cast<const uint32_t*>(&buffer.data[accessor.byteOffset + bufferView.byteOffset]);
					for (size_t index = 0; index < accessor.count; index++) {
						indexBuffer.push_back(buf[index] + vertexStart);
					}
					break;
				}
				case TINYGLTF_PARAMETER_TYPE_UNSIGNED_SHORT: {
					const uint16_t* buf = reinterpret_cast<const uint16_t*>(&buffer.data[accessor.byteOffset + bufferView.byteOffset]);
					for (size_t index = 0; index < accessor.count; index++) {
						indexBuffer.push_back(buf[index] + vertexStart);
					}
					break;
				}
				case TINYGLTF_PARAMETER_TYPE_UNSIGNED_BYTE: {
					const uint8_t* buf = reinterpret_cast<const uint8_t*>(&buffer.data[accessor.byteOffset + bufferView.byteOffset]);
					for (size_t index = 0; index < accessor.count; index++) {
						indexBuffer.push_back(buf[index] + vertexStart);
					}
					break;
				}
				default:
					std::cerr << "Index component type " << accessor.componentType << " not supported!" << std::endl;
					return;
				}
			}
			Primitive primitive{};
			primitive.firstIndex = firstIndex;
			primitive.indexCount = indexCount;
			primitive.materialIndex = glTFPrimitive.material;
			node->mesh.primitives.push_back(primitive);

			if (!normalsBuffer) generateNormals(vertexBuffer, vertexStart, vertexBuffer.size(), indexBuffer, firstIndex, indexBuffer.size());
			if (!tangentsBuffer) generateTangents(vertexBuffer, vertexStart, vertexBuffer.size(), indexBuffer, firstIndex, indexBuffer.size());

			auto& bbox = node->bbox;
			if (vertexMin.size() >= 3 && vertexMax.size() >= 3)
			{
				BoundingBox nodeBBox(toVec3(vertexMin), toVec3(vertexMax));
				bbox.merge(nodeBBox);
				primitive.bbox.merge(nodeBBox);
			}
			else
			{
				for (int i = vertexStart; i < vertexBuffer.size(); ++i) {
					bbox.merge(vertexBuffer[i].pos);
					primitive.bbox.merge(vertexBuffer[i].pos);
				}
			}
		}
	}

	if (parent) parent->children.push_back(node);
	else nodes_.push_back(node);

	if (node->bbox) {
		glm::vec3 center = node->bbox.center();
		float radius = glm::distance(node->bbox.max, center);
		node->bbox = BoundingBox(center - vec3(radius), center + vec3(radius));
	}

	node->nodeIndex = nodeIndex;
	if (nodeIndex >= nodeByIndex_.size()) nodeByIndex_.resize(nodeIndex + 1);
	nodeByIndex_[nodeIndex] = node;
}
void AnimatedModel::loadAnimations(tinygltf::Model& input)
{
	animations_.clear();
	for (auto& iAnimation : input.animations) {
		Animation ani;
		ani.load(input, iAnimation);
		animations_.push_back(ani);
	}
}
void AnimatedModel::loadSkins(tinygltf::Model& input)
{
	skins_.resize(input.skins.size());
	for (size_t i = 0; i < input.skins.size(); i++)
	{
		std::vector<AnimatedModelNode*> skinNodes;
		tinygltf::Skin glTFSkin = input.skins[i];

		auto& skinI = skins_[i];
		skinI.name = glTFSkin.name;
		skinI.rootNodeIndex = glTFSkin.skeleton;
		skinI.jointNodeIndexs = glTFSkin.joints;

		for (int j : skinI.jointNodeIndexs)
			skinNodes.push_back(nodeByIndex_[j]);

		// Get the inverse bind matrices from the buffer associated to this skin
		if (glTFSkin.inverseBindMatrices > -1)
		{
			const tinygltf::Accessor& accessor = input.accessors[glTFSkin.inverseBindMatrices];
			const tinygltf::BufferView& bufferView = input.bufferViews[accessor.bufferView];
			const tinygltf::Buffer& buffer = input.buffers[bufferView.buffer];
			skinI.inverseBindMatrices.resize(accessor.count);
			memcpy(skinI.inverseBindMatrices.data(), &buffer.data[accessor.byteOffset + bufferView.byteOffset], accessor.count * sizeof(glm::mat4));

			// Store inverse bind matrices for this skin in a shader storage buffer object
			// To keep this sample simple, we create a host visible shader storage buffer
			VK_CHECK_RESULT(vulkanDevice_->createBuffer(
				VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
				&skinI.ssbo, sizeof(glm::mat4) * skinI.inverseBindMatrices.size(), skinI.inverseBindMatrices.data())
			);
			VK_CHECK_RESULT(skinI.ssbo.map());
		}

		const VkDescriptorSetAllocateInfo allocInfo = vks::initializers::descriptorSetAllocateInfo(descriptorPool_, &skeletonDSLayout, 1);
		VK_CHECK_RESULT(vkAllocateDescriptorSets(vulkanDevice_->logicalDevice, &allocInfo, &skinI.descriptorSet));
	}
	if (skins_.empty())
	{
		auto& skinI = dummySkin_;

		skinI.inverseBindMatrices.clear();
		skinI.inverseBindMatrices.push_back(glm::mat4(1.0f));

		VK_CHECK_RESULT(vulkanDevice_->createBuffer(
			VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
			&skinI.ssbo, sizeof(glm::mat4) * skinI.inverseBindMatrices.size(), skinI.inverseBindMatrices.data())
		);
		VK_CHECK_RESULT(skinI.ssbo.map());

		const VkDescriptorSetAllocateInfo allocInfo = vks::initializers::descriptorSetAllocateInfo(descriptorPool_, &skeletonDSLayout, 1);
		VK_CHECK_RESULT(vkAllocateDescriptorSets(vulkanDevice_->logicalDevice, &allocInfo, &skinI.descriptorSet));
	}
}

#if 0
void AnimatedModel::drawNode(VkCommandBuffer commandBuffer, VkPipelineLayout pipelineLayout, AnimatedModelNode* node)
{
	if (node->mesh.primitives.size() > 0) {
		glm::mat4 nodeMatrix = node->getWorldMatrix();
		vkCmdPushConstants(commandBuffer, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(glm::mat4), &nodeMatrix);

		for (Primitive& primitive : node->mesh.primitives) {
			if (primitive.indexCount > 0) {
				const Material& mtl = materials[primitive.materialIndex];
				vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, MATERIAL_SET, 1, &mtl.descriptorSet, 0, nullptr);

				vkCmdDrawIndexed(commandBuffer, primitive.indexCount, 1, primitive.firstIndex, 0, 0);
			}
		}
	}
	for (auto& child : node->children) {
		drawNode(commandBuffer, pipelineLayout, child);
	}
}
void AnimatedModel::draw(VkCommandBuffer commandBuffer, VkPipelineLayout pipelineLayout)
{
	// All vertices and indices are stored in single buffers, so we only need to bind once
	VkDeviceSize offsets[1] = { 0 };
	vkCmdBindVertexBuffers(commandBuffer, 0, 1, &vertices.buffer, offsets);
	vkCmdBindIndexBuffer(commandBuffer, indices.buffer, 0, VK_INDEX_TYPE_UINT32);
	// Render all nodes at top-level
	for (auto& node : nodes) {
		drawNode(commandBuffer, pipelineLayout, node);
	}
}
#endif

struct CompareDrawableDepth
{
	bool operator()(const Drawable& l, const Drawable& r) const {
		return l.depth_ > r.depth_;
	}
};
void DrawableQueueGroup::sortTransmissionQueueByDepth()
{
	std::stable_sort(transmissionQueue_.begin(), transmissionQueue_.end(), CompareDrawableDepth());
}
void DrawableQueueGroup::sortTransparentQueueByDepth()
{
	std::stable_sort(transparentQueue_.begin(), transparentQueue_.end(), CompareDrawableDepth());
}
void Drawable::draw(VkCommandBuffer commandBuffer, VkPipelineLayout pipelineLayout)
{
	if (!isValid()) return;
	vkCmdPushConstants(commandBuffer, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pushConstant_), &pushConstant_);
	vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, MATERIAL_SET, descriptorSets_.size(), descriptorSets_.data(), 0, nullptr);
	vkCmdDrawIndexed(commandBuffer, indexCount_, 1, firstIndex_, 0, 0);
}

void AnimatedModel::getDrawableQueueGroupByNode(DrawableQueueGroup& drwQueGrp, Camera1& camera, AnimatedModelNode* node)
{
	if (node->mesh.primitives.size() > 0) {
		glm::mat4 modelMatrix = node->getWorldMatrix();

		for (Primitive& primitive : node->mesh.primitives) {
			if (primitive.indexCount > 0) {
				const Material& mtl = materials_[primitive.materialIndex];
				
				Drawable drawable;
				drawable.firstIndex_ = primitive.firstIndex;
				drawable.indexCount_ = primitive.indexCount;
				
				drawable.descriptorSets_[0] = mtl.descriptorSet;
				drawable.descriptorSets_[1] = (node->skinIndex < skins_.size()) ? skins_[node->skinIndex].descriptorSet : dummySkin_.descriptorSet;
				drawable.pushConstant_.u_ModelMatrix = modelMatrix;

				glm::mat4 mvp = camera.getViewProjectionMatrix() * modelMatrix;
				glm::vec4 pos = mvp * glm::vec4(primitive.bbox.center(), 1.0f);
				drawable.depth_ = pos[2];

				if (mtl.params.isFeatureEnabled(MTL_TEX_TRANSMISSION_BINDING)) {
					drawable.type_ = kDrawble_Transmission;
					drwQueGrp.transmissionQueue_.push_back(drawable);
				}
				else {
					if (mtl.params.u_AlphaMode == ALPHAMODE_OPAQUE) {
						drawable.type_ = kDrawble_Opaque;
						drwQueGrp.opaqueQueue_.push_back(drawable);
					}
					else {
						drawable.type_ = kDrawble_Transparent;
						drwQueGrp.transparentQueue_.push_back(drawable);
					}
				}
			}
		}
	}
	for (auto& child : node->children) {
		getDrawableQueueGroupByNode(drwQueGrp, camera, child);
	}
}
void AnimatedModel::getDrawableQueueGroup(DrawableQueueGroup& drwQueGrp, Camera1& camera)
{
	for (auto& node : nodes_) {
		getDrawableQueueGroupByNode(drwQueGrp, camera, node);
	}
}

glm::mat4 AnimatedModelNode::getWorldMatrix() const
{
	glm::mat4 nodeMatrix = this->localMatrix;
	AnimatedModelNode* currentParent = this->parent;
	while (currentParent) {
		nodeMatrix = currentParent->localMatrix * nodeMatrix;
		currentParent = currentParent->parent;
	}
	return nodeMatrix;
}
BoundingBox AnimatedModelNode::getWorldBBox() const
{
	BoundingBox worldBBox = bbox.transform(getWorldMatrix());
	for (auto child : children)
		worldBBox.merge(child->getWorldBBox());
	return worldBBox;
}
BoundingBox AnimatedModel::getWorldBBox() const
{
	BoundingBox worldBBox;
	for (auto node : nodes_) {
		worldBBox.merge(node->getWorldBBox());
	}
	return worldBBox;
}

std::vector<VkVertexInputAttributeDescription> AnimatedModel::getVertexAttributesDesc() const
{
	const std::vector<VkVertexInputAttributeDescription> vertexInputAttributes = {
		vks::initializers::vertexInputAttributeDescription(0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(AnimatedModel::Vertex, pos)),				// Location 0: Position
		vks::initializers::vertexInputAttributeDescription(0, 1, VK_FORMAT_R32G32B32_SFLOAT, offsetof(AnimatedModel::Vertex, normal)),			// Location 1: Normal
		vks::initializers::vertexInputAttributeDescription(0, 2, VK_FORMAT_R8G8B8A8_UNORM, offsetof(AnimatedModel::Vertex, color)),				// Location 2: Color
		vks::initializers::vertexInputAttributeDescription(0, 3, VK_FORMAT_R32G32B32_SFLOAT, offsetof(AnimatedModel::Vertex, uv)),				// Location 3: Texture coordinates
		vks::initializers::vertexInputAttributeDescription(0, 4, VK_FORMAT_R32G32B32_SFLOAT, offsetof(AnimatedModel::Vertex, uv1)),				// Location 4: Texture coordinates
		vks::initializers::vertexInputAttributeDescription(0, 5, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(AnimatedModel::Vertex, tangent)),		// Location 5: Tangent
		vks::initializers::vertexInputAttributeDescription(0, 6, VK_FORMAT_R8G8B8A8_UINT, offsetof(AnimatedModel::Vertex, blendIndex)),		// Location 6: Blend Index
		vks::initializers::vertexInputAttributeDescription(0, 7, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(AnimatedModel::Vertex, blendWeight)),  // Location 7: Blend Weight
	};
	return vertexInputAttributes;
}

#define Eplison 1e-5
static float clamp(float minv, float maxv, float v) { return std::max(minv, std::min(minv, v)); }
static bool selectFrameByTime(const std::vector<float>& times, float curTime, int& frameIndex, float& fParam)
{
	if (times.empty()) return false;

	if (curTime <= times[0]) {
		frameIndex = 0;
		fParam = 0;
		return true;
	}
	for (int idx = 1; idx < times.size(); ++idx) {
		if (curTime < times[idx] && times[idx] - times[idx - 1] > Eplison) {
			frameIndex = idx - 1;
			fParam = (curTime - times[idx - 1]) / (times[idx] - times[idx - 1]);
			return true;
		}
	}
	frameIndex = times.size();
	fParam = 0;
	return true;
}
void AnimatedModel::updateJoints(AnimatedModelNode* node)
{
	if (node->skinIndex > -1)
	{
		glm::mat4 inverseTransform = glm::inverse(node->getWorldMatrix());
		
		Skin& skin = skins_[node->skinIndex];
		std::vector<glm::mat4> jointMatrices(skin.jointNodeIndexs.size(), glm::mat4(1.0));
		for (int i = 0; i < skin.jointNodeIndexs.size(); i++) {
			int nodeIndex = skin.jointNodeIndexs[i];
			if (nodeIndex > nodeByIndex_.size()) continue;

			jointMatrices[i] = inverseTransform * nodeByIndex_[nodeIndex]->getWorldMatrix() * skin.inverseBindMatrices[i];
		}

		// Update ssbo
		skin.ssbo.copyTo(jointMatrices.data(), jointMatrices.size() * sizeof(glm::mat4));
	}

	for (auto& child : node->children) 
	{
		updateJoints(child);
	}
}
void AnimatedModel::setAnimationTime(float currentTime)
{
	if (animationIndex_ >= animations_.size()) return;
	auto& animation = animations_[animationIndex_];
	if (animation.getDuration() <= Eplison) return;

	currentTime = fmod(currentTime, animation.getDuration());
	for (int i = 0; i < animation.getTrackCount(); ++i)
	{
		const AnimationTrack& track = animation.getTrackByIndex(i);
		if (track.nodeIndex >= nodeByIndex_.size()) continue;
		AnimatedModelNode* trackNode = nodeByIndex_[track.nodeIndex];

		for (int i = 0; i < track.samplers.size(); ++i) 
		{
			const AnimationSampler& sample = track.samplers[i];
			if (!sample) continue;

			int frameIndex = 0;
			float fParam = 0;
			if (!selectFrameByTime(sample.times, currentTime, frameIndex, fParam))
				continue;

			switch ((EAnimationTargetPath)i)
			{
			case kATPath_Translation: {
				if (frameIndex < sample.translation.size())
					trackNode->position = (glm::mix(sample.translation[frameIndex], sample.translation[frameIndex + 1], fParam));
				else
					trackNode->position = (sample.translation.back());
			}break;
			case kATPath_Rotation: {
				if (frameIndex < sample.rotation.size())
					trackNode->rotation = (glm::slerp(sample.rotation[frameIndex], sample.rotation[frameIndex + 1], fParam));
				else
					trackNode->rotation = (sample.rotation.back());
			}break;
			case kATPath_Scale: {
				if (frameIndex < sample.scale.size())
					trackNode->scale = (glm::mix(sample.scale[frameIndex], sample.scale[frameIndex + 1], fParam));
				else
					trackNode->scale = (sample.scale.back());
			}break;
			case kATPath_Weight:
			default:
				break;
			}
		}

		Transform transform(trackNode->position, trackNode->rotation, trackNode->scale);
		trackNode->localMatrix = transform.getMatrix() * trackNode->matrix;
	}

	for (auto& node : nodes_)
	{
		updateJoints(node);
	}
}

void AnimatedModel::setAnimationIndex(int animationIndex)
{
	if (animationIndex_ != animationIndex) {
		animationIndex_ = animationIndex;
		setAnimationTime(0);
	}
}
std::vector<std::string> AnimatedModel::getAnimationNames() const
{
	std::vector<std::string> vec;
	for (auto ani : animations_) {
		vec.push_back(ani.getName());
	}
	return vec;
}

bool AnimatedModel::load(tinygltf::Model& glTFInput, MaterialFactory& mtlFac, bool flipY)
{
	reset();

	this->loadMaterials(glTFInput, mtlFac);
	this->loadTextures(glTFInput);
	std::vector<uint32_t> indexBuffer;
	std::vector<AnimatedModel::Vertex> vertexBuffer;
	const tinygltf::Scene& scene = glTFInput.scenes[0];
	for (size_t i = 0; i < scene.nodes.size(); i++) {
		const tinygltf::Node& node = glTFInput.nodes[scene.nodes[i]];
		this->loadNode(node, scene.nodes[i], glTFInput, nullptr, indexBuffer, vertexBuffer,flipY);
	}
	this->loadAnimations(glTFInput);
	this->loadSkins(glTFInput);

	// Create and upload vertex and index buffer
	// We will be using one single vertex buffer and one single index buffer for the whole glTF scene
	// Primitives (of the glTF model) will then index into these using index offsets
	size_t vertexBufferSize = vertexBuffer.size() * sizeof(AnimatedModel::Vertex);
	size_t indexBufferSize = indexBuffer.size() * sizeof(uint32_t);
	this->indices.count = static_cast<uint32_t>(indexBuffer.size());

	struct StagingBuffer {
		VkBuffer buffer;
		VkDeviceMemory memory;
	} vertexStaging, indexStaging;

	// Create host visible staging buffers (source)
	VK_CHECK_RESULT(vulkanDevice_->createBuffer(
		VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
		vertexBufferSize,
		&vertexStaging.buffer,
		&vertexStaging.memory,
		vertexBuffer.data()));
	// Index data
	VK_CHECK_RESULT(vulkanDevice_->createBuffer(
		VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
		indexBufferSize,
		&indexStaging.buffer,
		&indexStaging.memory,
		indexBuffer.data()));

	// Create device local buffers (target)
	VK_CHECK_RESULT(vulkanDevice_->createBuffer(
		VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
		vertexBufferSize,
		&this->vertices.buffer,
		&this->vertices.memory));
	VK_CHECK_RESULT(vulkanDevice_->createBuffer(
		VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
		indexBufferSize,
		&this->indices.buffer,
		&this->indices.memory));

	// Copy data from staging buffers (host) do device local buffer (gpu)
	VkCommandBuffer copyCmd = vulkanDevice_->createCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);
	VkBufferCopy copyRegion = {};

	copyRegion.size = vertexBufferSize;
	vkCmdCopyBuffer(
		copyCmd,
		vertexStaging.buffer,
		this->vertices.buffer,
		1,
		&copyRegion);

	copyRegion.size = indexBufferSize;
	vkCmdCopyBuffer(
		copyCmd,
		indexStaging.buffer,
		this->indices.buffer,
		1,
		&copyRegion);

	vulkanDevice_->flushCommandBuffer(copyCmd, queue_, true);

	// Free staging resources
	auto device = vulkanDevice_->logicalDevice;
	vkDestroyBuffer(device, vertexStaging.buffer, nullptr);
	vkFreeMemory(device, vertexStaging.memory, nullptr);
	vkDestroyBuffer(device, indexStaging.buffer, nullptr);
	vkFreeMemory(device, indexStaging.memory, nullptr);
	return true;
}

void AnimatedModel::uploadDescriptorSet2Gpu(std::vector<VkWriteDescriptorSet>& writeParams)
{
	for (auto& mtl : materials_) {
		mtl.uploadDescriptorSet2Gpu(images_, writeParams);
	}
	for (auto& skin : skins_) {
		writeParams.push_back(vks::initializers::writeDescriptorSet(skin.descriptorSet, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 0, &skin.ssbo.descriptor));
	}
	if (skins_.empty()) {
		writeParams.push_back(vks::initializers::writeDescriptorSet(dummySkin_.descriptorSet, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 0, &dummySkin_.ssbo.descriptor));
	}
}