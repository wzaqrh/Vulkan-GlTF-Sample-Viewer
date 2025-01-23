#pragma once
#include "gltfShaderStruct.h"

struct ExtensionReader
{
	tinygltf::ExtensionMap& extensions_;
	ExtensionReader(tinygltf::ExtensionMap& extensions) :extensions_(extensions) {}

	template <typename RetValue>
	static RetValue parseRetValue(const tinygltf::Value& value) {
		if constexpr (std::is_same_v<RetValue, float>) {
			if (value.IsNumber()) return value.GetNumberAsDouble();
			else return RetValue();
		}
		else if constexpr (std::is_same_v<RetValue, int>) {
			if (value.IsInt()) return value.GetNumberAsInt();
			else return RetValue();
		}
		else if constexpr (std::is_same_v<RetValue, vec4>) {
			if (value.IsArray()) {
				const auto& arr = value.Get<tinygltf::Value::Array>();
				return vec4(arr[0].GetNumberAsDouble(), arr[1].GetNumberAsDouble(), arr[2].GetNumberAsDouble(), arr.size() > 3 ? arr[3].GetNumberAsDouble() : 0);
			}
			else return RetValue();
		}
		else if constexpr (std::is_same_v<RetValue, vec3>) {
			if (value.IsArray()) {
				const auto& arr = value.Get<tinygltf::Value::Array>();
				return vec3(arr[0].GetNumberAsDouble(), arr[1].GetNumberAsDouble(), arr[2].GetNumberAsDouble());
			}
			else return RetValue();
		}
		else if constexpr (std::is_same_v<RetValue, vec2>) {
			if (value.IsArray()) {
				const auto& arr = value.Get<tinygltf::Value::Array>();
				return vec2(arr[0].GetNumberAsDouble(), arr[1].GetNumberAsDouble());
			}
			else return RetValue();
		}
		else {
			static_assert(std::is_same_v<RetValue, void>, "Unsupported type for getExtensionValue.");
		}
	}

	template <typename RetValue, typename... Keys>
	static bool getValue(RetValue& result, const tinygltf::Value& table, const std::string& key, Keys... keys) {
		if (!table.Has(key)) return false;

		const tinygltf::Value& nextValue = table.Get(key);
		if constexpr (sizeof...(keys) == 0) {
			result = parseRetValue<RetValue>(nextValue);
			return true;
		}
		else return getValue(result, nextValue, keys...);
	}

	template<class RetValue, typename... Keys>
	bool getExtensionValue(RetValue& result, const std::string& extensionKey, Keys... keys) {
		auto extIt = extensions_.find(extensionKey);
		if (extIt != extensions_.end()) {
			const tinygltf::Value& extension = extIt->second;
			return getValue(result, extension, keys...);
		}
		else false;
	}

	bool hasExtension(const std::string& extensionKey) const {
		return extensions_.count(extensionKey);
	}
	void getExtensionEnabled(float& result, const std::string& extensionKey) const {
		if (hasExtension(extensionKey)) result = std::max(result, -1.0f);
	}
};

class OglToVulkan
{
public:
	static bool convertSamplerFilter(int glFilter, VkFilter& vulkanFilter, VkSamplerMipmapMode& mipmapMode) {
		switch (glFilter) {
		case TINYGLTF_TEXTURE_FILTER_NEAREST: // 对应 GL_NEAREST
			vulkanFilter = VK_FILTER_NEAREST;
			//mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
			return true;
		case TINYGLTF_TEXTURE_FILTER_LINEAR: // 对应 GL_LINEAR
			vulkanFilter = VK_FILTER_LINEAR;
			//mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
			return true;
		case TINYGLTF_TEXTURE_FILTER_NEAREST_MIPMAP_NEAREST: // 对应 GL_NEAREST_MIPMAP_NEAREST
			vulkanFilter = VK_FILTER_NEAREST;
			mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
			return true;
		case TINYGLTF_TEXTURE_FILTER_LINEAR_MIPMAP_NEAREST: // 对应 GL_LINEAR_MIPMAP_NEAREST
			vulkanFilter = VK_FILTER_LINEAR;
			mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
			return true;
		case TINYGLTF_TEXTURE_FILTER_NEAREST_MIPMAP_LINEAR: // 对应 GL_NEAREST_MIPMAP_LINEAR
			vulkanFilter = VK_FILTER_NEAREST;
			mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
			return true;
		case TINYGLTF_TEXTURE_FILTER_LINEAR_MIPMAP_LINEAR: // 对应 GL_LINEAR_MIPMAP_LINEAR
			vulkanFilter = VK_FILTER_LINEAR;
			mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
			return true;
		default:
			return false; // Unknown filter
		}
	}

