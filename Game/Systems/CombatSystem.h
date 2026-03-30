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
		std::vector<entt::entity> attackers;
		for (auto entity : attackerView) attackers.push_back(entity);

		// 全アタッカーの存在確認ログ（120フレーム毎）
		static int listLogCount = 0;
		if (listLogCount++ % 120 == 0) {
			char header[128];
			sprintf_s(header, "[Combat] Total Attackers: %zu\n", attackers.size());
			OutputDebugStringA(header);
			for (auto attackerEntity : attackers) {
				std::string name = registry.all_of<NameComponent>(attackerEntity) ? registry.get<NameComponent>(attackerEntity).name : "Unknown";
				std::string tag = registry.all_of<TagComponent>(attackerEntity) ? registry.get<TagComponent>(attackerEntity).tag : "NoTag";
				char item[256];
				sprintf_s(item, "  - Entity: %s, Tag: %s, HitboxEnabled: %d, HitboxActive: %d\n", 
					name.c_str(), tag.c_str(), 
					registry.get<HitboxComponent>(attackerEntity).enabled ? 1 : 0,
					registry.get<HitboxComponent>(attackerEntity).isActive ? 1 : 0);
				OutputDebugStringA(item);
			}
		}

		for (auto attackerEntity : attackers) {
			if (!registry.valid(attackerEntity)) continue;
			auto& hitbox = registry.get<HitboxComponent>(attackerEntity);
			if (!hitbox.enabled) continue;

			// アタッカー（剣など）のワールド行列取得
			::Engine::Matrix4x4 aWorld = ::Engine::Matrix4x4::Identity();
			if (ctx.scene) aWorld = ctx.scene->GetWorldMatrix(static_cast<int>(attackerEntity));
			
			DirectX::XMMATRIX aWorldMat = DirectX::XMLoadFloat4x4(reinterpret_cast<const DirectX::XMFLOAT4X4*>(&aWorld));
			DirectX::XMVECTOR aCenter = DirectX::XMVector3TransformCoord(DirectX::XMLoadFloat3(reinterpret_cast<const DirectX::XMFLOAT3*>(&hitbox.center)), aWorldMat);
			
			DirectX::XMVECTOR aAxes[3];
			aAxes[0] = DirectX::XMVector3Normalize(aWorldMat.r[0]);
			aAxes[1] = DirectX::XMVector3Normalize(aWorldMat.r[1]);
			aAxes[2] = DirectX::XMVector3Normalize(aWorldMat.r[2]);
			
			DirectX::XMVECTOR aScale;
			DirectX::XMVECTOR aRot;
			DirectX::XMVECTOR aTrans;
			DirectX::XMMatrixDecompose(&aScale, &aRot, &aTrans, aWorldMat);
			
			float aExtents[3] = {
				hitbox.size.x * 0.5f * std::abs(DirectX::XMVectorGetX(aScale)),
				hitbox.size.y * 0.5f * std::abs(DirectX::XMVectorGetY(aScale)),
				hitbox.size.z * 0.5f * std::abs(DirectX::XMVectorGetZ(aScale))
			};

			// Hurtboxを持つエンティティと衝突判定
			auto defenderView = registry.view<HurtboxComponent, TransformComponent>();
			for (auto defenderEntity : defenderView) {
				if (attackerEntity == defenderEntity) continue;
				auto& hurtbox = registry.get<HurtboxComponent>(defenderEntity);
				if (!hurtbox.enabled) continue;

				::Engine::Matrix4x4 dWorld = ::Engine::Matrix4x4::Identity();
				if (ctx.scene) dWorld = ctx.scene->GetWorldMatrix(static_cast<int>(defenderEntity));
				DirectX::XMMATRIX dWorldMat = DirectX::XMLoadFloat4x4(reinterpret_cast<const DirectX::XMFLOAT4X4*>(&dWorld));
				DirectX::XMVECTOR dCenter = DirectX::XMVector3TransformCoord(DirectX::XMLoadFloat3(reinterpret_cast<const DirectX::XMFLOAT3*>(&hurtbox.center)), dWorldMat);
				
				DirectX::XMVECTOR dAxes[3];
				dAxes[0] = DirectX::XMVector3Normalize(dWorldMat.r[0]);
				dAxes[1] = DirectX::XMVector3Normalize(dWorldMat.r[1]);
				dAxes[2] = DirectX::XMVector3Normalize(dWorldMat.r[2]);
				
				DirectX::XMVECTOR dScale, dRot, dTrans;
				DirectX::XMMatrixDecompose(&dScale, &dRot, &dTrans, dWorldMat);
				float dExtents[3] = {
					hurtbox.size.x * 0.5f * std::abs(DirectX::XMVectorGetX(dScale)),
					hurtbox.size.y * 0.5f * std::abs(DirectX::XMVectorGetY(dScale)),
					hurtbox.size.z * 0.5f * std::abs(DirectX::XMVectorGetZ(dScale))
				};

				if (CheckObbOverlap(aCenter, aAxes, aExtents, dCenter, dAxes, dExtents) && hitbox.isActive) {
					// 判定スキップロジック
					std::string aTag = registry.all_of<TagComponent>(attackerEntity) ? registry.get<TagComponent>(attackerEntity).tag : "Untagged";
					std::string dTag = registry.all_of<TagComponent>(defenderEntity) ? registry.get<TagComponent>(defenderEntity).tag : "Untagged";
					
					bool skipDamage = false;
					if (aTag == "PlayerSword" || aTag == "Sword") { if (dTag != "Enemy") skipDamage = true; }
					if (aTag != "Untagged" && aTag == dTag) skipDamage = true;
					if (skipDamage) continue;

					if (registry.all_of<HealthComponent>(defenderEntity)) {
						auto& hc = registry.get<HealthComponent>(defenderEntity);
						if (hc.invincibleTime <= 0.0f) {
							hc.hp -= hitbox.damage * hurtbox.damageMultiplier;
							hc.invincibleTime = 0.5f;

							// ★追加: ヒット演出トリガー
							hc.hitFlashTimer = 0.2f; // 0.2秒間光る
							hc.hitStopTimer = 0.1f;  // 0.1秒間停止

							ApplyKnockback(registry, attackerEntity, defenderEntity);

							if (aTag == "Bullet" && ctx.scene) {
								ctx.scene->DestroyObject(static_cast<uint32_t>(attackerEntity));
							}
						}
					}
				}
			}

			// BoxColliderフォールバックも OBB で実行
			auto bcView = registry.view<BoxColliderComponent, TransformComponent>();
			for (auto defenderEntity : bcView) {
				if (attackerEntity == defenderEntity) continue;
				if (registry.all_of<HurtboxComponent>(defenderEntity)) continue;
				auto& bc = registry.get<BoxColliderComponent>(defenderEntity);
				if (!bc.enabled) continue;

				::Engine::Matrix4x4 dWorld = ctx.scene->GetWorldMatrix(static_cast<int>(defenderEntity));
				DirectX::XMMATRIX dWorldMat = DirectX::XMLoadFloat4x4(reinterpret_cast<const DirectX::XMFLOAT4X4*>(&dWorld));
				DirectX::XMVECTOR dCenter = DirectX::XMVector3TransformCoord(DirectX::XMLoadFloat3(reinterpret_cast<const DirectX::XMFLOAT3*>(&bc.center)), dWorldMat);
				DirectX::XMVECTOR dAxes[3];
				dAxes[0] = DirectX::XMVector3Normalize(dWorldMat.r[0]);
				dAxes[1] = DirectX::XMVector3Normalize(dWorldMat.r[1]);
				dAxes[2] = DirectX::XMVector3Normalize(dWorldMat.r[2]);
				DirectX::XMVECTOR dScale, dRot, dTrans;
				DirectX::XMMatrixDecompose(&dScale, &dRot, &dTrans, dWorldMat);
				float dExtents[3] = {
					bc.size.x * 0.5f * std::abs(DirectX::XMVectorGetX(dScale)),
					bc.size.y * 0.5f * std::abs(DirectX::XMVectorGetY(dScale)),
					bc.size.z * 0.5f * std::abs(DirectX::XMVectorGetZ(dScale))
				};

				if (CheckObbOverlap(aCenter, aAxes, aExtents, dCenter, dAxes, dExtents) && hitbox.isActive) {
					std::string aTag = registry.all_of<TagComponent>(attackerEntity) ? registry.get<TagComponent>(attackerEntity).tag : "Untagged";
					std::string dTag = registry.all_of<TagComponent>(defenderEntity) ? registry.get<TagComponent>(defenderEntity).tag : "Untagged";
					if ((aTag == "PlayerSword" || aTag == "Sword") && dTag != "Enemy") continue;

					if (registry.all_of<HealthComponent>(defenderEntity)) {
						auto& hc = registry.get<HealthComponent>(defenderEntity);
						if (hc.invincibleTime <= 0.0f) {
							hc.hp -= hitbox.damage;
							hc.invincibleTime = 0.5f;

							// ★追加: ヒット演出トリガー
							hc.hitFlashTimer = 0.2f; // 0.2秒間光る
							hc.hitStopTimer = 0.1f;  // 0.1秒間停止

							ApplyKnockback(registry, attackerEntity, defenderEntity);

							if (aTag == "Bullet" && ctx.scene) {
								ctx.scene->DestroyObject(static_cast<uint32_t>(attackerEntity));
							}
						}
					}
				}
			}
		}
	}

