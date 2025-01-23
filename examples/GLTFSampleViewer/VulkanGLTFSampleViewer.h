#pragma once
#include "GltfShaderStruct.h"
#include "Material.h"
#include "AnimatedModel.h"
#include "Camera.h"
#include "Light.h"
#include "Enviroment.h"
#include "VulkanFrameBuffer.hpp"

struct MultiSampleTarget 
{
	struct {
		VkImage image = VK_NULL_HANDLE;
		VkImageView view = VK_NULL_HANDLE;
		VkDeviceMemory memory = VK_NULL_HANDLE;
	} color;
	struct {
		VkImage image = VK_NULL_HANDLE;
		VkImageView view = VK_NULL_HANDLE;
		VkDeviceMemory memory = VK_NULL_HANDLE;
	} depth;

	void destroy(VkDevice device);
};

struct ConstantValue 
{
	int TONEMAP = TONEMAP_KHR_PBR_NEUTRAL;
	int USE_IBL = 1;
	int USE_PUNCTUAL = 1;
	int DEBUG1 = 0;
	int USE_SKELETON = 0;

	// Hash function
	struct Hash {
		size_t operator()(const ConstantValue& cv) const {
			size_t hashValue = 0;
			hashValue ^= std::hash<int>{}(cv.TONEMAP) + 0x9e3779b9 + (hashValue << 6) + (hashValue >> 2);
			hashValue ^= std::hash<int>{}(cv.USE_IBL) + 0x9e3779b9 + (hashValue << 6) + (hashValue >> 2);
			hashValue ^= std::hash<int>{}(cv.USE_PUNCTUAL) + 0x9e3779b9 + (hashValue << 6) + (hashValue >> 2);
			hashValue ^= std::hash<int>{}(cv.DEBUG1) + 0x9e3779b9 + (hashValue << 6) + (hashValue >> 2);
			hashValue ^= std::hash<int>{}(cv.USE_SKELETON) + 0x9e3779b9 + (hashValue << 6) + (hashValue >> 2);
			return hashValue;
		}
	};

	// Equality operator
	bool operator==(const ConstantValue& other) const {
		return TONEMAP == other.TONEMAP &&
			USE_IBL == other.USE_IBL &&
			USE_PUNCTUAL == other.USE_PUNCTUAL &&
			DEBUG1 == other.DEBUG1 &&
			USE_SKELETON == other.USE_SKELETON;
	}

	// Less-than operator (used for ordering in std::map and std::set)
	bool operator<(const ConstantValue& other) const {
		return std::tie(TONEMAP, USE_IBL, USE_PUNCTUAL, DEBUG1, USE_SKELETON)
			< std::tie(other.TONEMAP, other.USE_IBL, other.USE_PUNCTUAL, other.DEBUG1, USE_SKELETON);
	}
};

class VulkanGLTFSampleViewer : public VulkanExampleBase
{
public:
	bool wireframe_ = false;
	bool showEnviromentMap_ = true;

	ConstantValue constantValue_;

	VkPipelineLayout modelPipelineLayout_ = VK_NULL_HANDLE;
	struct ModelPipeline {
		VkPipelineLayout layout = VK_NULL_HANDLE;
		VkPipeline solid = VK_NULL_HANDLE;
		VkPipeline wireframe = VK_NULL_HANDLE;
	};
	std::unordered_map<ConstantValue, ModelPipeline, ConstantValue::Hash> modelPipelineByConstant_;
	VkPipelineLayout skyboxPipelineLayout_ = VK_NULL_HANDLE;
	ModelPipeline skyboxPipeline_, skyboxLinearPipeline_;

	MaterialFactoryPtr mtlFac_;
	CameraFactoryPtr cameraFac_;
	CameraPtr userCamera_;
	LightManagerPtr lightMgr_;
	EnviromentPtr enviroment_;
	AnimatedModelPtr model_, skyBox_;

	float animationTime_ = 0.0f;
	bool cameraFixed_ = false;
	std::string modelName_, enviromentName_;
	bool modelGlb_ = false;

	struct ShaderName {
		std::string vertex;
		std::string pixel;
	};
	ShaderName modelShaderName_, skyboxShaderName_;

	MultiSampleTarget multisampleTarget_;
	VkExtent2D attachmentSize_{};
	bool useSampleShading_ = false;
	VkSampleCountFlagBits sampleCount_ = VK_SAMPLE_COUNT_1_BIT;

	std::shared_ptr<vks::Framebuffer> opaqueFramebuffer_;
	bool hasTransmission_ = false;
public:
	VulkanGLTFSampleViewer();
	~VulkanGLTFSampleViewer();

	void getEnabledFeatures() override;
	std::string getSampleShadersPath() const;
	bool isMSAAEnabled() const { return sampleCount_ != VK_SAMPLE_COUNT_1_BIT; }

#pragma region prepare
	void buildCommandBuffers() override;
	void setupRenderPass() override;
	void setupMultisampleTarget();
	void setupFrameBuffer() override;

	// Prepare and initialize uniform buffer containing shader uniforms
	void initSettings();
	void createOpaqueFramebuffer();
	bool initScene();
	bool createModelPipeline(const ConstantValue& cv, ModelPipeline& mpipe);
	bool createSkyboxPipeline(int TONEMAP, ModelPipeline& mpipe);
	void preparePipelines();

	void prepare();
#pragma endregion

#pragma region render
	void updateScene();
	void drawScene(int currentBuffer);
	void myRrenderFrame();
	void render() override;
#pragma endregion

	bool reloadEnviroment();
	bool reloadModel();
	void windowResized() override;
	void OnUpdateUIOverlay(vks::UIOverlay* overlay) override;

	void mouseMoved(double x, double y, int32_t mouseFlag, bool& handled) override;
	void mouseWheeled(short wheelDelta, bool& handled) override;
};


