#pragma once
#include "ISystem.h"

namespace Game {

class HealthSystem : public ISystem {
public:
	void Update(std::vector<SceneObject>& objects, GameContext& ctx) override {
		if (!ctx.isPlaying) return;

		for (auto& obj : objects) {
			bool isInvincible = false;
			for (auto& hc : obj.healths) {
				if (!hc.enabled || hc.isDead) continue;

				if (hc.invincibleTime > 0.0f) {
					hc.invincibleTime -= ctx.dt;
					if (hc.invincibleTime < 0.0f) hc.invincibleTime = 0.0f;
					if (hc.invincibleTime > 0.0f) isInvincible = true;
				}

				if (hc.hp <= 0.0f && !hc.isDead) {
					hc.isDead = true;
				}
			}

			// 被弾リアクションの色変更
			Engine::Vector4 targetColor = isInvincible
				? Engine::Vector4{1.0f, 0.2f, 0.2f, 1.0f}
				: Engine::Vector4{1.0f, 1.0f, 1.0f, 1.0f};
			for (auto& mr : obj.meshRenderers) {
				mr.color = {targetColor.x, targetColor.y, targetColor.z, targetColor.w};
			}
		}
	}

	void Reset(std::vector<SceneObject>& objects) override {
		for (auto& obj : objects) {
			for (auto& hc : obj.healths) {
				hc.invincibleTime = 0.0f;
				hc.isDead = false;
				if (hc.hp <= 0) hc.hp = hc.maxHp;
			}
			for (auto& mr : obj.meshRenderers) {
				mr.color = {1.0f, 1.0f, 1.0f, 1.0f};
			}
		}
	}
};

} // namespace Game
