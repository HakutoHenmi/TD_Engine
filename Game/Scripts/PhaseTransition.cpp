#include "PhaseTransition.h"
#include "ObjectTypes.h"
#include "Scenes/GameScene.h"
#include "ScriptEngine.h"
#include <cmath>
#include <iostream>
#include <algorithm>

#include "PhaseSystemScript.h"

namespace Game {

void PhaseTransition::Start(entt::entity entity, GameScene* scene) {
  isAvailable_ = true;
	fadeState_ = FadeState::Idle;
	requestFade_ = false;
	switchPoint_ = false;
	alpha_ = 0.0f;

	if (scene->GetRegistry().all_of<UIImageComponent>(entity))
		scene->GetRegistry().get<UIImageComponent>(entity).color = {0, 0, 0, 0};
}

void PhaseTransition::Update(entt::entity entity, GameScene* scene, float dt) {
	if (!scene) return;
	auto& registry = scene->GetRegistry();
	if (!registry.valid(entity) || !registry.all_of<UIImageComponent>(entity)) return;

	if (requestFade_) {
		requestFade_ = false;
		switchPoint_ = false;
		fadeState_ = FadeState::FadeOut;
	}

	const float speed = (fadeDuration_ > 0.0001f) ? (1.0f / fadeDuration_) : 1000.0f;
	if (fadeState_ == FadeState::FadeOut) {
		alpha_ += speed * dt;
		if (alpha_ >= 1.0f) {
			alpha_ = 1.0f;
			switchPoint_ = true;
			fadeState_ = FadeState::FadeIn;
		}
	} else if (fadeState_ == FadeState::FadeIn) {
		alpha_ -= speed * dt;
		if (alpha_ <= 0.0f) {
			alpha_ = 0.0f;
			fadeState_ = FadeState::Idle;
		}
	}

	auto& img = registry.get<UIImageComponent>(entity);
	img.color = {0.0f, 0.0f, 0.0f, std::clamp(alpha_, 0.0f, 1.0f)};
}


void PhaseTransition::RequestFade(float duration) {
	fadeDuration_ = (duration > 0.01f) ? duration : 0.01f;
	requestFade_ = true;
}

bool PhaseTransition::ConsumeSwitchPoint() {
	if (!switchPoint_)
		return false;
	switchPoint_ = false;
	return true;
}

bool PhaseTransition::IsAvailable() {
	return isAvailable_;
}

void PhaseTransition::OnDestroy(entt::entity /*entity*/, GameScene* /*scene*/) {
	isAvailable_ = false;
	fadeState_ = FadeState::Idle;
	requestFade_ = false;
	switchPoint_ = false;
	alpha_ = 0.0f;
}

REGISTER_SCRIPT(PhaseTransition);

} // namespace Game