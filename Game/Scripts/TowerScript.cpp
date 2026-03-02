#include "TowerScript.h"
#include "ScriptEngine.h"

namespace Game {

void TowerScript::Start(SceneObject& obj, GameScene* scene) {
	(void)scene;
	(void)obj;
}

void TowerScript::Update(SceneObject& obj, GameScene* scene, float dt) {
	(void)scene;
	(void)obj;
	// Y軸回転を増やし続ける
	obj.rotate.y += rotateSpeed_ * dt;
}

void TowerScript::OnDestroy(SceneObject& obj, GameScene* scene) {

(void)scene;
	(void)obj;
}

REGISTER_SCRIPT(TowerScript);

} // namespace Game