#pragma once
#include "gltfShaderStruct.h"
#include "GltfReadUtils.h"

using MaterialFlag = unsigned;

class Material 
{
public:
	MaterialFlag mtl_flags = 0;
	std::string name;

	PbrMaterialUniforms params{};
	std::array<int, MATERIAL_TEXTURE_COUNT> textureIndexs;
	bool doubleSided = false;

	VkDescriptorSet descriptorSet;
	vks::Buffer uniformBuffer;

public:
	Material();
	void reset();
	void load(tinygltf::Material& gltfMtl);
	
	void createHardware(vks::VulkanDevice* vulkanDevice, VkDescriptorPool descriptorPool, VkDescriptorSetLayout dsLayout);
	void dispose();

	void uploadParams2Gpu();
	void uploadDescriptorSet2Gpu(const std::vector<vks::Texture2D>& imageByTexId, std::vector<VkWriteDescriptorSet>& writeParams);

	bool hasTexture(int binding) const;
private:
	void parseTextureTransform(int bindingIdx, const tinygltf::Value& extension);
	template<class TextureInfo> void parseTextureInfo(int bindingIdx, TextureInfo& texInfo)
	{
		bindingIdx -= MATERIAL_TEXTURE_FIRST_BINDING;

		textureIndexs[bindingIdx] = texInfo.index;
		params.getSamplerUVSet(bindingIdx) = (texInfo.index >= 0) ? texInfo.texCoord : -1;
		parseTextureTransform(bindingIdx, texInfo.extensions["KHR_texture_transform"]);
	}
	void parseTextureInfo(int bindingIdx, tinygltf::ExtensionMap& extensions, const std::string& extensionKey, const std::string& textureKey, bool checkEnable = false, float* pScaleFactor = nullptr)
	{
		if (!extensions.count(extensionKey)) return;
		auto& extension = extensions[extensionKey];

		bindingIdx -= MATERIAL_TEXTURE_FIRST_BINDING;
		if (checkEnable) params.getSamplerUVSet(bindingIdx) = std::max(params.getSamplerUVSet(bindingIdx), -1);

		if (!extension.Has(textureKey)) return;
		const auto& texInfo = extension.Get(textureKey);

		ExtensionReader::getValue(textureIndexs[bindingIdx], texInfo, "index");
		if (!ExtensionReader::getValue(params.getSamplerUVSet(bindingIdx), texInfo, "texCoord")) {
			params.getSamplerUVSet(bindingIdx) = 0;
		}
		if (pScaleFactor) {
			*pScaleFactor = 1;
			ExtensionReader::getValue(*pScaleFactor, texInfo, "scale");
		}
		if (texInfo.Has("extensions")) {
			const auto& texExtension = texInfo.Get("extensions");
			parseTextureTransform(bindingIdx, texExtension.Get("KHR_texture_transform"));
		}
	}
};

class MaterialFactory
{
	vks::VulkanDevice* vulkanDevice_ = nullptr;
	VkDevice device_ = VK_NULL_HANDLE;
	VkDescriptorPool descriptorPool_ = VK_NULL_HANDLE;
	VkDescriptorSetLayout descriptorSetLayout_ = VK_NULL_HANDLE;

public:
	MaterialFactory(vks::VulkanDevice* vulkanDevice, VkDescriptorPool descriptorPool);
	~MaterialFactory() { destroy(); }
	void destroy();
	Material createMaterial(tinygltf::Material& gltfMtl);

	VkDescriptorSetLayout getDescriptorSetLayout() const { return descriptorSetLayout_; }
};
using MaterialFactoryPtr = std::shared_ptr<MaterialFactory>;