#include "BulletScript.h"
#include "ObjectTypes.h"
#include "Scenes/GameScene.h"
#include "ScriptEngine.h"
#include <cmath>

namespace Game {

void BulletScript::Start(entt::entity /*entity*/, GameScene* /*scene*/) {
}

void BulletScript::Update(entt::entity entity, GameScene* scene, float dt) {
	if (!scene || !scene->GetRegistry().valid(entity)) return;
	auto& registry = scene->GetRegistry();
	if (!registry.all_of<TransformComponent>(entity)) return;
	auto& tc = registry.get<TransformComponent>(entity);

	// 3次元的な前進処理 (回転 y:自動, x:仰角)
	float cosX = std::cos(tc.rotate.x);
	float moveX = std::sin(tc.rotate.y) * cosX * speed_ * dt;
	float moveY = -std::sin(tc.rotate.x) * speed_ * dt; // x回転が正=下向き、負=上向き
	float moveZ = std::cos(tc.rotate.y) * cosX * speed_ * dt;

	tc.translate.x += moveX;
	tc.translate.y += moveY;
	tc.translate.z += moveZ;
}

void BulletScript::OnDestroy(entt::entity /*entity*/, GameScene* /*scene*/) {
}

// ★ スクリプト自動登録
REGISTER_SCRIPT(BulletScript);

} // namespace Game