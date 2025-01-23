#include "Camera.h"
#include "GltfReadUtils.h"
#include <glm/gtx/matrix_decompose.hpp>

/**
 * CameraEx
 */
CameraEx::CameraEx()
{
	type = Camera::CameraType::lookat;
	this->flipY = true;
	this->position = glm::vec3(0.0f, 0.0f, -1.0f);
	this->rotation = glm::quat(glm::vec3(0.0f, 45.0f, 0.0f));
	this->fov = 45;
	this->znear = 0.1f;
	this->zfar = 256.0f;
	this->movementSpeed = 0.1;
	this->rotationSpeed = 0.1;
}
glm::vec3 CameraEx::getTarget() const
{
	return this->getPosition() + this->getLookDirection() * distance_;
}

void CameraEx::fitDistanceToExtents(glm::vec3 min, glm::vec3 max)
{
	const float maxAxisLength = std::max(max[0] - min[0], max[1] - min[1]);
	const float yfov = glm::radians(this->getFov());
	const float xfov = glm::radians(this->getFov() * this->getAspect());

	const float yZoom = maxAxisLength / 2 / std::tan(yfov / 2);
	const float xZoom = maxAxisLength / 2 / std::tan(xfov / 2);

	baseDistance_ = distance_ = std::max(xZoom, yZoom);
}
void CameraEx::setDistanceFromTarget(float distance, glm::vec3 target)
{
	glm::vec3 lookdir = this->getLookDirection();
	this->setPosition(target - lookdir * distance);
	distance_ = distance;
}
void CameraEx::fitCameraTargetToExtents(glm::vec3 min, glm::vec3 max)
{
	this->setRotation(glm::vec3(rotAround_.x, rotAround_.y, 0));

	glm::vec3 target = (max + min) * 0.5f;
	setDistanceFromTarget(distance_, target);
}
void CameraEx::fitPanSpeedToScene(glm::vec3 min, glm::vec3 max)
{
	const float longestDistance = glm::distance(min, max);
	constexpr float PanSpeedDenominator = 3500;
	panSpeed_ = longestDistance / PanSpeedDenominator;
}
void CameraEx::fitCameraPlanesToExtents(glm::vec3 min, glm::vec3 max)
{
	// depends only on scene min/max and the camera distance

	// Manually increase scene extent just for the camera planes to avoid camera clipping in most situations.
	const float longestDistance = 10 * glm::distance(min, max);
	float zNear = distance_ - (longestDistance * 0.6);
	float zFar = distance_ + (longestDistance * 0.6);

	// minimum near plane value needs to depend on far plane value to avoid z fighting or too large near planes
	constexpr float MaxNearFarRatio = 10000;
	zNear = std::max(zNear, zFar / MaxNearFarRatio);

	this->setPerspective(this->getFov(), this->getAspect(), zNear, zFar);
}
void CameraEx::fitToScene(float aspect, glm::vec3 min, glm::vec3 max)
{
	this->aspect = aspect;
	fitDistanceToExtents(min, max);
	fitCameraTargetToExtents(min, max);

	fitPanSpeedToScene(min, max);
	fitCameraPlanesToExtents(min, max);
}

void CameraEx::zoomBy(float value, glm::vec3 min, glm::vec3 max)
{
	glm::vec3 target = getTarget();

	// zoom exponentially
	float zoomDistance = std::pow(distance_ / baseDistance_, 1.0 / zoomExponent_);
	zoomDistance = std::max(zoomDistance + zoomFactor_ * -value / WHEEL_DELTA, 0.0001f);
	distance_ = std::pow(zoomDistance, zoomExponent_) * baseDistance_;

	setDistanceFromTarget(distance_, target);
	fitCameraPlanesToExtents(min, max);
}
void CameraEx::orbit(float x, float y)
{
	glm::vec3 target = getTarget();

	const float rotAroundXMax = M_PI / 2 - 0.01;
	rotAround_.y += (-x * orbitSpeed_);
	rotAround_.x += (-y * orbitSpeed_);
	rotAround_.x = glm::clamp(rotAround_.x, -rotAroundXMax, rotAroundXMax);
	this->setRotation(glm::vec3(rotAround_.x, rotAround_.y, 0));

	setDistanceFromTarget(distance_, target);
}
void CameraEx::pan(float x, float y)
{
	float scale = panSpeed_ * (distance_ / baseDistance_);
	glm::vec3 right = this->getRight() * -x * scale;
	glm::vec3 up = this->getUp() * -y * scale;

	glm::vec3 pos = this->getPosition() + up + right;
	this->setPosition(pos);
}

