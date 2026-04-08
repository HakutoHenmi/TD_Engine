#include "ExplosionAttackArea.h"
#include "ObjectTypes.h"
#include "Scenes/GameScene.h"
#include "ScriptEngine.h"

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

	if (registry.all_of<HitboxComponent>(entity)) {
		HitboxComponent& hitbox = registry.get<HitboxComponent>(entity);
		hitbox.isActive = true;
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
	ImGui::DragFloat("Max Life Time", &maxLifeTime_, 0.01f, 0.01f, 5.0f);
#endif
}

REGISTER_SCRIPT(ExplosionAttackArea);

} // namespace Game