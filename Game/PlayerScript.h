#pragma once
#include "IScript.h"
#include "../Engine/Input.h"
#include <Windows.h>

namespace Game {

class PlayerScript : public IScript {
public:
	void Start(SceneObject& obj, GameScene* scene) override;
	void Update(SceneObject& obj, GameScene* scene, float dt) override;
	void OnDestroy(SceneObject& obj, GameScene* scene) override;

private:
	float speed_ = 5.0f;
	float shootTimer_ = 0.0f;
};

} // namespace Game
