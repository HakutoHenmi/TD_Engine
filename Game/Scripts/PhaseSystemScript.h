#pragma once
#include "IScript.h"

namespace Game {

class PhaseSystemScript : public IScript {
public:
	void Start(SceneObject& obj, GameScene* scene) override;
	void Update(SceneObject& obj, GameScene* scene, float dt) override;
	void OnDestroy(SceneObject& obj, GameScene* scene) override;

	void Installation(GameScene* scene);

	static bool IsPreparation() { return isPreparation_; };

private:
	static bool isPreparation_;

	bool preKeyP_;
};

} // namespace Game