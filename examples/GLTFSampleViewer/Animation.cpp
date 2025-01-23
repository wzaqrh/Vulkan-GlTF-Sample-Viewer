#include "Animation.h"

void Animation::reset() 
{
	duration_ = 0;
	tracks_.clear();
	name_.clear();
}

bool Animation::load(tinygltf::Model& input, tinygltf::Animation& iAnimation)
{
	reset();

	name_ = iAnimation.name;
	
	for (auto& ch : iAnimation.channels)
	{
		if (ch.sampler >= iAnimation.samplers.size()) continue;
		const tinygltf::AnimationSampler& iSampler = iAnimation.samplers[ch.sampler];

		if (iSampler.input >= input.accessors.size()) continue;
		if (iSampler.output >= input.accessors.size()) continue;
		const tinygltf::Accessor& inputAccess = input.accessors[iSampler.input];
		const tinygltf::Accessor& outputAccess = input.accessors[iSampler.output];

		EAnimationTargetPath targetPath = parseAniTargetPathFromString(ch.target_path);
		if (targetPath == kATPath_Max) continue;

		AnimationTrack* oChannel = nullptr;
		auto iter = std::find_if(tracks_.begin(), tracks_.end(), [&](const AnimationTrack& track) {
			return track.nodeIndex == ch.target_node;
		});
		if (iter != tracks_.end()) oChannel = &*iter;
		else {
			tracks_.emplace_back();
			AnimationTrack& newTrack = tracks_.back();
			newTrack.nodeIndex = ch.target_node;
			oChannel = &newTrack;
		}

		AnimationSampler& oSampler = oChannel->samplers[targetPath];
		int readCount = 0;
		switch (targetPath)
		{
		case kATPath_Translation:
			readCount = readAccesor<float, 3>(input, outputAccess, oSampler.translation);
			break;
		case kATPath_Rotation:
			readCount = readAccesor<float, 4>(input, outputAccess, oSampler.rotation);
			break;
		case kATPath_Scale:
			readCount = readAccesor<float, 3>(input, outputAccess, oSampler.scale);
			break;
		case kATPath_Weight:
			readCount = readAccesor<float, 4>(input, outputAccess, oSampler.weights);
			break;
		case kATPath_Max:
		default:
			break;
		}
		if (!readCount) {
			oSampler.clear();
			continue;
		}

		int readCount1 = readAccesor<float, 1>(input, inputAccess, oSampler.times);
		if (!readCount1) oSampler.clear();
		duration_ = std::max(duration_, oSampler.times.back());

		if (readCount != readCount1) 
		{
			int minCount = std::min(readCount, readCount1);
			switch (targetPath)
			{
			case kATPath_Translation:
				oSampler.translation.resize(minCount);
				break;
			case kATPath_Rotation:
				oSampler.rotation.resize(minCount);
				break;
			case kATPath_Scale:
				oSampler.scale.resize(minCount);
				break;
			case kATPath_Weight:
				oSampler.weights.resize(minCount);
				break;
			case kATPath_Max:
			default:
				break;
			}
		}
	}
	return true;
}
