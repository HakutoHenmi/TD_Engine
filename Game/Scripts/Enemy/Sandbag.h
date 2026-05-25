#pragma once
#include "BaseEnemy.h"

namespace Game {

class Sandbag : public BaseEnemy {
	// 初期化関数
	void Start(entt::entity entity, GameScene* scene) override;

	// 毎フレーム処理（移動・攻撃しない独自ロジック）
	void Update(entt::entity entity, GameScene* scene, float dt) override;

	// 攻撃は何もしない（純粋仮想関数の実装義務を満たすだけ）
	void ExecuteAttack(entt::entity entity, GameScene* scene, float dt) override;

private:
	float healTimer_ = 0.0f;       // 回復タイマー
	float healInterval_ = 3.0f;    // 何秒ごとに全回復するか
};

} // namespace Game
