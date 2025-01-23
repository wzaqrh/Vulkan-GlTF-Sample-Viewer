#include "VulkanGLTFSampleViewer.h"

static void ImGui_Combo(std::string uiName, std::map<int, std::string> channelMap, int& channelIndex)
{
	if (ImGui::BeginCombo(uiName.c_str(), channelMap[channelIndex].c_str()))
	{
		for (const auto& pair : channelMap) {
			int id = pair.first;
			const char* label = pair.second.c_str();
			bool isSelected = (channelIndex == id);
			if (ImGui::Selectable(label, isSelected)) {
				channelIndex = id;
			}
			if (isSelected) {
				ImGui::SetItemDefaultFocus();
			}
		}
		ImGui::EndCombo();
	}
}
static void ImGui_ScrollView(std::string uiName, ImVec2 scrollViewSize, std::map<int, std::string> channelMap, int& channelIndex)
{
	ImGui::BeginChild(uiName.c_str(), scrollViewSize, true, ImGuiWindowFlags_None);
	for (const auto& pair : channelMap) {
		int id = pair.first;
		const char* label = pair.second.c_str();
		bool isSelected = (channelIndex == id);
		if (ImGui::Selectable(label, isSelected)) {
			channelIndex = id;
		}
	}
	ImGui::EndChild();
}

