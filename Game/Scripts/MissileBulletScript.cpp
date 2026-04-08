#include "MissileBulletScript.h"
#include "ObjectTypes.h"
#include "Scenes/GameScene.h"
#include "ScriptEngine.h"
#include "ScriptUtils.h"
#include <cmath>

namespace Game {

float MissileBulletScript::LerpFloat(float start, float end, float t) { return start + (end - start) * t; }

void MissileBulletScript::Start(entt::entity entity, GameScene* scene) {
	lifeTime_ = 0.0f;
	hasTarget_ = false;
	target_ = entt::null;
	traveledDistance_ = 0.0f;
	totalDistance_ = 1.0f;

	if (!scene) {
		return;
	}

	float hasTargetValue = GetVar(entity, scene, "HasTarget", 0.0f);

	if (hasTargetValue > 0.5f) {
		float targetEntityValue = GetVar(entity, scene, "TargetEntity", -1.0f);

		if (targetEntityValue >= 0.0f) {
			uint32_t targetEntityId = static_cast<uint32_t>(targetEntityValue);
			target_ = static_cast<entt::entity>(targetEntityId);
			hasTarget_ = true;
		}
	}

	entt::registry& registry = scene->GetRegistry();

	if (!registry.valid(entity)) {
		return;
	}

	if (!registry.all_of<TransformComponent>(entity)) {
		return;
	}

	TransformComponent& missileTransform = registry.get<TransformComponent>(entity);

	startPosition_.x = missileTransform.translate.x;
	startPosition_.y = missileTransform.translate.y;
	startPosition_.z = missileTransform.translate.z;

	if (!hasTarget_) {
		return;
	}

	if (!registry.valid(target_)) {
		return;
	}

	if (!registry.all_of<TransformComponent>(target_)) {
		return;
	}

	TransformComponent& targetTransform = registry.get<TransformComponent>(target_);

	float diffX = targetTransform.translate.x - startPosition_.x;
	float diffY = targetTransform.translate.y - startPosition_.y;
	float diffZ = targetTransform.translate.z - startPosition_.z;

	totalDistance_ = std::sqrt(diffX * diffX + diffY * diffY + diffZ * diffZ);

	if (totalDistance_ < 0.0001f) {
		totalDistance_ = 0.0001f;
	}
}

void MissileBulletScript::Update(entt::entity entity, GameScene* scene, float dt) {
	if (!scene) {
		return;
	}

	entt::registry& registry = scene->GetRegistry();

	if (!registry.valid(entity)) {
		return;
	}

	if (!registry.all_of<TransformComponent>(entity)) {
		return;
	}

	TransformComponent& missileTransform = registry.get<TransformComponent>(entity);

	lifeTime_ += dt;

	if (lifeTime_ >= maxLifeTime_) {
		scene->DestroyObject(static_cast<uint32_t>(entity));
		return;
	}

	if (!hasTarget_) {
		scene->DestroyObject(static_cast<uint32_t>(entity));
		return;
	}

	if (!registry.valid(target_)) {
		scene->DestroyObject(static_cast<uint32_t>(entity));
		return;
	}

	if (!registry.all_of<TransformComponent>(target_)) {
		scene->DestroyObject(static_cast<uint32_t>(entity));
		return;
	}

	TransformComponent& targetTransform = registry.get<TransformComponent>(target_);

	float targetX = targetTransform.translate.x;
	float targetY = targetTransform.translate.y;
	float targetZ = targetTransform.translate.z;

	float diffToTargetX = targetX - missileTransform.translate.x;
	float diffToTargetY = targetY - missileTransform.translate.y;
	float diffToTargetZ = targetZ - missileTransform.translate.z;

	float currentDistance = std::sqrt(diffToTargetX * diffToTargetX + diffToTargetY * diffToTargetY + diffToTargetZ * diffToTargetZ);

	if (currentDistance <= hitDistance_) {
		scene->DestroyObject(static_cast<uint32_t>(entity));
		return;
	}

	traveledDistance_ += moveSpeed_ * dt;

	float t = traveledDistance_ / totalDistance_;

	if (t < 0.0f) {
		t = 0.0f;
	}

	if (t > 1.0f) {
		t = 1.0f;
	}

	float baseX = LerpFloat(startPosition_.x, targetX, t);
	float baseY = LerpFloat(startPosition_.y, targetY, t);
	float baseZ = LerpFloat(startPosition_.z, targetZ, t);

	float heightOffset = 4.0f * arcHeight_ * t * (1.0f - t);

	missileTransform.translate.x = baseX;
	missileTransform.translate.y = baseY + heightOffset;
	missileTransform.translate.z = baseZ;

	float nextT = t + 0.02f;

	if (nextT > 1.0f) {
		nextT = 1.0f;
	}

	float nextBaseX = LerpFloat(startPosition_.x, targetX, nextT);
	float nextBaseY = LerpFloat(startPosition_.y, targetY, nextT);
	float nextBaseZ = LerpFloat(startPosition_.z, targetZ, nextT);

	float nextHeightOffset = 4.0f * arcHeight_ * nextT * (1.0f - nextT);

	float directionX = nextBaseX - missileTransform.translate.x;
	float directionY = (nextBaseY + nextHeightOffset) - missileTransform.translate.y;
	float directionZ = nextBaseZ - missileTransform.translate.z;

	float directionLength = std::sqrt(directionX * directionX + directionY * directionY + directionZ * directionZ);

	if (directionLength > 0.0001f) {
		directionX /= directionLength;
		directionY /= directionLength;
		directionZ /= directionLength;

		float yaw = std::atan2(directionX, directionZ);
		float horizontalLength = std::sqrt(directionX * directionX + directionZ * directionZ);
		float pitch = std::atan2(-directionY, horizontalLength);

		missileTransform.rotate.y = yaw;
		missileTransform.rotate.x = pitch;
	}

	if (t >= 1.0f) {
		scene->DestroyObject(static_cast<uint32_t>(entity));
		return;
	}
}

void MissileBulletScript::OnDestroy(entt::entity /*entity*/, GameScene* /*scene*/) {}

REGISTER_SCRIPT(MissileBulletScript);

} // namespace Game