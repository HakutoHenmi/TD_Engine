#pragma once
#include "IScript.h"

namespace Game {

class PreparationCamera : public IScript {
public:
	void Start(SceneObject& obj, GameScene* scene) override;
	void Update(SceneObject& obj, GameScene* scene, float dt) override;
	void OnDestroy(SceneObject& obj, GameScene* scene) override;

	void UpdateMovement(SceneObject& obj, GameScene* scene, float dt);

private:
};

} // namespace Game