#include "InstallationButton.h"
#include "ObjectTypes.h"
#include "Scenes/GameScene.h"
#include "ScriptEngine.h"
#include <cmath>
#include <iostream>

#include "../../Engine/Input.h"
#include "PhaseSystemScript.h"
#if defined(USE_IMGUI) && !defined(NDEBUG)
#include <imgui.h>
#endif
#include "../../Engine/ThirdParty/nlohmann/json.hpp"

using json = nlohmann::json;



namespace Game {

namespace {
bool isButtonPressed[InstallationButton::FacilityTypes::FacilityTypesNum];
}

bool InstallationButton::isButtonPressed_[FacilityTypesNum] = {};

void InstallationButton::Start(entt::entity /*entity*/, GameScene* /*scene*/) {}

void InstallationButton::Update(entt::entity entity, GameScene* scene, float dt) {
	(void)dt;

	if (!scene || !scene->GetRegistry().valid(entity))
		return;
	auto& registry = scene->GetRegistry();
	if (!registry.all_of<TransformComponent>(entity))
		return;

	isButtonPressed_[FacilityTypes_] = scene->GetRegistry().all_of<UIButtonComponent>(entity) && scene->GetRegistry().get<UIButtonComponent>(entity).isPressed;
	isButtonPressed[FacilityTypes_] = isButtonPressed_[FacilityTypes_];

	if (PhaseSystemScript::IsPhase() == PhaseSystemScript::PreparationPhase) {
		if (scene->GetRegistry().all_of<UIImageComponent>(entity))
			scene->GetRegistry().get<UIImageComponent>(entity).enabled = true;
		if (scene->GetRegistry().all_of<UIButtonComponent>(entity))
			scene->GetRegistry().get<UIButtonComponent>(entity).enabled = true;
		// cameraTargets はECS化が必要だが、一旦省略
	} else {
		if (scene->GetRegistry().all_of<UIImageComponent>(entity))
			scene->GetRegistry().get<UIImageComponent>(entity).enabled = false;
		if (scene->GetRegistry().all_of<UIButtonComponent>(entity))
			scene->GetRegistry().get<UIButtonComponent>(entity).enabled = false;
	}
}

void InstallationButton::OnDestroy(entt::entity /*entity*/, GameScene* /*scene*/) {}

void InstallationButton::OnEditorUI() {
#if defined(USE_IMGUI) && !defined(NDEBUG)
	ImGui::SeparatorText("Installation Button");

	const char* buttonTypeNames[] = {"Cannon", "Pipe", "Tank"};
	int currentType = static_cast<int>(FacilityTypes_);
	if (ImGui::Combo("Facility Type", &currentType, buttonTypeNames, IM_ARRAYSIZE(buttonTypeNames))) {
		FacilityTypes_ = static_cast<FacilityTypes>(currentType);
	}

	ImGui::Text("isButtonPressed_: %s", isButtonPressed_[FacilityTypes_] ? "true" : "false");
#endif
}

std::string InstallationButton::SerializeParameters() {
	json j;
	j["FacilityTypes"] = static_cast<int>(FacilityTypes_);
	return j.dump();
}

void InstallationButton::DeserializeParameters(const std::string& data) {
	if (data.empty()) return;
	try {
		json j = json::parse(data);
		if (j.contains("FacilityTypes")) {
			FacilityTypes_ = static_cast<FacilityTypes>(j["FacilityTypes"].get<int>());
		}
	} catch (...) {
		// Log or suppress parse errors
	}
}

bool InstallationButton::IsButtonPressed(FacilityTypes type) { return isButtonPressed[type]; }

REGISTER_SCRIPT(InstallationButton);

} // namespace Game