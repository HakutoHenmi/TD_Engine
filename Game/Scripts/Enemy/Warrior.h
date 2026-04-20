#pragma once
#include "BaseEnemy.h"

namespace Game {

class Warrior : public BaseEnemy {
	// 固有の攻撃処理
	void ExecuteAttack(entt::entity entity, GameScene* scene, float dt) override;
};

} // namespace Game