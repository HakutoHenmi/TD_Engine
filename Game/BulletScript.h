#pragma once
#include "IScript.h"

namespace Game {

class BulletScript : public IScript {
public:
	void Start(SceneObject& obj, GameScene* scene) override;
	void Update(SceneObject& obj, GameScene* scene, float dt) override;
	void OnDestroy(SceneObject& obj, GameScene* scene) override;

private:
	float speed_ = 30.0f;
};

} // namespace Game
