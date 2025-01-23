#pragma once
#include "gltfShaderStruct.h"
#include "GltfReadUtils.h"

enum EAnimationTargetPath {
	kATPath_Translation,
	kATPath_Rotation,
	kATPath_Scale,
	kATPath_Weight,
	kATPath_Max
};
static inline EAnimationTargetPath parseAniTargetPathFromString(const std::string& str) {
	if (str == "translation") return kATPath_Translation;
	else if (str == "rotation") return kATPath_Rotation;
	else if (str == "scale") return kATPath_Scale;
	else if (str == "weights") return kATPath_Weight;
	return kATPath_Max;
}
struct AnimationSampler
{
	void clear() 
	{
		times.clear();
		translation.clear();
		scale.clear();
		rotation.clear();
		weights.clear();
	}
	bool isValid() const { return times.size() >= 2; }
	operator bool() const { return isValid(); }

	std::vector<float> times;
	std::vector<glm::vec3> translation;
	std::vector<glm::vec3> scale;
	std::vector<glm::quat> rotation;
	std::vector<glm::vec4> weights;
};
struct AnimationTrack
{
	bool isValid() const { return samplers.size() > 0; }
	operator bool() const { return isValid(); }

	int nodeIndex = -1;
	std::array<AnimationSampler, kATPath_Max> samplers;
};
class Animation 
{
public:
	using const_iterator = std::vector<AnimationTrack>::const_iterator;
	using iterator = std::vector<AnimationTrack>::iterator;

	void reset();
	bool load(tinygltf::Model& gltfMdl, tinygltf::Animation& gltfAni);
	
	float getDuration() const { return duration_; }
	const std::string& getName() const { return name_; }

	const_iterator begin() const { return tracks_.begin(); }
	const_iterator end() const { return tracks_.end(); }
	size_t getTrackCount() const { return tracks_.size(); }
	AnimationTrack& getTrackByIndex(size_t index) { return tracks_[index]; }
	const AnimationTrack& getTrackByIndex(size_t index) const { return tracks_[index]; }
private:
	std::vector<AnimationTrack> tracks_;
	float duration_ = 0.0f;
	std::string name_;
};

