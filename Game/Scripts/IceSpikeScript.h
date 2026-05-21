#pragma once
#include "IScript.h"
#include "Scenes/GameScene.h"

namespace Game {

class IceSpikeScript : public IScript {
public:
	void Start(entt::entity entity, GameScene* scene) override;
	void Update(entt::entity entity, GameScene* scene, float dt) override;
	void OnDestroy(entt::entity entity, GameScene* scene) override;

private:
	float lifeTime_ = 0.0f;
	float maxLifeTime_ = 1.2f;
	float growDuration_ = 0.15f;
	float targetScaleY_ = 1.0f;
	float baseHeight_ = 0.0f;
};

} // namespace Game
