#include "CannonButton.h"
#include "ObjectTypes.h"
#include "Scenes/GameScene.h"
#include "ScriptEngine.h"
#include <cmath>
#include <iostream>

#include "PhaseSystemScript.h"
#include "../../Engine/Input.h"
#if defined(USE_IMGUI) && !defined(NDEBUG)
#include <imgui.h>
#endif

namespace Game {

bool CannonButton::isButtonPressed_ = false;

void CannonButton::Start(entt::entity /*entity*/, GameScene* /*scene*/) { isButtonPressed_ = false; }

void CannonButton::Update(entt::entity entity, GameScene* scene, float dt) {
	(void)dt;


	if (!scene || !scene->GetRegistry().valid(entity))
		return;
	auto& registry = scene->GetRegistry();
	if (!registry.all_of<TransformComponent>(entity))
		return;

   isButtonPressed_ = scene->GetRegistry().all_of<UIButtonComponent>(entity) && scene->GetRegistry().get<UIButtonComponent>(entity).isPressed;

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

void CannonButton::OnDestroy(entt::entity /*entity*/, GameScene* /*scene*/) {}

void CannonButton::OnEditorUI() {

	ImGui::Begin("CannonButton Script");
	ImGui::Text("isButtonPressed_: %s", isButtonPressed_ ? "true" : "false");
	ImGui::End();
}

REGISTER_SCRIPT(CannonButton);

} // namespace Game