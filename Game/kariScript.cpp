#include "kariScript.h"
#include "ObjectTypes.h"
#include "Scenes/GameScene.h"
#include "ScriptEngine.h"

namespace Game {

void kariScript::Start(SceneObject& /*obj*/, GameScene* /*scene*/) {
	// ここに初期設定を記述
}

void kariScript::Update(SceneObject& obj, GameScene* scene, float dt) {
	// ここに毎フレームの挙動を記述
}

void kariScript::OnDestroy(SceneObject& /*obj*/, GameScene* /*scene*/) {
	// 終了時のクリーンアップなどを記述
}

// ★ スクリプト自動登録
REGISTER_SCRIPT(kariScript);

} // namespace Game
