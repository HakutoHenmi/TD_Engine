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

		if (distance <= damageRadius_) {
			scene->DestroyObject(static_cast<uint32_t>(enemy));
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
	ImGui::DragFloat("Damage Radius", &damageRadius_, 0.1f, 0.1f, 30.0f);
	ImGui::DragFloat("Max Life Time", &maxLifeTime_, 0.01f, 0.1f, 5.0f);
#endif
}

REGISTER_SCRIPT(ExplosionAttackArea);

} // namespace Game