#pragma once
#include "ISystem.h"
#include <cmath>

namespace Game {

class CombatSystem : public ISystem {
public:
	void Update(std::vector<SceneObject>& objects, GameContext& ctx) override {
		if (!ctx.isPlaying) return;

		for (size_t ai = 0; ai < objects.size(); ++ai) {
			auto& attacker = objects[ai];
				for (auto& hitbox : attacker.hitboxes) {
					if (!hitbox.enabled || !hitbox.isActive) continue;

					// スケールを考慮したHitbox位置とサイズ
					DirectX::XMFLOAT3 hAPos = {
						attacker.translate.x + hitbox.center.x * std::abs(attacker.scale.x),
						attacker.translate.y + hitbox.center.y * std::abs(attacker.scale.y),
						attacker.translate.z + hitbox.center.z * std::abs(attacker.scale.z)
					};
					DirectX::XMFLOAT3 hASize = {
						hitbox.size.x * std::abs(attacker.scale.x),
						hitbox.size.y * std::abs(attacker.scale.y),
						hitbox.size.z * std::abs(attacker.scale.z)
					};

					for (size_t di = 0; di < objects.size(); ++di) {
						if (ai == di) continue;
						auto& defender = objects[di];

						// プレイヤーと自身の剣の間での当たり判定をスキップ（自爆防止）
						if ((attacker.name == "Player" && defender.name == "PlayerSword") ||
							(attacker.name == "PlayerSword" && defender.name == "Player")) {
							continue;
						}

						// Hurtbox判定
						bool hit = false;
						for (auto& hurtbox : defender.hurtboxes) {
							if (!hurtbox.enabled) continue;
							
							// スケールを考慮したHurtbox位置とサイズ
							DirectX::XMFLOAT3 hBPos = {
								defender.translate.x + hurtbox.center.x * std::abs(defender.scale.x),
								defender.translate.y + hurtbox.center.y * std::abs(defender.scale.y),
								defender.translate.z + hurtbox.center.z * std::abs(defender.scale.z)
							};
							DirectX::XMFLOAT3 hBSize = {
								hurtbox.size.x * std::abs(defender.scale.x),
								hurtbox.size.y * std::abs(defender.scale.y),
								hurtbox.size.z * std::abs(defender.scale.z)
							};

							if (CheckAABBOverlap(hAPos, hASize, hBPos, hBSize)) {
								hit = true;
								if (!defender.healths.empty() && defender.healths[0].invincibleTime <= 0.0f) {
									defender.healths[0].hp -= hitbox.damage * hurtbox.damageMultiplier;
									defender.healths[0].invincibleTime = 0.5f;
									ApplyKnockback(attacker, defender);
								}
								break;
							}
						}

					// BoxColliderフォールバック（Hurtboxがない場合）
					if (!hit && defender.hurtboxes.empty() && !defender.boxColliders.empty()) {
						for (auto& bc : defender.boxColliders) {
							if (!bc.enabled) continue;
							DirectX::XMFLOAT3 hBPos = {
								defender.translate.x + bc.center.x,
								defender.translate.y + bc.center.y,
								defender.translate.z + bc.center.z
							};
							DirectX::XMFLOAT3 defSize = {
								bc.size.x * std::abs(defender.scale.x),
								bc.size.y * std::abs(defender.scale.y),
								bc.size.z * std::abs(defender.scale.z)
							};
							if (CheckAABBOverlap(hAPos, hitbox.size, hBPos, defSize)) {
								hit = true;
								if (!defender.healths.empty() && defender.healths[0].invincibleTime <= 0.0f) {
									defender.healths[0].hp -= hitbox.damage;
									defender.healths[0].invincibleTime = 0.5f;
								}
								break;
							}
						}
					}

					// 弾が当たったら破壊
					if (hit) {
						bool isBullet = false;
						for (const auto& t : attacker.tags) {
							if (t.tag == "Bullet") isBullet = true;
						}
						if (isBullet && !attacker.healths.empty()) {
							attacker.healths[0].isDead = true;
						}
					}
				}
			}
		}
	}

private:
	static bool CheckAABBOverlap(const DirectX::XMFLOAT3& posA, const DirectX::XMFLOAT3& sizeA,
		const DirectX::XMFLOAT3& posB, const DirectX::XMFLOAT3& sizeB) {
		return std::abs(posA.x - posB.x) < (sizeA.x + sizeB.x) &&
		       std::abs(posA.y - posB.y) < (sizeA.y + sizeB.y) &&
		       std::abs(posA.z - posB.z) < (sizeA.z + sizeB.z);
	}

	static void ApplyKnockback(const SceneObject& attacker, SceneObject& defender) {
		if (!defender.rigidbodies.empty() && !defender.rigidbodies[0].isKinematic) {
			float dx = defender.translate.x - attacker.translate.x;
			float dz = defender.translate.z - attacker.translate.z;
			float dist = std::sqrt(dx * dx + dz * dz);
			if (dist > 0.001f) {
				float knockbackPower = 5.0f;
				defender.rigidbodies[0].velocity.x += (dx / dist) * knockbackPower;
				defender.rigidbodies[0].velocity.z += (dz / dist) * knockbackPower;
				defender.rigidbodies[0].velocity.y += 2.0f;
			}
		}
	}
};

} // namespace Game
