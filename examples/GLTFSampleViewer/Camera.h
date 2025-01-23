#pragma once
#include "camera.hpp"
#include "GltfShaderStruct.h"

class CameraEx : public Camera
{
public:
	std::string name_;
	float distance_ = 1;
	float baseDistance_ = 1;

	float zoomExponent_ = 5;
	float zoomFactor_ = 0.01;
	float orbitSpeed_ = 1 / 180.0;
	float panSpeed_ = 1;
	glm::vec3 rotAround_;

public:
	CameraEx();
	void fitToScene(float aspect, glm::vec3 min, glm::vec3 max);
	void zoomBy(float value, glm::vec3 min, glm::vec3 max);
	void orbit(float x, float y);
	void pan(float x, float y);

	glm::vec3 getTarget() const;

public:
	void setDistanceFromTarget(float distance, glm::vec3 target);
	void fitDistanceToExtents(glm::vec3 min, glm::vec3 max);
	void fitCameraTargetToExtents(glm::vec3 min, glm::vec3 max);
	void fitPanSpeedToScene(glm::vec3 min, glm::vec3 max);
	void fitCameraPlanesToExtents(glm::vec3 min, glm::vec3 max);
};

class Camera1
{
	CameraUniforms params{};

	glm::vec3 sceneMin_, sceneMax_;
	std::vector<CameraEx> cameraList_;
	size_t cameraIndex_ = 0;

	vks::VulkanDevice* vulkanDevice_ = nullptr;
	VkDescriptorPool descriptorPool_ = VK_NULL_HANDLE;
public:
	VkDescriptorSet descriptorSet;
	vks::Buffer uniformBuffer;
	float Exposure = 1.0f;

public:
	Camera1();
	~Camera1();
	void load(float aspect, glm::vec3 min, glm::vec3 max, tinygltf::Model& gltfMdl);
	bool createHardware(vks::VulkanDevice* vulkanDevice, VkDescriptorPool descriptorPool, VkDescriptorSetLayout descriptorSetLayout);
	void destroy();

	void setCurrentIndex(int currentIndex);
	int getCurrentIndex() const { return cameraIndex_; }
	std::vector<std::string> getCameraNames() const;
	mat4 getViewProjectionMatrix() const;

	void zoomBy(float value);
	void orbit(float x, float y);
	void pan(float x, float y);

	void uploadParams2Gpu();
	void uploadDescriptorSet2Gpu(std::vector<VkWriteDescriptorSet>& writeParams);
};
using CameraPtr = std::shared_ptr<Camera1>;

class CameraFactory
{
	vks::VulkanDevice* vulkanDevice_ = nullptr;
	VkDevice device_ = VK_NULL_HANDLE;
	VkDescriptorPool descriptorPool_ = VK_NULL_HANDLE;

	std::vector<CameraPtr> trackedCameras_;
public:
	VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;

public:
	CameraFactory(vks::VulkanDevice* vulkanDevice, VkDescriptorPool descriptorPool);
	~CameraFactory() { destroy(); }
	void destroy();

	CameraPtr creatCamera(float aspect, glm::vec3 min, glm::vec3 max, tinygltf::Model& gltfMdl);
};
using CameraFactoryPtr = std::shared_ptr<CameraFactory>;