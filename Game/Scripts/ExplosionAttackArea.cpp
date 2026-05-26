#include "ExplosionAttackArea.h"
#include "ObjectTypes.h"
#include "Scenes/GameScene.h"
#include "ScriptEngine.h"
#include <cmath>

#ifdef USE_IMGUI
#include "../../externals/imgui/imgui.h"
#endif

namespace Game {

void ExplosionAttackArea::Start(entt::entity entity, GameScene* scene) {
	if (!scene) {
		return;
	}

	entt::registry& registry = scene->GetRegistry();

	if (!registry.valid(entity)) {
		return;
	}

	damage_ =  GetVar(entity, scene, "Damage", 1.0f);
	radius_ = GetVar(entity, scene, "ExplosionRadius", 1.0f);

	lifeTime_ = 0.0f;

	if (!registry.all_of<TransformComponent>(entity)) {
		return;
	}

	const TransformComponent& explosionTransform = registry.get<TransformComponent>(entity);

	const std::vector<entt::entity>& enemies = scene->GetEntitiesByTag(TagType::Enemy);

	for (entt::entity enemy : enemies) {
		if (!registry.valid(enemy)) {
			continue;
		}

		if (!registry.all_of<TransformComponent>(enemy)) {
			continue;
		}

		const TransformComponent& enemyTransform = registry.get<TransformComponent>(enemy);

		float diffX = enemyTransform.translate.x - explosionTransform.translate.x;
		float diffY = enemyTransform.translate.y - explosionTransform.translate.y;
		float diffZ = enemyTransform.translate.z - explosionTransform.translate.z;

		float distance = std::sqrt(diffX * diffX + diffY * diffY + diffZ * diffZ);

		if (distance <= radius_) {
			if (registry.all_of<HealthComponent>(enemy)) {
				HealthComponent& hc = registry.get<HealthComponent>(enemy);
				
				// ★変更: シールドシステムとプレイヤー攻撃係数への対応
				float isPlayerAtk = GetVar(entity, scene, "IsPlayerAttack", 0.0f);
				bool isPlayerAttack = (isPlayerAtk > 0.5f);
				
				// ダメージ係数 (CombatSystem.h と同等の計算)
				// プレイヤー攻撃: シールドに適正ダメージ(1.0倍)、本体には少しだけダメージ(0.2倍)
				// タワー攻撃: シールドに極小ダメージ(0.1倍)、本体に超大ダメージ(2.0倍)
				float shieldDamageRate = isPlayerAttack ? 1.0f : 0.1f;
				float hpDamageRate = isPlayerAttack ? 0.2f : 2.0f;
				
				float damageToApply = damage_;

				if (hc.shieldHp > 0.0f) {
					float effectiveShieldDamage = damageToApply * shieldDamageRate;
					if (hc.shieldHp >= effectiveShieldDamage) {
						hc.shieldHp -= effectiveShieldDamage;
						damageToApply = 0.0f; // 吸収
					} else {
						float overflowDamage = effectiveShieldDamage - hc.shieldHp;
						hc.shieldHp = 0.0f; // 破壊
						damageToApply = (overflowDamage / shieldDamageRate) * hpDamageRate;
					}
				} else {
					damageToApply *= hpDamageRate;
				}

				hc.hp -= damageToApply;
				hc.hitFlashTimer = 0.2f; // ヒット演出
			} else {
				// HealthComponentがない場合はとりあえず即死（旧仕様維持）
				scene->DestroyObject(static_cast<uint32_t>(enemy));
			}
		}
	}

	if (registry.all_of<HitboxComponent>(entity)) {
		HitboxComponent& hitbox = registry.get<HitboxComponent>(entity);
		hitbox.isActive = false;
	}
}

void ExplosionAttackArea::Update(entt::entity entity, GameScene* scene, float dt) {
	if (!scene) {
		return;
	}

	entt::registry& registry = scene->GetRegistry();

	if (!registry.valid(entity)) {
		return;
	}

	lifeTime_ += dt;

	if (lifeTime_ >= maxLifeTime_) {
		scene->DestroyObject(static_cast<uint32_t>(entity));
		return;
	}
}

void ExplosionAttackArea::OnDestroy(entt::entity entity, GameScene* scene) {
	if (!scene) {
		return;
	}

	entt::registry& registry = scene->GetRegistry();

	if (!registry.valid(entity)) {
		return;
	}

	if (!registry.all_of<HitboxComponent>(entity)) {
		return;
	}

	HitboxComponent& hitbox = registry.get<HitboxComponent>(entity);
	hitbox.isActive = false;
}

void ExplosionAttackArea::OnEditorUI() {
#if defined(USE_IMGUI) && !defined(NDEBUG)
	ImGui::DragFloat("Damage", &damage_, 1.0f, 0.0f, 1000.0f);
	ImGui::DragFloat("Radius", &radius_, 0.1f, 0.1f, 50.0f);
	ImGui::DragFloat("Max Life Time", &maxLifeTime_, 0.01f, 0.1f, 5.0f);
#endif
}

REGISTER_SCRIPT(ExplosionAttackArea);

} // namespace Game