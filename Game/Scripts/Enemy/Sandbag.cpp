#include "Sandbag.h"
#include "../ScriptEngine.h"

namespace Game {

void Sandbag::Start(entt::entity entity, GameScene* scene) {
	// 継承元のStartを呼んで基本初期化
	BaseEnemy::Start(entity, scene);

	hp_ = 9999.0f;
	maxHp_ = 9999.0f;
	speed_ = 0.0f;           // 動かない
	attackRange_ = 0.0f;     // 攻撃しない
	searchRange_ = 0.0f;     // 索敵しない

	// HealthComponentのHPも同期
	auto& registry = scene->GetRegistry();
	if (registry.all_of<HealthComponent>(entity)) {
		auto& hc = registry.get<HealthComponent>(entity);
		hc.hp = 9999.0f;
		hc.maxHp = 9999.0f;
	}
}

void Sandbag::Update(entt::entity entity, GameScene* scene, float dt) {
	auto& registry = scene->GetRegistry();

	// HPが1以下にならないように毎フレームクランプ
	if (registry.all_of<HealthComponent>(entity)) {
		auto& hc = registry.get<HealthComponent>(entity);
		if (hc.hp < 1.0f) {
			hc.hp = 1.0f;
		}
		hc.isDead = false;  // 絶対に死なない
	}

	// 数秒ごとに全回復
	healTimer_ += dt;
	if (healTimer_ >= healInterval_) {
		healTimer_ = 0.0f;
		if (registry.all_of<HealthComponent>(entity)) {
			auto& hc = registry.get<HealthComponent>(entity);
			hc.hp = hc.maxHp;
		}
	}

	// ※BaseEnemy::Updateは呼ばない（移動・索敵・攻撃を全てスキップ）
	// 攻撃クールタイムの減算もしない
}

void Sandbag::ExecuteAttack(entt::entity /*entity*/, GameScene* /*scene*/, float /*dt*/) {
	// 攻撃しない。何もしない。
}

REGISTER_SCRIPT(Sandbag);
} // namespace Game
