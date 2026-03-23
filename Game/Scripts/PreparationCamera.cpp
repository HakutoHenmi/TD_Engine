#include "PreparationCamera.h"
#include "ObjectTypes.h"
#include "Scenes/GameScene.h"
#include "ScriptEngine.h"
#include <cmath>
#include <iostream>

#include "PhaseSystemScript.h"

namespace Game {

void PreparationCamera::Start(entt::entity /*entity*/, GameScene* /*scene*/) {
}

void PreparationCamera::Update(entt::entity entity, GameScene* scene, float dt) {
	if (!scene || !scene->GetRegistry().valid(entity)) return;
	auto& registry = scene->GetRegistry();
	if (!registry.all_of<TransformComponent>(entity)) return;

	if (PhaseSystemScript::IsPreparation()) {
		UpdateMovement(entity, scene, dt);
		// cameraTargets はECS化が必要だが、一旦省略
	}
}

void PreparationCamera::UpdateMovement(entt::entity entity, GameScene* /*scene*/, float dt) {
	// ※ このメソッドはGameSceneのregistryを使ってentityを操作
	// scene経由でregistryを取得するが、引数でsceneがunusedの場合は直接使えない
	// 一旦簡易実装
	(void)entity;
	(void)dt;
}

void PreparationCamera::OnDestroy(entt::entity /*entity*/, GameScene* /*scene*/) {}

REGISTER_SCRIPT(PreparationCamera);

} // namespace Game