private:
	static bool CheckObbOverlap(DirectX::XMVECTOR cA, DirectX::XMVECTOR* axesA, float* extA, 
						      DirectX::XMVECTOR cB, DirectX::XMVECTOR* axesB, float* extB) {
		DirectX::XMVECTOR L_axes[15];
		for (int i = 0; i < 3; ++i) { L_axes[i] = axesA[i]; L_axes[i + 3] = axesB[i]; }
		for (int i = 0; i < 3; ++i) {
			for (int j = 0; j < 3; ++j) {
				L_axes[6 + i * 3 + j] = DirectX::XMVector3Cross(axesA[i], axesB[j]);
			}
		}

		DirectX::XMVECTOR relPos = DirectX::XMVectorSubtract(cA, cB);

		for (int i = 0; i < 15; ++i) {
			DirectX::XMVECTOR L = L_axes[i];
			float lenSq = DirectX::XMVectorGetX(DirectX::XMVector3LengthSq(L));
			if (lenSq < 0.001f) continue;
			L = DirectX::XMVectorScale(L, 1.0f / std::sqrt(lenSq));

			float rA = 0, rB = 0;
			for (int m = 0; m < 3; ++m) {
				rA += std::abs(DirectX::XMVectorGetX(DirectX::XMVector3Dot(axesA[m], L))) * extA[m];
				rB += std::abs(DirectX::XMVectorGetX(DirectX::XMVector3Dot(axesB[m], L))) * extB[m];
			}
			float dist = std::abs(DirectX::XMVectorGetX(DirectX::XMVector3Dot(relPos, L)));
			if (dist > rA + rB) return false;
		}
		return true;
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
			float knockbackPower = 35.0f;
			dRb.velocity.x += (dx / dist) * knockbackPower;
			dRb.velocity.z += (dz / dist) * knockbackPower;
			dRb.velocity.y += 10.0f; // 囲みを抜けるため高めに跳ね上げる
		}
	}
};

} // namespace Game
