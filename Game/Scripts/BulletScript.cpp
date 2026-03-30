#include "BulletScript.h"
#include "ObjectTypes.h"
#include "Scenes/GameScene.h"
#include "ScriptEngine.h"
#include <cmath>

namespace Game {

void BulletScript::Start(entt::entity /*entity*/, GameScene* /*scene*/) {}

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
		registry.destroy(entity);
		return;
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

	if (!registry.all_of<TransformComponent>(target_)) {
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
	float pitch = std::atan2(-directionY, std::sqrt(directionX * directionX + directionZ * directionZ));

	bulletTransform.rotate.y = yaw;
	bulletTransform.rotate.x = pitch;
}

void BulletScript::OnDestroy(entt::entity /*entity*/, GameScene* /*scene*/) {}

REGISTER_SCRIPT(BulletScript);

} // namespace Game