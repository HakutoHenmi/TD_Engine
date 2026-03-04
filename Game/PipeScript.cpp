#include "PipeScript.h"
#include "ObjectTypes.h"
#include "Scenes/GameScene.h"
#include "ScriptEngine.h"
#include <cmath>
namespace Game {
void PipeScript::Start(SceneObject& obj, GameScene* /*scene*/) {
	(void)obj;
	
}

void PipeScript::Update(SceneObject& obj, GameScene* scene, float dt) {
	(void)scene;

	obj.rotate.z += rotationSpeed_ * dt;
}

void PipeScript::OnDestroy(SceneObject& /*obj*/, GameScene* /*scene*/ ) {

	
}
REGISTER_SCRIPT(PipeScript);
} // namespace Game