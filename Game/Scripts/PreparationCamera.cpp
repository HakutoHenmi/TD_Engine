#include "PreparationCamera.h"
#include "ObjectTypes.h"
#include "Scenes/GameScene.h"
#include "ScriptEngine.h"
#include <cmath>
#include <iostream>

#include "PhaseSystemScript.h"

namespace Game {

void PreparationCamera::Start(entt::entity /*entity*/, GameScene* /*scene*/) {}

void PreparationCamera::Update(entt::entity entity, GameScene* scene, float dt) {
	if (!scene || !scene->GetRegistry().valid(entity))
		return;
	auto& registry = scene->GetRegistry();
	if (!registry.all_of<TransformComponent>(entity))
		return;

	if (PhaseSystemScript::IsPreparation()) {
		UpdateMovement(entity, scene, dt);
		if (scene->GetRegistry().all_of<CameraTargetComponent>(entity))scene->GetRegistry().get<CameraTargetComponent>(entity).enabled = true;
		if (scene->GetRegistry().all_of<PlayerInputComponent>(entity))scene->GetRegistry().get<PlayerInputComponent>(entity).enabled = true;
		// cameraTargets はECS化が必要だが、一旦省略
	} else {
		if (scene->GetRegistry().all_of<CameraTargetComponent>(entity))scene->GetRegistry().get<CameraTargetComponent>(entity).enabled = false;
		if (scene->GetRegistry().all_of<PlayerInputComponent>(entity))scene->GetRegistry().get<PlayerInputComponent>(entity).enabled = false;
	}
}

void PreparationCamera::UpdateMovement(entt::entity entity, GameScene* scene, float /*dt*/) {
	if (!scene || !scene->GetRegistry().valid(entity))
		return;

	auto& registry = scene->GetRegistry();
	if (!registry.all_of<PlayerInputComponent>(entity))
		return;

	auto& input = registry.get<PlayerInputComponent>(entity);
	float kPreparationCameraSpeedMul = 2.0f;
	input.moveDir.x *= kPreparationCameraSpeedMul;
	input.moveDir.y *= kPreparationCameraSpeedMul;
}

void PreparationCamera::OnDestroy(entt::entity /*entity*/, GameScene* /*scene*/) {}

REGISTER_SCRIPT(PreparationCamera);

} // namespace Game