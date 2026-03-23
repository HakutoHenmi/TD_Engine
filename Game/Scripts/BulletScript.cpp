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

	// 前方に進む処理
	float moveX = std::sin(tc.rotate.y) * speed_ * dt;
	float moveZ = std::cos(tc.rotate.y) * speed_ * dt;

	tc.translate.x += moveX;
	tc.translate.z += moveZ;

	// 衝突時の死亡フラグは Hitbox と Hurtbox の処理で GameScene.cpp 側がやっているため、
	// ここは移動のみ。寿命による消滅も GameScene.cpp が担っている。
}

void BulletScript::OnDestroy(entt::entity /*entity*/, GameScene* /*scene*/) {
}

// ★ スクリプト自動登録
REGISTER_SCRIPT(BulletScript);

} // namespace Game