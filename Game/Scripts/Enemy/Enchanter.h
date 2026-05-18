#pragma once
#include "BaseEnemy.h"

namespace Game {
class Enchanter : public BaseEnemy {
public:
	void Start(entt::entity entity, GameScene* scene) override;
	void Update(entt::entity entity, GameScene* scene, float dt) override;
	void ExecuteAttack(entt::entity entity, GameScene* scene, float dt) override;

private:
	float buffTimer_ = 0.0f;
	float buffInterval_ = 3.0f; // 3秒に1回バフを配る
	float buffRange_ = 25.0f;   // バフが届く範囲
};
} // namespace Game