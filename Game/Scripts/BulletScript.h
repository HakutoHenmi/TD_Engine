#pragma once
#include "IScript.h"

namespace Game {

class BulletScript : public IScript {
public:
	void Start(entt::entity entity, GameScene* scene) override;
	void Update(entt::entity entity, GameScene* scene, float dt) override;
	void OnDestroy(entt::entity entity, GameScene* scene) override;

private:
	float speed_ = 30.0f;
};

} // namespace Game
