#include "BulletScript.h"
#include "ObjectTypes.h"
#include "Scenes/GameScene.h"
#include "ScriptEngine.h"
#include <cmath>

namespace Game {

void BulletScript::Start(SceneObject& /*obj*/, GameScene* /*scene*/) {
}

void BulletScript::Update(SceneObject& obj, GameScene* /*scene*/, float dt) {
	// 前方に進む処理
	float moveX = std::sin(obj.rotate.y) * speed_ * dt;
	float moveZ = std::cos(obj.rotate.y) * speed_ * dt;
	
	obj.translate.x += moveX;
	obj.translate.z += moveZ;

	// 衝突時の死亡フラグは Hitbox と Hurtbox の処理で GameScene.cpp 側がやっているため、
	// ここは移動のみ。寿命による消滅も GameScene.cpp が担っている。
}

void BulletScript::OnDestroy(SceneObject& /*obj*/, GameScene* /*scene*/) {
}

// ★ スクリプト自動登録
REGISTER_SCRIPT(BulletScript);

} // namespace Game