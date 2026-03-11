#include "BulletScript.h"
#include "ObjectTypes.h"
#include "Scenes/GameScene.h"
#include "ScriptEngine.h"
#include <cmath>

namespace Game {

void BulletScript::Start(SceneObject& /*obj*/, GameScene* /*scene*/) {}

void BulletScript::Update(SceneObject& obj, GameScene* /*scene*/, float dt) {
	float moveX = std::sin(obj.rotate.y) * std::cos(obj.rotate.x) * speed_ * dt;
	float moveY = -std::sin(obj.rotate.x) * speed_ * dt;
	float moveZ = std::cos(obj.rotate.y) * std::cos(obj.rotate.x) * speed_ * dt;

	obj.translate.x += moveX;
	obj.translate.y += moveY;
	obj.translate.z += moveZ;
}

void BulletScript::OnDestroy(SceneObject& /*obj*/, GameScene* /*scene*/) {}

REGISTER_SCRIPT(BulletScript);

} // namespace Game