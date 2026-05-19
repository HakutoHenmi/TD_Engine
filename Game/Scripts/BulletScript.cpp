#include "BulletScript.h"
#include "ObjectTypes.h"
#include "Scenes/GameScene.h"
#include "ScriptEngine.h"
#include <cmath>

namespace Game {

void BulletScript::Start(entt::entity entity, GameScene* scene) {
	lifeTime_ = 0.0f;
	hasTarget_ = false;
	target_ = entt::null;

	if (!scene) {
		return;
	}

	speed_ = GetVar(entity, scene, "Speed", 80.0f); 
	maxLifeTime_ = GetVar(entity, scene, "MaxLifeTime", 5.0f); // ★追加: 寿命を個別設定可能に

	float hasTargetValue = GetVar(entity, scene, "HasTarget", 0.0f);

	if (hasTargetValue > 0.5f) {
		float targetEntityValue = GetVar(entity, scene, "TargetEntity", -1.0f);
		if (targetEntityValue >= 0.0f) {
			// 旧方式
			uint32_t targetEntityId = static_cast<uint32_t>(targetEntityValue);
			target_ = static_cast<entt::entity>(targetEntityId);
			hasTarget_ = true;
		} else {
			// 新方式 (High/Low分割による精度欠落回避)
			float high = GetVar(entity, scene, "TargetHigh", -1.0f);
			float low = GetVar(entity, scene, "TargetLow", -1.0f);
			if (high >= 0.0f && low >= 0.0f) {
				uint32_t targetId = (static_cast<uint32_t>(high) << 16) | static_cast<uint32_t>(low);
				target_ = static_cast<entt::entity>(targetId);
				hasTarget_ = true;
			}
		}
	}
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

	if (!hasTarget_) {
		float cosX = std::cos(bulletTransform.rotate.x);
		float moveX = std::sin(bulletTransform.rotate.y) * cosX * speed_ * dt;
		float moveY = -std::sin(bulletTransform.rotate.x) * speed_ * dt;
		float moveZ = std::cos(bulletTransform.rotate.y) * cosX * speed_ * dt;

		// ★追加: レイキャストによる地形・オブジェクトとの衝突判定
		Engine::Vector3 rayOrig = {bulletTransform.translate.x, bulletTransform.translate.y, bulletTransform.translate.z};
		Engine::Vector3 rayDir = {moveX, moveY, moveZ};
		float moveLen = std::sqrt(moveX*moveX + moveY*moveY + moveZ*moveZ);
		
		if (moveLen > 0.0001f) {
			rayDir.x /= moveLen; rayDir.y /= moveLen; rayDir.z /= moveLen;
			float hitDist = 0.0f;
			if (scene->RayCast(rayOrig, rayDir, moveLen, static_cast<uint32_t>(entity), hitDist)) {
				// 何かに当たった
				bulletTransform.translate.x += rayDir.x * hitDist;
				bulletTransform.translate.y += rayDir.y * hitDist;
				bulletTransform.translate.z += rayDir.z * hitDist;

				if (registry.all_of<VariableComponent>(entity)) {
					auto& vc = registry.get<VariableComponent>(entity);
					if (vc.GetValue("Enhanced", 0.0f) > 0.5f) {
						scene->GetEventSystem().Emit("EnhancedBulletHit", static_cast<float>(static_cast<uint32_t>(entity)));
					}
				}
				scene->DestroyObject(static_cast<uint32_t>(entity));
				return;
			}
		}

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

	float moveAmount = speed_ * dt;

	// ★追加: 追尾中もレイキャストで壁や床にぶつかったら爆発させる
	Engine::Vector3 rayOrig = {bulletTransform.translate.x, bulletTransform.translate.y, bulletTransform.translate.z};
	Engine::Vector3 rayDir = {directionX, directionY, directionZ};
	float hitDist = 0.0f;
	if (scene->RayCast(rayOrig, rayDir, moveAmount, static_cast<uint32_t>(entity), hitDist)) {
		bulletTransform.translate.x += rayDir.x * hitDist;
		bulletTransform.translate.y += rayDir.y * hitDist;
		bulletTransform.translate.z += rayDir.z * hitDist;

		if (registry.all_of<VariableComponent>(entity)) {
			auto& vc = registry.get<VariableComponent>(entity);
			if (vc.GetValue("Enhanced", 0.0f) > 0.5f) {
				scene->GetEventSystem().Emit("EnhancedBulletHit", static_cast<float>(static_cast<uint32_t>(entity)));
			}
		}
		scene->DestroyObject(static_cast<uint32_t>(entity));
		return;
	}

	bulletTransform.translate.x += directionX * moveAmount;
	bulletTransform.translate.y += directionY * moveAmount;
	bulletTransform.translate.z += directionZ * moveAmount;

	float yaw = std::atan2(directionX, directionZ);
	float horizontalLength = std::sqrt(directionX * directionX + directionZ * directionZ);
	float pitch = std::atan2(-directionY, horizontalLength);

	bulletTransform.rotate.y = yaw;
	bulletTransform.rotate.x = pitch;
}

void BulletScript::OnDestroy(entt::entity /*entity*/, GameScene* /*scene*/) {}

REGISTER_SCRIPT(BulletScript);

} // namespace Game