#include "IceSpikeScript.h"
#include "Scenes/GameScene.h"
#include "ScriptEngine.h"

namespace Game {

void IceSpikeScript::Start(entt::entity entity, GameScene* scene) {
	lifeTime_ = 0.0f;
	auto& registry = scene->GetRegistry();
	if (registry.valid(entity) && registry.all_of<VariableComponent>(entity)) {
		targetScaleY_ = registry.get<VariableComponent>(entity).GetValue("TargetScaleY", 1.0f);
	}
	if (registry.valid(entity) && registry.all_of<TransformComponent>(entity)) {
		baseHeight_ = registry.get<TransformComponent>(entity).translate.y;
	}
}

void IceSpikeScript::Update(entt::entity entity, GameScene* scene, float dt) {
	auto& registry = scene->GetRegistry();
	if (!registry.valid(entity) || !registry.all_of<TransformComponent>(entity)) {
		return;
	}

	lifeTime_ += dt;
	if (lifeTime_ >= maxLifeTime_) {
		scene->DestroyObject(static_cast<uint32_t>(entity));
		return;
	}

	auto& tc = registry.get<TransformComponent>(entity);

	if (lifeTime_ < growDuration_) {
		float t = lifeTime_ / growDuration_;
		t = 1.0f - (1.0f - t) * (1.0f - t); // OutQuad
		tc.scale.y = targetScaleY_ * t;
		tc.translate.y = baseHeight_ + t * 0.5f;
	} else {
		float t = (lifeTime_ - growDuration_) / (maxLifeTime_ - growDuration_);
		if (t > 0.5f) {
			float shrinkT = (t - 0.5f) * 2.0f;
			tc.scale.y = targetScaleY_ * (1.0f - shrinkT);
			tc.scale.x = 0.15f * (1.0f - shrinkT);
			tc.scale.z = 0.15f * (1.0f - shrinkT);
			tc.translate.y = baseHeight_ + 0.5f - shrinkT * 0.6f;
			
			if (registry.all_of<MeshRendererComponent>(entity)) {
				auto& mr = registry.get<MeshRendererComponent>(entity);
				mr.color.w = 1.6f * (1.0f - shrinkT);
			}
		} else {
			tc.scale.y = targetScaleY_;
			tc.translate.y = baseHeight_ + 0.5f;
		}
	}
}

void IceSpikeScript::OnDestroy(entt::entity /*entity*/, GameScene* /*scene*/) {}

REGISTER_SCRIPT(IceSpikeScript);

} // namespace Game
