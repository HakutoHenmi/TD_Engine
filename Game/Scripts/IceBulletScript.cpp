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
	vfxCreated_ = false;
	bulletVfx_ = entt::null;

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

	// 雪の結晶型パーティクルエフェクトをアタッチし、毎フレーム位置を同期
	if (!vfxCreated_) {
		vfxCreated_ = true;
		Engine::Renderer* renderer = scene->GetRenderer();
		if (renderer) {
			bulletVfx_ = scene->CreateEntity("IceBullet_Trail_VFX");
			scene->SetTag(bulletVfx_, TagType::VFX);
			auto& vfxTrans = registry.get<TransformComponent>(bulletVfx_);
			vfxTrans.translate = bulletTransform.translate;

			auto& pec = registry.emplace<ParticleEmitterComponent>(bulletVfx_);
			pec.emitter.params.name = "IceBulletTrail";
			pec.emitter.params.texturePath = "Resources/Textures/particles/prism_star.png"; // 雪の結晶
			pec.emitter.params.emitRate = 35.0f;
			pec.emitter.params.shape = Engine::EmissionShape::Sphere;
			pec.emitter.params.shapeRadius = 0.3f;
			pec.emitter.params.lifeTime = 0.5f;
			pec.emitter.params.lifeTimeVariance = 0.15f;
			pec.emitter.params.startVelocity = {0.0f, 0.0f, 0.0f};
			pec.emitter.params.velocityVariance = {0.5f, 0.5f, 0.5f};
			pec.emitter.params.startColor = {0.7f, 0.9f, 1.0f, 1.5f};
			pec.emitter.params.endColor = {0.3f, 0.6f, 1.0f, 0.0f};
			pec.emitter.params.startSize = {0.25f, 0.25f, 0.25f};
			pec.emitter.params.endSize = {0.01f, 0.01f, 0.01f};
			pec.emitter.params.angularVelocity = {0.0f, 0.0f, 5.0f};
			pec.emitter.params.angularVelocityVariance = {0.0f, 0.0f, 10.0f};
			pec.emitter.params.isAdditive = true;
			pec.emitter.params.position = {bulletTransform.translate.x, bulletTransform.translate.y, bulletTransform.translate.z};

			pec.emitter.Initialize(*renderer, "IceBulletTrail");
			pec.isInitialized = true;
		}
	} else {
		if (registry.valid(bulletVfx_) && registry.all_of<TransformComponent>(bulletVfx_)) {
			auto& vfxTrans = registry.get<TransformComponent>(bulletVfx_);
			vfxTrans.translate = bulletTransform.translate;
			if (registry.all_of<ParticleEmitterComponent>(bulletVfx_)) {
				auto& pec = registry.get<ParticleEmitterComponent>(bulletVfx_);
				pec.emitter.params.position = {bulletTransform.translate.x, bulletTransform.translate.y, bulletTransform.translate.z};
			}
		}
	}

	lifeTime_ += dt;

	if (lifeTime_ >= maxLifeTime_) {
		scene->DestroyObject(static_cast<uint32_t>(entity));
		return;
	}

if (hasTarget_) {
		if (!registry.valid(target_)) {
			hasTarget_ = false;
			target_ = entt::null;
		} else if (!registry.all_of<TransformComponent>(target_)) {
			hasTarget_ = false;
			target_ = entt::null;
		}
	}

	if (hasTarget_) {
		if (registry.valid(target_) && registry.all_of<TransformComponent>(target_)) {
			TransformComponent& targetTransform = registry.get<TransformComponent>(target_);

			lastTargetPosition_ = targetTransform.translate;
			hasLastTargetPosition_ = true;
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

if (!hasTarget_) {
		if (hasLastTargetPosition_) {
			float directionX = lastTargetPosition_.x - bulletTransform.translate.x;
			float directionY = lastTargetPosition_.y - bulletTransform.translate.y;
			float directionZ = lastTargetPosition_.z - bulletTransform.translate.z;

			float length = std::sqrt(directionX * directionX + directionY * directionY + directionZ * directionZ);

			if (length <= 0.2f) {
				scene->DestroyObject(static_cast<uint32_t>(entity));
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
			return;
		}

		// 今まで通り前に飛ぶ
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

void IceBulletScript::OnDestroy(entt::entity /*entity*/, GameScene* scene) {
	if (scene && vfxCreated_ && scene->GetRegistry().valid(bulletVfx_)) {
		scene->DestroyObject(static_cast<uint32_t>(bulletVfx_));
	}
}

REGISTER_SCRIPT(IceBulletScript);

} // namespace Game