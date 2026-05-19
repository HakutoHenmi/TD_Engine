#pragma once
#include "BaseEnemy.h"
#include <../entt/entt.hpp>

namespace Game {

class Boss : public BaseEnemy {
public:
	// 初期化関数
	void Start(entt::entity entity, GameScene* scene) override;

	// 毎フレームの更新処理
	void Update(entt::entity entity, GameScene* scene, float dt) override;

	// 固有の攻撃処理
	void ExecuteAttack(entt::entity entity, GameScene* scene, float dt) override;

	// 破棄時の処理
	void OnDestroy(entt::entity entity, GameScene* scene) override;

private:
	// シールド用のエンティティID
	entt::entity shieldEntity_ = entt::null;
	// シールドの半径
	float shieldRadius_ = 12.0f;
};

} // namespace Game
