#pragma once
#include "IScript.h"
#include "Scenes/GameScene.h"
namespace Game {

class ExperienceHopper : public IScript {

	void Start(SceneObject& obj, GameScene* scene) override;
	void Update(SceneObject& obj, GameScene* scene, float dt) override;
	void OnDestroy(SceneObject& obj, GameScene* scene) override;

private:
	float spawnTimer_ = 0.0f;
	
	float spawnInterval_ = 0.5f;
};
} // namespace Game