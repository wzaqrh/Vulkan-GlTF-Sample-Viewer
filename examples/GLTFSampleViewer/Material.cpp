#include "Material.h"

void Material::createHardware(vks::VulkanDevice* vulkanDevice, VkDescriptorPool descriptorPool, VkDescriptorSetLayout dsLayout)
{
	VkDescriptorSetAllocateInfo allocInfo = vks::initializers::descriptorSetAllocateInfo(descriptorPool, &dsLayout, 1);
	VK_CHECK_RESULT(vkAllocateDescriptorSets(vulkanDevice->logicalDevice, &allocInfo, &descriptorSet));

	VK_CHECK_RESULT(vulkanDevice->createBuffer(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &uniformBuffer, sizeof(params)));
}
void Material::dispose()
{
	//vkSafeFreeDescriptorSets(device_, descriptorPool_, 1, descriptorSet);
	uniformBuffer.destroy();
}

Material::Material()
{
	textureIndexs.fill(-1);
	reset();
}
void Material::reset()
{
	mtl_flags = 0;

	mat3 mat3_identity;
	for (int i = 0; i < MATERIAL_TEXTURE_COUNT; ++i) {
		params.getSamplerUVTransform(i) = mat3_identity;
		params.getSamplerUVSet(i) = -2;
	}
	
	params.u_BaseColorFactor = vec4(1, 1, 1, 1);

	params.u_NormalScale = 1;
	params.u_MetallicFactor = 1;
	params.u_RoughnessFactor = 1;
	params.u_SheenRoughnessFactor = 0;

	params.u_SheenColorFactor = vec3(0, 0, 0);

	params.u_ClearcoatNormalScale = 1;
	params.u_ClearcoatFactor = 0;
	params.u_ClearcoatRoughnessFactor = 0;
	params.u_TransmissionFactor = 0;

	params.u_AttenuationColor = vec3(1, 1, 1);
	params.u_EmissiveFactor = vec3(0, 0, 0);

	params.u_ThicknessFactor = 0;
	params.u_AttenuationDistance = FLT_MAX;
	params.u_OcclusionStrength = 1;
	params.u_Ior = 1.5;

	params.u_AlphaCutoff = 0.5;

	params.u_IridescenceFactor = 0;
	params.u_IridescenceIor = 1.3;
	params.u_IridescenceThicknessMinimum = 100.0;
	params.u_IridescenceThicknessMaximum = 400.0;

	params.u_DiffuseTransmissionFactor = 0;
	params.u_DiffuseTransmissionColorFactor = vec3(1,1,1);

	constexpr float factor = 0;
	constexpr float rotation = 0;
	params.u_Anisotropy = vec3(std::cos(rotation), std::sin(rotation ), factor);

	params.u_Dispersion = 0;
	params.u_EmissiveStrength = 1;

	params.u_AlphaMode = 0;
}
void Material::parseTextureTransform(int bindingIdx, const tinygltf::Value& extension)
{
	//https://github.com/KhronosGroup/glTF/tree/main/extensions/2.0/Khronos/KHR_texture_transform
	if (!extension.IsObject()) return;

	vec2 offset = vec2(0, 0); ExtensionReader::getValue(offset, extension, "offset");
	float rotation = 0; ExtensionReader::getValue(rotation, extension, "rotation");
	vec2 scale = vec2(1, 1); ExtensionReader::getValue(scale, extension, "scale");

	float s = std::sin(rotation);
	float c = std::cos(rotation);
	glm::mat3 m_rotation = glm::mat3(
		c, -s, 0.0f,
		s, c, 0.0f,
		0.0f, 0.0f, 1.0f
	);
	glm::mat3 m_scale = glm::mat3(
		scale.x, 0.0f, 0.0f,
		0.0f, scale.y, 0.0f,
		0.0f, 0.0f, 1.0f
	);
	glm::mat3 m_translation = glm::mat3(
		1.0f, 0.0f, 0.0f,
		0.0f, 1.0f, 0.0f,
		offset.x, offset.y, 1.0f
	);
	params.getSamplerUVTransform(bindingIdx) = m_translation * m_rotation * m_scale;
}
void Material::load(tinygltf::Material& gltfMtl)
{
	reset();

	name = gltfMtl.name;

	params.u_BaseColorFactor = toVec4(gltfMtl.pbrMetallicRoughness.baseColorFactor);
	parseTextureInfo(MTL_TEX_BASE_COLOR_BINDING, gltfMtl.pbrMetallicRoughness.baseColorTexture);
	params.u_MetallicFactor = gltfMtl.pbrMetallicRoughness.metallicFactor;
	params.u_RoughnessFactor = gltfMtl.pbrMetallicRoughness.roughnessFactor;
	parseTextureInfo(MTL_TEX_METALLIC_ROUGHNESS_BINDING, gltfMtl.pbrMetallicRoughness.metallicRoughnessTexture);

	parseTextureInfo(MTL_TEX_NORMAL_BINDING, gltfMtl.normalTexture);
	params.u_NormalScale = gltfMtl.normalTexture.scale;
	parseTextureInfo(MTL_TEX_OCCLUSION_BINDING, gltfMtl.occlusionTexture);
	params.u_OcclusionStrength = gltfMtl.occlusionTexture.strength;
	parseTextureInfo(MTL_TEX_EMISSIVE_BINDING, gltfMtl.emissiveTexture);
	params.u_EmissiveFactor = toVec3(gltfMtl.emissiveFactor);
	params.u_AlphaCutoff = gltfMtl.alphaCutoff;
	doubleSided = gltfMtl.doubleSided;

	ExtensionReader rd(gltfMtl.extensions);
	rd.getExtensionValue(params.u_Ior, "KHR_materials_ior", "ior");

	rd.getExtensionValue(params.u_SheenColorFactor, "KHR_materials_sheen", "sheenColorFactor");
	parseTextureInfo(MTL_TEX_SHEEN_COLOR_BINDING, gltfMtl.extensions, "KHR_materials_sheen", "sheenColorTexture", true);
	rd.getExtensionValue(params.u_SheenRoughnessFactor, "KHR_materials_sheen", "sheenRoughnessFactor");
	parseTextureInfo(MTL_TEX_SHEEN_ROUGHNESS_BINDING, gltfMtl.extensions, "KHR_materials_sheen", "sheenRoughnessTexture");

	rd.getExtensionValue(params.u_ClearcoatFactor, "KHR_materials_clearcoat", "clearcoatFactor");
	parseTextureInfo(MTL_TEX_CLEARCOAT_BINDING, gltfMtl.extensions, "KHR_materials_clearcoat", "clearcoatTexture", true);
	rd.getExtensionValue(params.u_ClearcoatRoughnessFactor, "KHR_materials_clearcoat", "clearcoatRoughnessFactor");
	parseTextureInfo(MTL_TEX_CLEARCOAT_ROUGHNESS_BINDING, gltfMtl.extensions, "KHR_materials_clearcoat", "clearcoatRoughnessTexture");
	parseTextureInfo(MTL_TEX_CLEARCOAT_NORMAL_BINDING, gltfMtl.extensions, "KHR_materials_clearcoat", "clearcoatNormalTexture", &params.u_ClearcoatNormalScale);

	rd.getExtensionValue(params.u_TransmissionFactor, "KHR_materials_transmission", "transmissionFactor");
	parseTextureInfo(MTL_TEX_TRANSMISSION_BINDING, gltfMtl.extensions, "KHR_materials_transmission", "transmissionTexture", true);

	rd.getExtensionValue(params.u_ThicknessFactor, "KHR_materials_volume", "thicknessFactor");
	parseTextureInfo(MTL_TEX_THICKNESS_BINDING, gltfMtl.extensions, "KHR_materials_volume", "thicknessTexture", true);
	rd.getExtensionValue(params.u_AttenuationDistance, "KHR_materials_volume", "attenuationDistance");
	rd.getExtensionValue(params.u_AttenuationColor, "KHR_materials_volume", "attenuationColor");

	rd.getExtensionValue(params.u_IridescenceFactor, "KHR_materials_iridescence", "iridescenceFactor");
	parseTextureInfo(MTL_TEX_IRIDESCENE_BINDING, gltfMtl.extensions, "KHR_materials_iridescence", "iridescenceTexture", true);
	rd.getExtensionValue(params.u_IridescenceIor, "KHR_materials_iridescence", "iridescenceIor");
	rd.getExtensionValue(params.u_IridescenceThicknessMinimum, "KHR_materials_iridescence", "iridescenceThicknessMinimum");
	rd.getExtensionValue(params.u_IridescenceThicknessMaximum, "KHR_materials_iridescence", "iridescenceThicknessMaximum");
	parseTextureInfo(MTL_TEX_IRIDESCENE_THICKNESS_BINDING, gltfMtl.extensions, "KHR_materials_iridescence", "iridescenceThicknessTexture");
	
	rd.getExtensionValue(params.u_DiffuseTransmissionFactor, "KHR_materials_diffuse_transmission", "diffuseTransmissionFactor");
	parseTextureInfo(MTL_TEX_DIFFUSE_TRANSIMISSION_BINDING, gltfMtl.extensions, "KHR_materials_diffuse_transmission", "diffuseTransmissionTexture", true);
	rd.getExtensionValue(params.u_DiffuseTransmissionColorFactor, "KHR_materials_diffuse_transmission", "diffuseTransmissionColorFactor");
	parseTextureInfo(MTL_TEX_DIFFUSE_TRANSIMISSION_COLOR_BINDING, gltfMtl.extensions, "KHR_materials_diffuse_transmission", "diffuseTransmissionColorTexture");
	
	float factor = 0, rotation = 0;
	rd.getExtensionValue(factor, "KHR_materials_anisotropy", "anisotropyStrength");
	rd.getExtensionValue(rotation, "KHR_materials_anisotropy", "anisotropyRotation");
	params.u_Anisotropy = vec3(std::cos(rotation), std::sin(rotation), factor);
	parseTextureInfo(MTL_TEX_ANISOTROPY_BINDING, gltfMtl.extensions, "KHR_materials_anisotropy", "anisotropyTexture", true);

	rd.getExtensionValue(params.u_Dispersion, "KHR_materials_dispersion", "dispersion");
	rd.getExtensionValue(params.u_EmissiveStrength, "KHR_materials_emissive_strength", "emissiveStrength");

	if (gltfMtl.alphaMode == "OPAQUE") params.u_AlphaMode = ALPHAMODE_OPAQUE; 
	else if (gltfMtl.alphaMode == "MASK") params.u_AlphaMode = ALPHAMODE_MASK;
	else if (gltfMtl.alphaMode == "BLEND") params.u_AlphaMode = ALPHAMODE_BLEND;

	mtl_flags = 0;
	for (int i = 0; i < MATERIAL_TEXTURE_COUNT; ++i) {
		if (params.getSamplerUVSet(i) >= 0)
			mtl_flags |= (1 << i);
	}
	int shift = MATERIAL_TEXTURE_COUNT;
	auto func_materialMask = [&](int binding) {
		if (params.getSamplerUVSet(binding - MATERIAL_TEXTURE_FIRST_BINDING) >= -1)
			mtl_flags |= (1 << shift);
		shift++;
	};
	func_materialMask(MTL_TEX_SHEEN_COLOR_BINDING);
	func_materialMask(MTL_TEX_CLEARCOAT_BINDING);
	func_materialMask(MTL_TEX_TRANSMISSION_BINDING);
	func_materialMask(MTL_TEX_THICKNESS_BINDING);
	func_materialMask(MTL_TEX_IRIDESCENE_BINDING);
	func_materialMask(MTL_TEX_DIFFUSE_TRANSIMISSION_BINDING);
	func_materialMask(MTL_TEX_ANISOTROPY_BINDING);
	if (params.u_Dispersion != 0.0)			mtl_flags |= MATERIAL_DISPERSION_BIT;
	if (params.u_EmissiveStrength != 1.0)	mtl_flags |= MATERIAL_EMISSIVE_STRENGTH_BIT;
	if (params.u_Ior != 1.5)				mtl_flags |= MATERIAL_IOR_BIT;
}

