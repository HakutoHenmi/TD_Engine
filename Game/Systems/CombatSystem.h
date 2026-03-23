#pragma once
#include "ISystem.h"
#include <cmath>

namespace Game {

class CombatSystem : public ISystem {
public:
	void Update(entt::registry& registry, GameContext& ctx) override {
		if (!ctx.isPlaying) return;

		// Hitboxを持つエンティティをリスト化
		auto attackerView = registry.view<HitboxComponent, TransformComponent>();
		std::vector<entt::entity> attackers(attackerView.begin(), attackerView.end());

		for (auto attackerEntity : attackers) {
			if (!registry.valid(attackerEntity)) continue;
			auto& hitbox = registry.get<HitboxComponent>(attackerEntity);
			if (!hitbox.enabled || !hitbox.isActive) continue;

			auto& aTc = registry.get<TransformComponent>(attackerEntity);
			DirectX::XMFLOAT3 hAPos = {
				aTc.translate.x + hitbox.center.x * std::abs(aTc.scale.x),
				aTc.translate.y + hitbox.center.y * std::abs(aTc.scale.y),
				aTc.translate.z + hitbox.center.z * std::abs(aTc.scale.z)
			};
			DirectX::XMFLOAT3 hASize = {
				hitbox.size.x * std::abs(aTc.scale.x),
				hitbox.size.y * std::abs(aTc.scale.y),
				hitbox.size.z * std::abs(aTc.scale.z)
			};

			// Hurtboxを持つエンティティと衝突判定
			auto defenderView = registry.view<HurtboxComponent, TransformComponent>();
			for (auto defenderEntity : defenderView) {
				if (attackerEntity == defenderEntity) continue;
				if (!registry.valid(defenderEntity)) continue;

				// プレイヤーと自身の剣の間での当たり判定をスキップ
				bool skipSelfDamage = false;
				if (registry.all_of<TagComponent>(attackerEntity) && registry.all_of<TagComponent>(defenderEntity)) {
					auto& aTag = registry.get<TagComponent>(attackerEntity);
					auto& dTag = registry.get<TagComponent>(defenderEntity);
					if ((aTag.tag == "Player" && dTag.tag == "PlayerSword") ||
						(aTag.tag == "PlayerSword" && dTag.tag == "Player")) {
						skipSelfDamage = true;
					}
				}
				if (skipSelfDamage) continue;

				auto& hurtbox = registry.get<HurtboxComponent>(defenderEntity);
				auto& dTc = registry.get<TransformComponent>(defenderEntity);
				if (!hurtbox.enabled) continue;

				DirectX::XMFLOAT3 hBPos = {
					dTc.translate.x + hurtbox.center.x * std::abs(dTc.scale.x),
					dTc.translate.y + hurtbox.center.y * std::abs(dTc.scale.y),
					dTc.translate.z + hurtbox.center.z * std::abs(dTc.scale.z)
				};
				DirectX::XMFLOAT3 hBSize = {
					hurtbox.size.x * std::abs(dTc.scale.x),
					hurtbox.size.y * std::abs(dTc.scale.y),
					hurtbox.size.z * std::abs(dTc.scale.z)
				};

				if (CheckAABBOverlap(hAPos, hASize, hBPos, hBSize)) {
					if (registry.all_of<HealthComponent>(defenderEntity)) {
						auto& hc = registry.get<HealthComponent>(defenderEntity);
						if (hc.invincibleTime <= 0.0f) {
							hc.hp -= hitbox.damage * hurtbox.damageMultiplier;
							hc.invincibleTime = 0.5f;
							ApplyKnockback(registry, attackerEntity, defenderEntity);
						}
					}

					// 弾が当たったら破壊
					if (registry.all_of<TagComponent>(attackerEntity)) {
						if (registry.get<TagComponent>(attackerEntity).tag == "Bullet") {
							if (registry.all_of<HealthComponent>(attackerEntity)) {
								registry.get<HealthComponent>(attackerEntity).isDead = true;
							}
						}
					}
				}
			}

			// BoxColliderフォールバック（Hurtboxがない場合）
			auto bcView = registry.view<BoxColliderComponent, TransformComponent>();
			for (auto defenderEntity : bcView) {
				if (attackerEntity == defenderEntity) continue;
				if (!registry.valid(defenderEntity)) continue;
				if (registry.all_of<HurtboxComponent>(defenderEntity)) continue; // Hurtbox持ちは既に処理済み

				auto& bc = registry.get<BoxColliderComponent>(defenderEntity);
				auto& dTc = registry.get<TransformComponent>(defenderEntity);
				if (!bc.enabled) continue;

				DirectX::XMFLOAT3 hBPos = {
					dTc.translate.x + bc.center.x,
					dTc.translate.y + bc.center.y,
					dTc.translate.z + bc.center.z
				};
				DirectX::XMFLOAT3 defSize = {
					bc.size.x * std::abs(dTc.scale.x),
					bc.size.y * std::abs(dTc.scale.y),
					bc.size.z * std::abs(dTc.scale.z)
				};

				if (CheckAABBOverlap(hAPos, hASize, hBPos, defSize)) {
					if (registry.all_of<HealthComponent>(defenderEntity)) {
						auto& hc = registry.get<HealthComponent>(defenderEntity);
						if (hc.invincibleTime <= 0.0f) {
							hc.hp -= hitbox.damage;
							hc.invincibleTime = 0.5f;
						}
					}

					// 弾が当たったら破壊
					if (registry.all_of<TagComponent>(attackerEntity)) {
						if (registry.get<TagComponent>(attackerEntity).tag == "Bullet") {
							if (registry.all_of<HealthComponent>(attackerEntity)) {
								registry.get<HealthComponent>(attackerEntity).isDead = true;
							}
						}
					}
				}
			}
		}
	}

private:
	static bool CheckAABBOverlap(const DirectX::XMFLOAT3& posA, const DirectX::XMFLOAT3& sizeA,
		const DirectX::XMFLOAT3& posB, const DirectX::XMFLOAT3& sizeB) {
		// size は全幅を想定しているため、0.5倍して半辺長(extents)で判定する
		return std::abs(posA.x - posB.x) < (sizeA.x + sizeB.x) * 0.5f &&
		       std::abs(posA.y - posB.y) < (sizeA.y + sizeB.y) * 0.5f &&
		       std::abs(posA.z - posB.z) < (sizeA.z + sizeB.z) * 0.5f;
	}

	static void ApplyKnockback(entt::registry& registry, entt::entity attacker, entt::entity defender) {
		if (!registry.all_of<RigidbodyComponent>(defender) || !registry.all_of<TransformComponent>(attacker) || !registry.all_of<TransformComponent>(defender)) return;
		auto& dRb = registry.get<RigidbodyComponent>(defender);
		if (dRb.isKinematic) return;

		auto& aTc = registry.get<TransformComponent>(attacker);
		auto& dTc = registry.get<TransformComponent>(defender);
		float dx = dTc.translate.x - aTc.translate.x;
		float dz = dTc.translate.z - aTc.translate.z;
		float dist = std::sqrt(dx * dx + dz * dz);
		if (dist > 0.001f) {
			float knockbackPower = 10.0f;
			dRb.velocity.x += (dx / dist) * knockbackPower;
			dRb.velocity.z += (dz / dist) * knockbackPower;
			dRb.velocity.y += 4.0f;
		}
	}
};

} // namespace Game
