#pragma once
#include "ISystem.h"
#include <algorithm>
#include <cmath>

namespace Game {

class CleanupSystem : public ISystem {
public:
	void Update(std::vector<SceneObject>& objects, GameContext& ctx) override {
		if (!ctx.isPlaying) return;

		// 射程外の弾消去
		for (auto& obj : objects) {
			bool isBullet = false;
			for (const auto& t : obj.tags) {
				if (t.tag == "Bullet") isBullet = true;
			}
			if (isBullet && !obj.healths.empty()) {
				float distSq = obj.translate.x * obj.translate.x +
				               obj.translate.y * obj.translate.y +
				               obj.translate.z * obj.translate.z;
				if (distSq > 10000.0f) {
					obj.healths[0].isDead = true;
				}
			}
		}

		// dead除去
		objects.erase(
			std::remove_if(objects.begin(), objects.end(),
				[](const SceneObject& o) {
					if (o.name == "Player" || o.name == "PlayerSword") return false;
					return !o.healths.empty() && o.healths[0].isDead && o.healths[0].enabled;
				}),
			objects.end());
	}
};

} // namespace Game
