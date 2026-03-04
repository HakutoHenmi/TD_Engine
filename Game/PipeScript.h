#pragma once
#include "IScript.h"
#include "Scenes/GameScene.h"

namespace Game {

class PipeScript : public IScript {
public:
	void Start(SceneObject& obj, GameScene* scene) override;
	void Update(SceneObject& obj, GameScene* scene, float dt) override;
	void OnDestroy(SceneObject& obj, GameScene* scene) override;

private:
	float rotationSpeed_ = 1.0f; // パイプの回転速度（ラジアン/秒）
};

}// namespace Game