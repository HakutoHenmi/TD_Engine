#pragma once
#include "IScript.h"
#include "Scenes/GameScene.h"

namespace Game {

class BaseScript : public IScript {
public:
	void Start(SceneObject& obj, GameScene* scene) override;
	void Update(SceneObject& obj, GameScene* scene, float dt) override;
	void OnDestroy(SceneObject& obj, GameScene* scene) override;

private:
	float rotationSpeed_ = 1.0f;
	float attackInterval_ = 1.0f;
	float attackTimer_ = 0.0f;
	float damage_ = 10.0f;
	float attackRange_ = 30.0f;
};

} // namespace Game