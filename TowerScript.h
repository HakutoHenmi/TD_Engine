#pragma once
#include "IScript.h"
#include "Scenes/GameScene.h"

namespace Game {

class TowerScript : public IScript {
public:
	void Start(SceneObject& obj, GameScene* scene) override;
	void Update(SceneObject& obj, GameScene* scene, float dt) override;
	void OnDestroy(SceneObject& obj, GameScene* scene) override; 

private:
	float rotateSpeed_ = 2.0f; // 回転速度（1秒あたり）
};

} // namespace Game