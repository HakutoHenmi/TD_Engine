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

namespace {
bool isButtonPressed;
}

namespace Game {

bool InstallationButton::isButtonPressed_[ButtonTypesNum] = {};

void InstallationButton::Start(entt::entity /*entity*/, GameScene* /*scene*/) {}

void InstallationButton::Update(entt::entity entity, GameScene* scene, float dt) {
	(void)dt;

	if (!scene || !scene->GetRegistry().valid(entity))
		return;
	auto& registry = scene->GetRegistry();
	if (!registry.all_of<TransformComponent>(entity))
		return;

	isButtonPressed_[buttonTypes_] = scene->GetRegistry().all_of<UIButtonComponent>(entity) && scene->GetRegistry().get<UIButtonComponent>(entity).isPressed;
	isButtonPressed = isButtonPressed_[buttonTypes_];

	if (PhaseSystemScript::IsPreparation()) {
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

	ImGui::Begin("InstallationButton Script");
	ImGui::Text("isButtonPressed_: %s", isButtonPressed_ ? "true" : "false");
	ImGui::End();
}

bool InstallationButton::IsButtonPressed() { return isButtonPressed; }

REGISTER_SCRIPT(InstallationButton);

} // namespace Game