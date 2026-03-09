#pragma once
#include "IScript.h"
#include "Scenes/GameScene.h"

namespace Game {

class BulletTank : public IScript {
public:
	void Start(SceneObject& obj, GameScene* scene) override;
	void Update(SceneObject& obj, GameScene* scene, float dt) override;
	void OnDestroy(SceneObject& obj, GameScene* scene) override;
	
	

private:
	float rotationSpeed_ = 1.0f; // タワーの回転速度
};

} // namespace Game