void Material::uploadParams2Gpu()
{
	VK_CHECK_RESULT(uniformBuffer.map());
	memcpy(uniformBuffer.mapped, &params, sizeof(PbrMaterialUniforms));
	uniformBuffer.unmap();
}

void Material::uploadDescriptorSet2Gpu(const std::vector<vks::Texture2D>& imageByTexIdx, std::vector<VkWriteDescriptorSet>& writeDescriptorSet)
{
	writeDescriptorSet.push_back(vks::initializers::writeDescriptorSet(descriptorSet, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, MATERIAL_BINDING, &uniformBuffer.descriptor));

	for (int i = 0; i < textureIndexs.size(); ++i) {
		int textureIndex = textureIndexs[i];
		if (textureIndex >= imageByTexIdx.size()) continue;

		auto& image = imageByTexIdx[textureIndex];
		assert(image);
		const int binding = MATERIAL_TEXTURE_FIRST_BINDING + i;
		const bool hasSRGB = image.hasSRGBView() 
			&& (binding == MTL_TEX_BASE_COLOR_BINDING
			|| binding == MTL_TEX_SHEEN_COLOR_BINDING
			|| binding == MTL_TEX_CLEARCOAT_BINDING);

		VkDescriptorImageInfo* imageDescriptor = hasSRGB ? (VkDescriptorImageInfo*)&image.sRGBDescriptor : (VkDescriptorImageInfo*)&image.descriptor;
		writeDescriptorSet.push_back(vks::initializers::writeDescriptorSet(descriptorSet, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, binding, imageDescriptor));
	}
}