	static bool convertSamplerWrap(int glWrap, VkSamplerAddressMode& vulkanWrap) {
		switch (glWrap) {
		case TINYGLTF_TEXTURE_WRAP_REPEAT: // 对应 GL_REPEAT
			vulkanWrap = VK_SAMPLER_ADDRESS_MODE_REPEAT;
			return true;
		case TINYGLTF_TEXTURE_WRAP_CLAMP_TO_EDGE: // 对应 GL_CLAMP_TO_EDGE
			vulkanWrap = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
			return true;
		case TINYGLTF_TEXTURE_WRAP_MIRRORED_REPEAT: // 对应 GL_MIRRORED_REPEAT
			vulkanWrap = VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
			return true;
		#define TINYGLTF_TEXTURE_WRAP_CLAMP_TO_BORDER (33069)
		case TINYGLTF_TEXTURE_WRAP_CLAMP_TO_BORDER: // 对应 GL_CLAMP_TO_BORDER
			vulkanWrap = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
			return true;
		default:
			return false; // Unknown wrap mode
		}
	}
};

static inline int getAccesorTypeChannels(int type) 
{
	switch (type)
	{
	case TINYGLTF_TYPE_VEC2:
		return 2;
	case TINYGLTF_TYPE_VEC3:
		return 3;
	case TINYGLTF_TYPE_VEC4:
		return 4;
	default:
		return 1;
		break;
	}
}

template<class T, int TComponentCount, class ComponentT>
static inline int readAccesor(const tinygltf::Model& input, const tinygltf::Accessor& accessor, std::vector<ComponentT>& values)
{
	const tinygltf::BufferView& bufferView = input.bufferViews[accessor.bufferView];
	const tinygltf::Buffer& buffer = input.buffers[bufferView.buffer];
	if (accessor.count == 0) return false;

	// TINYGLTF_TYPE_VEC2
	switch (accessor.componentType) {
	case TINYGLTF_PARAMETER_TYPE_INT:
	case TINYGLTF_PARAMETER_TYPE_UNSIGNED_INT: {
		if (!std::is_same<T, int32_t>::value && !std::is_same<T, uint32_t>::value)
			return false;
		if (getAccesorTypeChannels(accessor.type) != TComponentCount)
			return false;
		values.resize(accessor.count);
		memcpy(&values[0], &buffer.data[accessor.byteOffset + bufferView.byteOffset], accessor.count * sizeof(ComponentT));
	}break;
	case TINYGLTF_PARAMETER_TYPE_SHORT:
	case TINYGLTF_PARAMETER_TYPE_UNSIGNED_SHORT: {
		if (!std::is_same<T, int16_t>::value && !std::is_same<T, uint16_t>::value)
			return false;
		if (getAccesorTypeChannels(accessor.type) != TComponentCount)
			return false;
		values.resize(accessor.count);
		memcpy(&values[0], &buffer.data[accessor.byteOffset + bufferView.byteOffset], accessor.count * sizeof(ComponentT));
	}break;
	case TINYGLTF_PARAMETER_TYPE_BYTE:
	case TINYGLTF_PARAMETER_TYPE_UNSIGNED_BYTE: {
		if (!std::is_same<T, int8_t>::value && !std::is_same<T, uint8_t>::value)
			return false;
		if (getAccesorTypeChannels(accessor.type) != TComponentCount)
			return false;
		values.resize(accessor.count);
		memcpy(&values[0], &buffer.data[accessor.byteOffset + bufferView.byteOffset], accessor.count * sizeof(ComponentT));
	}break;
	case TINYGLTF_PARAMETER_TYPE_FLOAT: {
		if (!std::is_same<T, float>::value)
			return false;
		if (getAccesorTypeChannels(accessor.type) != TComponentCount)
			return false;
		values.resize(accessor.count);
		memcpy(&values[0], &buffer.data[accessor.byteOffset + bufferView.byteOffset], accessor.count * sizeof(ComponentT));
	}break;
	default:
		return false;
	}
	return accessor.count;
}

static inline vec4 toVec4(const std::vector<double>& value)
{
	return (value.size() >= 3) ? vec4(value[0], value[1], value[2], value.size() > 3 ? value[3] : 1) : vec4(1.0);
}
static inline vec3 toVec3(const std::vector<double>& value)
{
	return (value.size() >= 3) ? vec3(value[0], value[1], value[2]) : vec3(1.0);
}
static inline vec4 toVec4(glm::vec3 value)
{
	return vec4(value, 1);
}
static inline glm::quat toQuat(const glm::vec4& value)
{
	return glm::quat(value[3], value[0], value[1], value[2]);
}
static inline glm::quat toQuat(const std::vector<double>& value)
{
	return (value.size() >= 4) ? glm::quat(value[3], value[0], value[1], value[2]) : glm::quat();
}
static inline glm::mat4 toMat4(const std::vector<double>& v)
{
	return (v.size() >= 16) 
	? glm::mat4(
		v[0], v[1], v[2], v[3],
		v[4], v[5], v[6], v[7],
		v[8], v[9], v[10], v[11],
		v[12], v[13], v[14], v[15]
	)
	: glm::mat4(1.0);
}
static inline glm::mat3 toMat3(const std::vector<double>& v)
{
	return  (v.size() >= 9)
	? glm::mat3(
		v[0], v[1], v[2], 
		v[3], v[4], v[5], 
		v[6], v[7], v[8]
	)
	: glm::mat3(1.0);
}
