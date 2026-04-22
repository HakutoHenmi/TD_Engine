#include "Guardian.h"
#include "../ScriptEngine.h"

// 攻撃のクールタイム
static inline const float kCooltime = 2.0f;	// 秒

namespace Game {
	void Guardian::Start(entt::entity entity, GameScene* scene) {
		// 継承元のStartを呼んで大まかな部分の初期化
		BaseEnemy::Start(entity, scene);

		attackCooltime_ = kCooltime;	
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
		hb.damage = 50.0f;
		hb.size = { 3.0f, 2.0f, 3.0f };

		// クールタイムリセット
		attackCooltime_ = kCooltime;
	}

REGISTER_SCRIPT(Guardian);
} // namespace Game