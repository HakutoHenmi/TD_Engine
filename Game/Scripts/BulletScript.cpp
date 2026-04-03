#include "BulletScript.h"
#include "ObjectTypes.h"
#include "Scenes/GameScene.h"
#include "ScriptEngine.h"
#include <cmath>
#include <vector>

namespace Game {

static entt::entity FindNearestEnemy(entt::registry& registry, GameScene* scene, entt::entity bulletEntity, float searchRange) {
	if (!scene) {
		return entt::null;
	}

	if (!registry.valid(bulletEntity)) {
		return entt::null;
	}

	if (!registry.all_of<TransformComponent>(bulletEntity)) {
		return entt::null;
	}

	TransformComponent& bulletTransform = registry.get<TransformComponent>(bulletEntity);

	entt::entity nearestEnemy = entt::null;
	float bestDistance = searchRange;

	const std::vector<entt::entity>& enemies = scene->GetEntitiesByTag("Enemy");

	for (entt::entity enemy : enemies) {
		if (!registry.valid(enemy)) {
			continue;
		}

		if (!registry.all_of<TransformComponent>(enemy)) {
			continue;
		}

		TransformComponent& enemyTransform = registry.get<TransformComponent>(enemy);

		float diffX = enemyTransform.translate.x - bulletTransform.translate.x;
		float diffY = enemyTransform.translate.y - bulletTransform.translate.y;
		float diffZ = enemyTransform.translate.z - bulletTransform.translate.z;

		float distance = std::sqrt(diffX * diffX + diffY * diffY + diffZ * diffZ);

		if (distance < bestDistance) {
			bestDistance = distance;
			nearestEnemy = enemy;
		}
	}

	return nearestEnemy;
}

void BulletScript::Start(entt::entity entity, GameScene*scene ) {
	lifeTime_ = 0.0f;

	if (!scene) {
		target_ = entt::null;
		return;
	}

	entt::registry& registry = scene->GetRegistry();

	if (!registry.valid(entity)) {
		target_ = entt::null;
		return;
	}

	target_ = FindNearestEnemy(registry, scene, entity, homingSearchRange_);
}

void BulletScript::Update(entt::entity entity, GameScene* scene, float dt) {
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

	if (registry.valid(target_)) {
		if (!registry.all_of<TransformComponent>(target_)) {
			target_ = entt::null;
		}
	}

	if (!registry.valid(target_)) {
		float cosX = std::cos(bulletTransform.rotate.x);
		float moveX = std::sin(bulletTransform.rotate.y) * cosX * speed_ * dt;
		float moveY = -std::sin(bulletTransform.rotate.x) * speed_ * dt;
		float moveZ = std::cos(bulletTransform.rotate.y) * cosX * speed_ * dt;

		bulletTransform.translate.x += moveX;
		bulletTransform.translate.y += moveY;
		bulletTransform.translate.z += moveZ;
		return;
	}

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

void BulletScript::OnDestroy(entt::entity /*entity*/, GameScene* /*scene*/) {}

REGISTER_SCRIPT(BulletScript);

} // namespace Game