static int getIndex(std::vector<std::string> strlist, std::string str)
{
	for (int i = 0; i < strlist.size(); ++i) {
		if (strlist[i] == str)
			return i;
	}
	return 0;
}
static std::map<int, std::string> vector2map(std::vector<std::string> strlist) {
	std::map<int, std::string> mapping;
	int index = 0;
	for (std::string name : strlist) {
		mapping[index++] = name;
	}
	return mapping;
}
static std::map<int, std::string> sDebugChannels =
{
	{DEBUG_NONE, "None"},
	{DEBUG_DIFFUSE, "Diffuse"},
	{DEBUG_UV_0, "UV 0"},
	{DEBUG_UV_1, "UV 1"},

	{DEBUG_NORMAL_TEXTURE, "Normal Texture"},
	{DEBUG_GEOMETRY_NORMAL, "Geometry Normal"},
	{DEBUG_GEOMETRY_TANGENT, "Geometry Tangent"},
	{DEBUG_GEOMETRY_BITANGENT, "Geometry Bitangent"},
	{DEBUG_SHADING_NORMAL, "Shading Normal"},

	{DEBUG_ALPHA, "Alpha"},
	{DEBUG_OCCLUSION, "Occlusion"},
	{DEBUG_EMISSIVE, "Emissive"},

	{DEBUG_METALLIC, "Metallic"},
	{DEBUG_ROUGHNESS, "Roughness"},
	{DEBUG_BASE_COLOR, "Base Color"},

	{DEBUG_CLEARCOAT_FACTOR, "Clearcoat Factor"},
	{DEBUG_CLEARCOAT_ROUGHNESS, "Clearcoat Roughness"},
	{DEBUG_CLEARCOAT_NORMAL, "Clearcoat Normal"},

	{DEBUG_SHEEN_COLOR, "Sheen Color"},
	{DEBUG_SHEEN_ROUGHNESS, "Sheen Roughness"},

	{DEBUG_TRANSMISSION_FACTOR, "Transmission Factor"},
	{DEBUG_VOLUME_THICKNESS, "Volume Thickness"},

	{DEBUG_IRIDESCENCE_FACTOR, "Iridescence Factor"},
	{DEBUG_IRIDESCENCE_THICKNESS, "Iridescence Thickness"},

	{DEBUG_ANISOTROPIC_STRENGTH, "Anisotropic Strength"},
	{DEBUG_ANISOTROPIC_DIRECTION, "Anisotropic Direction"},

	{DEBUG_DIFFUSE_TRANSMISSION_FACTOR, "Diffuse Transmission Factor"},
	{DEBUG_DIFFUSE_TRANSMISSION_COLOR_FACTOR, "Diffuse Transmission Color Factor"},

	{DEBUG_IBL_DIFFUSE, "IBL Diffuse"},
	{DEBUG_IBL_SPECULAR_TRANSMISSION, "IBL Specular Transmission"},
	{DEBUG_IBL_SPECULAR_METAL, "IBL Specular Metal"},
	{DEBUG_IBL_SPECULAR_DIELECTRIC, "IBL Specular Dielectric"},
	{DEBUG_IBL_BRDF_METAL, "IBL BRDF Metal"},
	{DEBUG_IBL_BRDF_DIELECTRIC, "IBL BRDF Dielectric"},
	{DEBUG_IBL_BRDF_CLEARCOAT, "IBL BRDF ClearCoat"},
	{DEBUG_IBL_SHEEN, "IBL Sheen"},
	{DEBUG_IBL_SHEEN_LIGHT, "IBL Sheen Light"},
	{DEBUG_IBL_SHEEN_BRDF_POINT, "IBL Sheen BRDF Point"},
	{DEBUG_IBL_SHEEN_BRDF, "IBL Sheen BRDF"},

	{DEBUG_VECTOR_V, "Vector V"},
	{DEBUG_VECTOR_L, "Vector L"},
};
static std::map<int, std::string> sToneMappings =
{
	{TONEMAP_KHR_PBR_NEUTRAL, "Neutral"},
	{TONEMAP_ACES_HILL_EXPOSURE_BOOST, "ACES Filmic(Hill Exposure Boost)"},
	{TONEMAP_ACES_NARKOWICZ, "ACES Filmic(Narkowicz)"},
	{TONEMAP_ACES_HILL, "ACES Filmic(Hill)"},
	{TONEMAP_LINEAR, "None(Linear)"}
};
static std::map<int, std::string> sDegreeMappings =
{
	{90,  "+Z"},
	{180, "-X"},
	{270, "-Z"},
	{0,   "+X"}
};
static std::vector<std::string> sEnviromentAssets = 
{
	"neutral", 
	"pisa", 
	"footprint_court", 
	"doge2"
};
static std::vector<std::string> sModelAssets = 
{
	"ToyCar",
	"BoxAnimated",
	"BrainStem",
	"BusterDrone",
	"CesiumMan",
	"CesiumMilkTruck",
	"FlightHelmet",
	"Suzanne",
	"Sponza",
	"MetalRoughSpheres",
	"MetalRoughSpheresNoTextures",
	"DragonAttenuation",
	"GlamVelvetSofa",
	"IridescenceLamp",
	"IridescentDishWithOlives",
	"LightsPunctualLamp",
	"MaterialsVariantsShoe",
	"MosquitoInAmber",
	"SheenChair",
	"SheenCloth",
	"AttenuationTest",
	"EnvironmentTest"
};
void VulkanGLTFSampleViewer::OnUpdateUIOverlay(vks::UIOverlay* overlay)
{
	cameraFixed_ = ImGui::IsAnyItemHovered();
	if (overlay->header("Settings")) 
	{
		overlay->checkBox("Wireframe", &wireframe_);
		
		int enviromentIndex, enviromentIndexOld;
		enviromentIndex = enviromentIndexOld = getIndex(sEnviromentAssets, enviromentName_);
		ImGui_Combo("Environment", vector2map(sEnviromentAssets), enviromentIndex);

		bool isGlb = modelGlb_;
		int modelIndex, modelIndexOld;
		modelIndex = modelIndexOld = getIndex(sModelAssets, modelName_);
		ImGui_Combo("Model", vector2map(sModelAssets), modelIndex);
		overlay->checkBox("glTF-Binary", &isGlb);

		overlay->checkBox("Show EnviromentMap", &showEnviromentMap_);
		overlay->checkBox("Blur Enviroment", &enviroment_->environmentBlur);
		ImGui_Combo("EnviromentMap Rotation", sDegreeMappings, enviroment_->environmentRotation);

		int animationIndex = model_->getAnimationIndex();
		int cameraIndex = userCamera_->getCurrentIndex();
		ConstantValue oldConstantValue = constantValue_;
		{
			overlay->checkBox("Enable IBL", &constantValue_.USE_IBL);
			ImGui::SliderFloat("IBL Intensity", &enviroment_->envIntensity, 0.01f, 100.0f, "%.2f", 10);
			overlay->checkBox("Enable Punctual", &constantValue_.USE_PUNCTUAL);

			ImGui_Combo("Cameras", vector2map(userCamera_->getCameraNames()), cameraIndex);
			ImGui::SliderFloat("Exposure", &userCamera_->Exposure, 0.001f, 64.0f, "%.2f", 2);
			ImGui_Combo("Tone Map", sToneMappings, constantValue_.TONEMAP);

			ImGui_Combo("Animations", vector2map(model_->getAnimationNames()), cameraIndex);
			ImGui_ScrollView("Debug Channel", ImVec2(200, 200), sDebugChannels, constantValue_.DEBUG1);
		}
		if (memcmp(&oldConstantValue, &constantValue_, sizeof(constantValue_))) 
		{
			preparePipelines();
		}
		if (userCamera_->getCurrentIndex() != cameraIndex)
		{
			userCamera_->setCurrentIndex(cameraIndex);
		}
		if (model_->getAnimationIndex() != animationIndex)
		{
			model_->setAnimationIndex(animationIndex);
		}
		if (enviromentIndex != enviromentIndexOld)
		{
			enviromentName_ = sEnviromentAssets[enviromentIndex];
			reloadEnviroment();
		}
		if (modelIndex != modelIndexOld || isGlb != modelGlb_)
		{
			modelName_ = sModelAssets[modelIndex];
			modelGlb_ = isGlb;
			reloadModel();
		}
	}
}