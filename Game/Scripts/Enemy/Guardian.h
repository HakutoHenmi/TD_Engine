#pragma once
#include "BaseEnemy.h"

namespace Game {

class Guardian : public BaseEnemy {
	// 初期化関数
	void Start(entt::entity entity, GameScene* scene) override;

	// 固有の攻撃処理
	void ExecuteAttack(entt::entity entity, GameScene* scene, float dt) override;
};

} // namespace Game