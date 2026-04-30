#include "IceBulletScript.h"
#include "ObjectTypes.h"
#include "Scenes/GameScene.h"
#include "ScriptEngine.h"
#include <cmath>

namespace Game {

void IceBulletScript::Start(entt::entity entity, GameScene* scene) {
	lifeTime_ = 0.0f;
	hasTarget_ = false;
	target_ = entt::null;

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

	if (registry.valid(entity)) {
		if (registry.all_of<TransformComponent>(entity)) {
			TransformComponent& bulletTransform = registry.get<TransformComponent>(entity);
			centerPosition_.x = bulletTransform.translate.x;
			centerPosition_.y = bulletTransform.translate.y;
			centerPosition_.z = bulletTransform.translate.z;
		}
	}

	spiralAngle_ = static_cast<float>(static_cast<uint32_t>(entity) % 6) * 1.0f;
	spiralRadius_ = 0.0f;
}

void IceBulletScript::Update(entt::entity entity, GameScene* scene, float dt) {
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

	TransformComponent& bulletTransform = registry.get<TransformComponent>(entity);

	lifeTime_ += dt;

	if (lifeTime_ >= maxLifeTime_) {
		scene->DestroyObject(static_cast<uint32_t>(entity));
		return;
	}

	if (hasTarget_) {
		if (!registry.valid(target_)) {
			hasTarget_ = false;
			target_ = entt::null;
		}
	}

	if (hasTarget_) {
		if (!registry.all_of<TransformComponent>(target_)) {
			hasTarget_ = false;
			target_ = entt::null;
		}
	}

	// 1. 上に上がる
	if (lifeTime_ < upTime_) {
		bulletTransform.translate.y += 6.0f * dt;
		bulletTransform.rotate.y += 10.0f * dt;
		return;
	}

	// 2. らせん状に広がる
	if (lifeTime_ < upTime_ + spiralTime_) {
		spiralAngle_ += 12.0f * dt;
		spiralRadius_ += 2.0f * dt;

		bulletTransform.translate.x = centerPosition_.x + std::cos(spiralAngle_) * spiralRadius_;
		bulletTransform.translate.z = centerPosition_.z + std::sin(spiralAngle_) * spiralRadius_;
		bulletTransform.translate.y += 1.5f * dt;

		bulletTransform.rotate.y += 12.0f * dt;
		return;
	}

	// 3. ターゲットがない場合は前に飛ぶ
	if (!hasTarget_) {
		float cosX = std::cos(bulletTransform.rotate.x);
		float moveX = std::sin(bulletTransform.rotate.y) * cosX * speed_ * dt;
		float moveY = -std::sin(bulletTransform.rotate.x) * speed_ * dt;
		float moveZ = std::cos(bulletTransform.rotate.y) * cosX * speed_ * dt;

		bulletTransform.translate.x += moveX;
		bulletTransform.translate.y += moveY;
		bulletTransform.translate.z += moveZ;
		return;
	}

	// 4. 敵を追尾
	TransformComponent& targetTransform = registry.get<TransformComponent>(target_);

	float directionX = targetTransform.translate.x - bulletTransform.translate.x;
	float directionY = targetTransform.translate.y - bulletTransform.translate.y;
	float directionZ = targetTransform.translate.z - bulletTransform.translate.z;

	float length = std::sqrt(directionX * directionX + directionY * directionY + directionZ * directionZ);

	if (length <= 0.0001f) {
		return;
	}

	directionX /= length;
	directionY /= length;
	directionZ /= length;

	bulletTransform.translate.x += directionX * speed_ * dt;
	bulletTransform.translate.y += directionY * speed_ * dt;
	bulletTransform.translate.z += directionZ * speed_ * dt;

	float yaw = std::atan2(directionX, directionZ);
	float horizontalLength = std::sqrt(directionX * directionX + directionZ * directionZ);
	float pitch = std::atan2(-directionY, horizontalLength);

	bulletTransform.rotate.y = yaw;
	bulletTransform.rotate.x = pitch;
}

void IceBulletScript::OnDestroy(entt::entity /*entity*/, GameScene* /*scene*/) {}

REGISTER_SCRIPT(IceBulletScript);

} // namespace Game