/**
 * UserCamera
 */
Camera1::Camera1()
{}
Camera1::~Camera1()
{
	destroy();
}

void Camera1::load(float aspect, glm::vec3 min, glm::vec3 max, tinygltf::Model& gltfMdl)
{
	sceneMin_ = min;
	sceneMax_ = max;

	cameraList_.clear();
	cameraList_.emplace_back();
	CameraEx& defaultCam = cameraList_.back();
	defaultCam.name_ = "User Camera";
	defaultCam.fitToScene(aspect, sceneMin_, sceneMax_);

	for (auto& cam : gltfMdl.cameras) 
	{
		if (cam.type != "perspective" || gltfMdl.nodes.empty()) continue;

		tinygltf::Camera* gltfCam = &cam;
		cameraList_.emplace_back();
		CameraEx& newCam = cameraList_.back();
		newCam.name_ = cam.name.empty() ? "Camera " + std::to_string(cameraList_.size() - 1) : cam.name;

		auto& psersCam = gltfCam->perspective;
		float fov = (psersCam.yfov != 0) ? glm::degrees(psersCam.yfov) : newCam.getFov();
		float znear = (psersCam.znear != 0) ? psersCam.znear : newCam.getNearClip();
		float zfar = (psersCam.zfar != 0) ? psersCam.zfar : newCam.getFarClip();
		newCam.setPerspective(fov, aspect, znear, zfar);

		int nodeIndex = 0;
		auto iter = std::find_if(gltfMdl.nodes.begin(), gltfMdl.nodes.end(), [gltfCam](const tinygltf::Node& node) {
			return node.name == gltfCam->name;
		});
		if (iter != gltfMdl.nodes.end()) {
			nodeIndex = iter - gltfMdl.nodes.begin();
		}
		auto& node = gltfMdl.nodes[nodeIndex];

		glm::vec3 translation;
		glm::quat rot;
		if (node.translation.size() >= 3 && node.rotation.size() >= 4) {
			translation = toVec3(node.translation);
			rot = toQuat(node.rotation);
		}
		else if (node.matrix.size() >= 16) {
			glm::mat4 mat = toMat4(node.matrix);
			glm::vec3 scale, skew;
			glm::vec4 perspective;
			glm::decompose(mat, scale, rot, translation, skew, perspective);
		}
		else if (node.matrix.size() >= 9) {
			glm::mat4 mat = toMat3(node.matrix);
			glm::vec3 scale, skew;
			glm::vec4 perspective;
			glm::decompose(mat, scale, rot, translation, skew, perspective);
		}

		newCam.setPosition(translation);
		newCam.setRotation(rot);

		glm::vec3 target = (max + min) * 0.5f;
		newCam.baseDistance_ = newCam.distance_ = glm::distance(target, translation);
		newCam.fitPanSpeedToScene(min, max);
	}
	cameraIndex_ = (cameraList_.size() > 1) ? 1 : 0;
	
}
bool Camera1::createHardware(vks::VulkanDevice* vulkanDevice, VkDescriptorPool descriptorPool, VkDescriptorSetLayout descriptorSetLayout)
{
	vulkanDevice_ = vulkanDevice;
	descriptorPool_ = descriptorPool;

	VkDescriptorSetAllocateInfo allocInfo = vks::initializers::descriptorSetAllocateInfo(descriptorPool, &descriptorSetLayout, 1);
	VK_CHECK_RESULT(vkAllocateDescriptorSets(vulkanDevice->logicalDevice, &allocInfo, &descriptorSet));
	
	VK_CHECK_RESULT(vulkanDevice->createBuffer(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &uniformBuffer, sizeof(params)));

	uploadParams2Gpu();
	return true;
}
void Camera1::destroy()
{
	//vkSafeFreeDescriptorSets(vulkanDevice_->logicalDevice, descriptorPool_, 1, descriptorSet);
	uniformBuffer.destroy();
}

