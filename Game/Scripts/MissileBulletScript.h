#pragma once
#include "IScript.h"

namespace Game {

class MissileBulletScript : public IScript {
public:
	void Start(entt::entity entity, GameScene* scene) override;
	void Update(entt::entity entity, GameScene* scene, float dt) override;
	void OnDestroy(entt::entity entity, GameScene* scene) override;

private:
	struct Vector3 {
		float x;
		float y;
		float z;
	};

	float LerpFloat(float start, float end, float t);

	entt::entity target_ = entt::null;
	bool hasTarget_ = false;

	float lifeTime_ = 0.0f;
	float maxLifeTime_ = 8.0f;

	float moveSpeed_ = 8.0f;
	float arcHeight_ = 12.0f;
	float hitDistance_ = 1.0f;

	Vector3 startPosition_ = {0.0f, 0.0f, 0.0f};
	float totalDistance_ = 1.0f;
	float traveledDistance_ = 0.0f;
};

} // namespace Game