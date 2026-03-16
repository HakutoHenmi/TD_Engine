#pragma once
#include "IScript.h"

#include "PlayerScript.h"
namespace Game {

class ExperienceOrbScript : public IScript {
public:
	void Start(SceneObject& obj, GameScene* scene) override;
	void Update(SceneObject& obj, GameScene* scene, float dt) override;
	void OnDestroy(SceneObject& obj, GameScene* scene) override;

private:
	float velocityX_ = 0.0f;
	float velocityY_ = 0.0f;
	float velocityZ_ = 0.0f;

	float startY_ = 0.0f;
	float floatTimer_ = 0.0f;

	bool isFloating_ = false;

	float suctionRange_ = 4.0f;
	float suctionSpeed_ = 6.0f;
	float hitRange_ = 0.8f;
};

} // namespace Game