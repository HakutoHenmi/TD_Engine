#pragma once
#include "IScript.h"
#include "Scenes/GameScene.h"
namespace Game {

class SpikeScript : public IScript {

public:
	void Start(SceneObject& obj, GameScene* scene) override;
	void Update(SceneObject& obj, GameScene* scene, float dt) override;
	void OnDestroy(SceneObject& obj, GameScene* scene) override;

private:

	float connectRange = 2.0f;
};

} // namespace Game
