#pragma once
#include "ISystem.h"
#include <unordered_map>

namespace Game {

class HealthSystem : public ISystem {
public:
	void Update(entt::registry& registry, GameContext& ctx) override {
		if (!ctx.isPlaying) return;

		auto view = registry.view<HealthComponent>();
		for (auto entity : view) {
			auto& hc = registry.get<HealthComponent>(entity);
			if (!hc.enabled || hc.isDead) continue;

			if (hc.invincibleTime > 0.0f) {
				hc.invincibleTime -= ctx.dt;
				if (hc.invincibleTime < 0.0f) hc.invincibleTime = 0.0f;
			}

			// ダメージ検知用の簡易ロジック
			uint32_t eid = static_cast<uint32_t>(entity);
			if (lastHp_.find(eid) != lastHp_.end()) {
				float diff = lastHp_[eid] - hc.hp;
				if (diff > 0.1f) {
					// ダメージポップアップ（簡易版、WorldSpaceUIコンポーネントが存在する場合）
				}
			}
			lastHp_[eid] = hc.hp;

			if (hc.hp <= 0.0f && !hc.isDead) {
				hc.isDead = true;
			}
		}
	}

	void Reset(entt::registry& registry) override {
		auto view = registry.view<HealthComponent>();
		for (auto entity : view) {
			auto& hc = registry.get<HealthComponent>(entity);
			hc.invincibleTime = 0.0f;
			hc.isDead = false;
			if (hc.hp <= 0) hc.hp = hc.maxHp;
		}
		lastHp_.clear();
	}

private:
	std::unordered_map<uint32_t, float> lastHp_;
};

} // namespace Game
