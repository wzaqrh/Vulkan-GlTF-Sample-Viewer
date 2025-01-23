/*
* Vulkan texture loader
*
* Copyright(C) by Sascha Willems - www.saschawillems.de
*
* This code is licensed under the MIT license(MIT) (http://opensource.org/licenses/MIT)
*/

#pragma once

#include <fstream>
#include <stdlib.h>
#include <string>
#include <vector>

#include "vulkan/vulkan.h"

#include <ktx.h>
#include <ktxvulkan.h>

#include "VulkanBuffer.h"
#include "VulkanDevice.h"
#include "VulkanTools.h"

#if defined(__ANDROID__)
#	include <android/asset_manager.h>
#endif

namespace vks
{
class Texture
{
  public:
	vks::VulkanDevice*    device = nullptr;
	VkImage               image = VK_NULL_HANDLE;
	VkImageLayout         imageLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	VkDeviceMemory        deviceMemory = VK_NULL_HANDLE;
	VkFormat			  format = VK_FORMAT_UNDEFINED, sRGBFormat = VK_FORMAT_UNDEFINED;
	VkImageView           view = VK_NULL_HANDLE, sRGBView = VK_NULL_HANDLE;
	uint32_t              width = 0, height = 0;
	uint32_t              mipLevels = 0;
	uint32_t              layerCount = 0;
	VkDescriptorImageInfo descriptor, sRGBDescriptor;
	VkSampler             sampler = VK_NULL_HANDLE;

	bool	  hasSRGBView() const { return sRGBView != VK_NULL_HANDLE; }
	bool	  isValid() const { return device != nullptr; }
	operator  bool() const { return isValid(); }

	void      updateDescriptor();
	void      destroy();
	static ktxResult loadKTXFile(std::string filename, ktxTexture **target);
};

class Texture2D : public Texture
{
public:
	struct SamplerOption {
		VkFilter                magFilter = VK_FILTER_LINEAR;
		VkFilter                minFilter = VK_FILTER_LINEAR;
		VkSamplerMipmapMode     mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;

		VkSamplerAddressMode    addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
		VkSamplerAddressMode    addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
		VkSamplerAddressMode    addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
		
		VkBool32                compareEnable = FALSE;
		VkCompareOp             compareOp = VK_COMPARE_OP_NEVER;
		VkBool32                anisotropyEnable = FALSE;
	};
	bool loadFromFile(
		std::string        filename,
		VkFormat           format,
		vks::VulkanDevice* device,
		VkQueue            copyQueue,
		VkImageUsageFlags  imageUsageFlags = VK_IMAGE_USAGE_SAMPLED_BIT,
		VkImageLayout      imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		bool               forceLinear = false,
		SamplerOption	   samplerOpt = SamplerOption());
	void fromBuffer(
		void* buffer,
		VkDeviceSize       bufferSize,
		VkFormat           format,
		uint32_t           texWidth,
		uint32_t           texHeight,
		vks::VulkanDevice* device,
		VkQueue            copyQueue,
		VkImageUsageFlags  imageUsageFlags = VK_IMAGE_USAGE_SAMPLED_BIT,
		VkImageLayout      imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		bool			   mutableFormat = false,
		SamplerOption	   samplerOpt = SamplerOption());
private:
	bool loadFromKtxFile(
		std::string        filename,
		VkFormat           format,
		vks::VulkanDevice* device,
		VkQueue            copyQueue,
		VkImageUsageFlags  imageUsageFlags = VK_IMAGE_USAGE_SAMPLED_BIT,
		VkImageLayout      imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		bool               forceLinear = false,
		SamplerOption	   samplerOpt = SamplerOption());
	bool loadFromPngFile(
		std::string        filename,
		VkFormat           format,
		vks::VulkanDevice* device,
		VkQueue            copyQueue,
		VkImageUsageFlags  imageUsageFlags = VK_IMAGE_USAGE_SAMPLED_BIT,
		VkImageLayout      imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		bool               forceLinear = false,
		SamplerOption	   samplerOpt = SamplerOption());
};

class Texture2DArray : public Texture
{
  public:
	void loadFromFile(
	    std::string        filename,
	    VkFormat           format,
	    vks::VulkanDevice *device,
	    VkQueue            copyQueue,
	    VkImageUsageFlags  imageUsageFlags = VK_IMAGE_USAGE_SAMPLED_BIT,
	    VkImageLayout      imageLayout     = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
};

class TextureCubeMap : public Texture
{
  public:
	bool loadFromFile(
	    std::string        filename,
	    VkFormat           format,
	    vks::VulkanDevice *device,
	    VkQueue            copyQueue,
	    VkImageUsageFlags  imageUsageFlags = VK_IMAGE_USAGE_SAMPLED_BIT,
	    VkImageLayout      imageLayout     = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
};
}        // namespace vks