void Camera1::zoomBy(float value)
{
	cameraList_[cameraIndex_].zoomBy(value, sceneMin_, sceneMax_);
}
void Camera1::orbit(float x, float y)
{
	cameraList_[cameraIndex_].orbit(x, y);
}
void Camera1::pan(float x, float y)
{
	cameraList_[cameraIndex_].pan(x, y);
}

void Camera1::uploadParams2Gpu()
{
	const auto& cam = cameraList_[cameraIndex_];
	params.u_ProjectionMatrix = cam.matrices.perspective;
	params.u_ViewMatrix = cam.matrices.view;
	params.u_Camera = toVec4(cam.position);// vec4(camera->position * -1.0f, 1.0);
	params.u_Exposure = Exposure;

#if 0
	mat4 mvp = params.u_ProjectionMatrix;
	glm::vec3 camp = glm::vec3(0);

	glm::vec4 p_near = mvp * vec4(camp.x, camp.y, camp.z - camera_->getNearClip(), 1);
	glm::vec3 ndc_near = p_near / p_near.w;
	assert(std::abs(ndc_near.z - 0) < 1e-3);

	glm::vec4 p_far = mvp * vec4(camp.x, camp.y, camp.z - camera_->getFarClip(), 1);
	glm::vec3 ndc_far = p_far / p_far.w;
	assert(std::abs(ndc_far.z - 1) < 1e-3);
	assert(std::abs(camera_->getNearClip()) < std::abs(camera_->getFarClip()));
#endif

	VK_CHECK_RESULT(uniformBuffer.map());
	memcpy(uniformBuffer.mapped, &params, sizeof(params));
	uniformBuffer.unmap();
}
void Camera1::uploadDescriptorSet2Gpu(std::vector<VkWriteDescriptorSet>& writeDescriptorSet)
{
	writeDescriptorSet.push_back(vks::initializers::writeDescriptorSet(descriptorSet, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, CAMERA_BINDING, &uniformBuffer.descriptor));
}

void Camera1::setCurrentIndex(int currentIndex)
{
	if (cameraIndex_ != currentIndex) {
		cameraIndex_ = currentIndex;
		uploadParams2Gpu();
	}
}
std::vector<std::string> Camera1::getCameraNames() const
{
	std::vector<std::string> nameList;
	for (auto& cam : cameraList_)
		nameList.push_back(cam.name_);
	return nameList;
}

mat4 Camera1::getViewProjectionMatrix() const
{
	return params.u_ProjectionMatrix * params.u_ViewMatrix;
}

/**
 * CameraFactory
 */
CameraFactory::CameraFactory(vks::VulkanDevice* vulkanDevice, VkDescriptorPool descriptorPool)
{
	vulkanDevice_ = vulkanDevice;
	device_ = vulkanDevice->logicalDevice;
	descriptorPool_ = descriptorPool;

	std::vector<VkDescriptorSetLayoutBinding> setLayoutBinding_Camera(1, vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, CAMERA_BINDING));
	VkDescriptorSetLayoutCreateInfo descriptorSetLayoutCI_Params = vks::initializers::descriptorSetLayoutCreateInfo(setLayoutBinding_Camera.data(), setLayoutBinding_Camera.size());
	VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device_, &descriptorSetLayoutCI_Params, nullptr, &descriptorSetLayout));
}
void CameraFactory::destroy()
{
	for (auto& cam : trackedCameras_)
		cam->destroy();
	trackedCameras_.clear();

	vkSafeDestroyDescriptorSetLayout(device_, descriptorSetLayout);
}

CameraPtr CameraFactory::creatCamera(float aspect, glm::vec3 min, glm::vec3 max, tinygltf::Model& gltfMdl)
{
	CameraPtr camera = std::make_shared<Camera1>();
	camera->load(aspect, min, max, gltfMdl);
	camera->createHardware(vulkanDevice_, descriptorPool_, descriptorSetLayout);
	return camera;
}
