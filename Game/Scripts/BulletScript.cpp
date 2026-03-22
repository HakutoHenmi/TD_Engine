#include "BulletScript.h"
#include "ObjectTypes.h"
#include "Scenes/GameScene.h"
#include "ScriptEngine.h"
#include <cmath>

namespace Game {

void BulletScript::Start(SceneObject& /*obj*/, GameScene* /*scene*/) {
}

void BulletScript::Update(SceneObject& obj, GameScene* /*scene*/, float dt) {
	float yaw = obj.rotate.y;
	float pitch = -obj.rotate.x;

	float moveX = std::sin(yaw) * std::cos(pitch) * speed_ * dt;
	float moveY = std::sin(pitch) * speed_ * dt;
	float moveZ = std::cos(yaw) * std::cos(pitch) * speed_ * dt;

	obj.translate.x += moveX;
	obj.translate.y += moveY;
	obj.translate.z += moveZ;
}

void BulletScript::OnDestroy(SceneObject& /*obj*/, GameScene* /*scene*/) {
}

// ★ スクリプト自動登録
REGISTER_SCRIPT(BulletScript);

} // namespace Game