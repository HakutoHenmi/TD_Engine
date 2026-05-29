#include "Guardian.h"
#include "../ScriptEngine.h"

// 攻撃のクールタイム
static inline const float kCooltime = 2.0f;	// 秒

namespace Game {
	void Guardian::Start(entt::entity entity, GameScene* scene) {
		// 継承元のStartを呼んで大まかな部分の初期化
		BaseEnemy::Start(entity, scene);

		hp_ = 150.0f;
		maxHp_ = 150.0f;
		attackRange_ = 4.0f; // 物理コライダー(3m)の外から攻撃できるように拡大
		attackCooltime_ = kCooltime;	
		SetCategory(entity, scene, Tank);
	}

	void Game::Guardian::ExecuteAttack(entt::entity entity, GameScene* scene, float /*dt*/) {
		auto& registry = scene->GetRegistry();

		// HitBoxが無ければ該当コンポーネントを追加
		if (!registry.all_of<HitboxComponent>(entity)) {
			registry.emplace<HitboxComponent>(entity);
		}

		// HitBoxコンポーネントの設定
		auto& hb = registry.get<HitboxComponent>(entity);
		hb.isActive = true;
		hb.damage = 20.0f;
		hb.size = { 4.5f, 3.0f, 4.5f }; // 離れた位置からでも届くように拡大

		// クールタイムリセット
		attackCooltime_ = kCooltime;
	}

REGISTER_SCRIPT(Guardian);
} // namespace Game