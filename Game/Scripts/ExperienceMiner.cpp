#include "ExperienceMiner.h"
#include "ScriptEngine.h"

namespace Game {

void Game::ExperienceMiner::Start(SceneObject& obj, GameScene* scene) {
	(void)obj;
	(void)scene;
}

void Game::ExperienceMiner::Update(SceneObject& obj, GameScene* scene, float dt) {

	(void)obj;
	(void)scene;
	obj.rotate.y += 0.5f * dt;

	
}

void Game::ExperienceMiner::OnDestroy(SceneObject& obj, GameScene* scene) {
	(void)obj;
	(void)scene;
}

REGISTER_SCRIPT(ExperienceMiner);
} // namespace Game