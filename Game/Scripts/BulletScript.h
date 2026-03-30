#pragma once
#include "IScript.h"

namespace Game {

class BulletScript : public IScript {
public:
	void Start(entt::entity entity, GameScene* scene) override;
	void Update(entt::entity entity, GameScene* scene, float dt) override;
	void OnDestroy(entt::entity entity, GameScene* scene) override;

	void SetTarget(entt::entity target) { target_ = target; }

private:
	entt::entity target_ = entt::null;

	float speed_ = 30.0f;
	float lifeTime_ = 0.0f;
	float maxLifeTime_ = 3.0f;

	float homingSearchRange_ = 100.0f;
};

} // namespace Game