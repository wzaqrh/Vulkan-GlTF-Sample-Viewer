#pragma once
#include "gltfShaderStruct.h"
#include "Transform.h"
#include "BoundingBox.h"
#include "Material.h"
#include "Animation.h"

enum DrawableType 
{
	kDrawble_Opaque,
	kDrawble_Transmission,
	kDrawble_Transparent,
	kDrawble_Max
};
struct Drawable
{
	DrawableType type_ = kDrawble_Opaque;
	float depth_ = 0;

	PushConsts pushConstant_;
	uint32_t firstIndex_ = 0;
	uint32_t indexCount_ = 0;
	std::array<VkDescriptorSet, 2> descriptorSets_;

	bool isValid() const { return indexCount_; }
	operator bool() const { return isValid(); }
	void draw(VkCommandBuffer commandBuffer, VkPipelineLayout pipelineLayout);
};
struct DrawableQueueGroup
{
	std::vector<Drawable> opaqueQueue_;
	std::vector<Drawable> transmissionQueue_;
	std::vector<Drawable> transparentQueue_;

	void sortTransmissionQueueByDepth();
	void sortTransparentQueueByDepth();
};

struct Primitive 
{
	uint32_t firstIndex;
	uint32_t indexCount;
	int32_t materialIndex;
	BoundingBox bbox;
};
struct Mesh 
{
	std::vector<Primitive> primitives;
};

struct AnimatedModelNode
{
	int nodeIndex = -1;
	AnimatedModelNode* parent = nullptr;
	std::vector<AnimatedModelNode*> children;
	Mesh mesh;
	int skinIndex = -1;

	glm::vec3 position, scale;
	glm::quat rotation;
	glm::mat4 localMatrix, bindMatrix, matrix;
	BoundingBox bbox;

public:
	~AnimatedModelNode() {
		for (auto& child : children) {
			delete child;
		}
	}
	glm::mat4 getWorldMatrix() const;
	BoundingBox getWorldBBox() const;
};

struct Skin
{
	std::string				name;
	int						rootNodeIndex = -1;
	std::vector<glm::mat4>	inverseBindMatrices;
	std::vector<int>		jointNodeIndexs;
	vks::Buffer				ssbo;
	VkDescriptorSet			descriptorSet = VK_NULL_HANDLE;

	void destroy() { ssbo.destroy(); }

	bool isValid() const { return descriptorSet != VK_NULL_HANDLE; }
	operator bool() const { return isValid(); }
};

struct VertexBuffer
{
	VkBuffer buffer = VK_NULL_HANDLE;
	VkDeviceMemory memory = VK_NULL_HANDLE;

	void destroy(VkDevice device);
};

struct IndexBuffer
{
	int count = 0;
	VkBuffer buffer = VK_NULL_HANDLE;
	VkDeviceMemory memory = VK_NULL_HANDLE;

	void destroy(VkDevice device);
};

class Camera1;
class MaterialFactory;
class AnimatedModel
{
public:
	vks::VulkanDevice* vulkanDevice_ = nullptr;
	VkDescriptorPool descriptorPool_ = VK_NULL_HANDLE;
	VkQueue queue_ = VK_NULL_HANDLE;

	VkDescriptorSetLayout skeletonDSLayout = VK_NULL_HANDLE;

	struct Vertex 
	{
		glm::vec3 pos;
		glm::vec3 normal;
		glm::uint32 color;
		glm::vec2 uv;
		glm::vec2 uv1;
		glm::vec3 tangent;
		float tangent_w;
		uchar4 blendIndex;
		glm::vec4 blendWeight;
	};
	VertexBuffer vertices;
	IndexBuffer indices;

	using Image = vks::Texture2D;

	std::vector<Image> images_;
	std::vector<Material> materials_;
	std::vector<AnimatedModelNode*> nodes_, nodeByIndex_;
	std::vector<Skin> skins_;
	Skin dummySkin_;
	std::vector<Animation> animations_;
	int animationIndex_ = 0;

public:
	AnimatedModel(vks::VulkanDevice* vulkanDevice, VkDescriptorPool descriptorPool, VkQueue queue);
	~AnimatedModel();
	void reset();
	void destroy();
	bool load(tinygltf::Model& gltfMdl, MaterialFactory& mtlFac, bool flipY = false);
	
	// glTF loading functions
	void loadTextures(tinygltf::Model& input);
	void loadMaterials(tinygltf::Model& input, MaterialFactory& mtlFac);
	void loadNode(const tinygltf::Node& inputNode, int nodeIndex, const tinygltf::Model& input, AnimatedModelNode* parent, 
		std::vector<uint32_t>& indexBuffer, std::vector<AnimatedModel::Vertex>& vertexBuffer, bool flipY);
	void loadAnimations(tinygltf::Model& input);
	void loadSkins(tinygltf::Model& input);

	void uploadDescriptorSet2Gpu(std::vector<VkWriteDescriptorSet>& writeParams);
public:
#if 0
	void drawNode(VkCommandBuffer commandBuffer, VkPipelineLayout pipelineLayout, AnimatedModelNode* node);
	void draw(VkCommandBuffer commandBuffer, VkPipelineLayout pipelineLayout);
#endif
	void getDrawableQueueGroupByNode(DrawableQueueGroup& drwQueGrp, Camera1& camera, AnimatedModelNode* node);
	void getDrawableQueueGroup(DrawableQueueGroup& drwQueGrp, Camera1& camera);

	std::vector<VkVertexInputAttributeDescription> getVertexAttributesDesc() const;
	BoundingBox getWorldBBox() const;

public:
	void updateJoints(AnimatedModelNode* node);
	void setAnimationTime(float currentTime);
	void setAnimationIndex(int animationIndex);
	int getAnimationIndex() const { return animationIndex_; }
	std::vector<std::string> getAnimationNames() const;
	bool hasSkin() const { return skins_.size() > 0; }
};
using AnimatedModelPtr = std::shared_ptr<AnimatedModel>;