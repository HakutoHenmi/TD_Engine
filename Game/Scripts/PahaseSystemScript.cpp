#include "PahaseSystemScript.h"
#include "ObjectTypes.h"
#include "Scenes/GameScene.h"
#include "ScriptEngine.h"
#include <cmath>
#include <iostream>

namespace Game {

	bool PahaseSystemScript::isPreparation_ = false;

void PahaseSystemScript::Start(SceneObject& obj, GameScene* scene) {
	(obj);
	(scene);
}

void PahaseSystemScript::Update(SceneObject& obj, GameScene* scene, float dt) {
	(obj);
	(scene);
	(dt);
	bool keyP = (GetAsyncKeyState('P') & 0x8000) != 0;
	if (isPreparation_) {
		Installation();
		
		
		if (keyP && !preKeyP_) {
			isPreparation_ = false;
		}

	} else {
		if (keyP && !preKeyP_) {
			isPreparation_ = true;
		}
	}
	preKeyP_ = keyP;
}

void PahaseSystemScript::Installation() {}

void PahaseSystemScript::OnDestroy(SceneObject& /*obj*/, GameScene* /*scene*/) {}

REGISTER_SCRIPT(PahaseSystemScript);

} // namespace Game