bool Material::hasTexture(int binding) const
{
	return params.getSamplerUVSet(binding - MATERIAL_TEXTURE_FIRST_BINDING) >= 0;
}

/**
 * MaterialFactory
 */
MaterialFactory::MaterialFactory(vks::VulkanDevice* vulkanDevice, VkDescriptorPool descriptorPool)
{
	vulkanDevice_ = vulkanDevice;
	device_ = vulkanDevice_->logicalDevice;
	descriptorPool_ = descriptorPool;

	{
		std::vector<VkDescriptorSetLayoutBinding> setLayoutBinding_Material(1, vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_FRAGMENT_BIT, MATERIAL_BINDING));
		for (int binding = MATERIAL_TEXTURE_FIRST_BINDING; binding <= MATERIAL_TEXTURE_LAST_BINDING; binding++) {
			setLayoutBinding_Material.push_back(vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, binding));
		}
		VkDescriptorSetLayoutCreateInfo descriptorSetLayoutCI_Matrices = vks::initializers::descriptorSetLayoutCreateInfo(setLayoutBinding_Material.data(), setLayoutBinding_Material.size());

		VkDescriptorSetLayoutBindingFlagsCreateInfo dsLayoutBindingFCInfo = {};
		std::vector<VkDescriptorBindingFlags> bindingFlags;
		{
			bindingFlags.push_back(0);
			for (int i = 1; i < setLayoutBinding_Material.size(); ++i) {
				bindingFlags.push_back(VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT);
			}
			dsLayoutBindingFCInfo.bindingCount = bindingFlags.size();
			dsLayoutBindingFCInfo.pBindingFlags = bindingFlags.data();
			dsLayoutBindingFCInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO;
			dsLayoutBindingFCInfo.pNext = nullptr;
			descriptorSetLayoutCI_Matrices.pNext = &dsLayoutBindingFCInfo;
		}

		VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device_, &descriptorSetLayoutCI_Matrices, nullptr, &descriptorSetLayout_));
	}
}
void MaterialFactory::destroy()
{
	vkSafeDestroyDescriptorSetLayout(device_, descriptorSetLayout_, nullptr);
}
Material MaterialFactory::createMaterial(tinygltf::Material& gltfMtl)
{
	Material mtl;
	mtl.load(gltfMtl);

	mtl.createHardware(vulkanDevice_, descriptorPool_, descriptorSetLayout_);
	mtl.uploadParams2Gpu();
	return mtl;
}
