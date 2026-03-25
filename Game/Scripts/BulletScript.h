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
	float lifeTime_ = 0.0f;    // 生存時間
	float maxLifeTime_ = 3.0f; // 何秒で消すか
};

} // namespace Game
