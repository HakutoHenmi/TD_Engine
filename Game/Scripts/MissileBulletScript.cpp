#include "MissileBulletScript.h"
#include "HitDistortionScript.h"
#include "ObjectTypes.h"
#include "Scenes/GameScene.h"
#include "ScriptEngine.h"
#include "ScriptUtils.h"
#include <cmath>

namespace Game {

float MissileBulletScript::LerpFloat(float start, float end, float t) { return start + (end - start) * t; }

void MissileBulletScript::Start(entt::entity entity, GameScene* scene) {
	lifeTime_ = 0.0f;
	flightTime_ = 0.0f;
	hasTarget_ = false;
	target_ = entt::null;
	engineFlameVfx_ = entt::null;
	trailSmokeVfxA_ = entt::null;
	trailSmokeVfxB_ = entt::null;
	hasLastTargetPosition_ = false;
	lastTargetPosition_ = {0.0f, 0.0f, 0.0f};
	if (!scene) {
		return;
	}

	float hasTargetValue = GetVar(entity, scene, "HasTarget", 0.0f);
	if (hasTargetValue > 0.5f) {
		float high = GetVar(entity, scene, "TargetHigh", -1.0f);
		float low = GetVar(entity, scene, "TargetLow", -1.0f);
		if (high >= 0.0f && low >= 0.0f) {
			uint32_t targetId = (static_cast<uint32_t>(high) << 16) | static_cast<uint32_t>(low);
			target_ = static_cast<entt::entity>(targetId);
			hasTarget_ = true;
		} else {
			float targetEntityValue = GetVar(entity, scene, "TargetEntity", -1.0f);
			if (targetEntityValue >= 0.0f) {
				uint32_t targetEntityId = static_cast<uint32_t>(targetEntityValue);
				target_ = static_cast<entt::entity>(targetEntityId);
				hasTarget_ = true;
			}
		}
	}

	damage_ = GetVar(entity, scene, "Damage", 1.0f);
	explosionRadius_ = GetVar(entity, scene, "ExplosionRadius", 1.0f);

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

	// --- エフェクト初期化 (Start VFX) ---
	Engine::Renderer* renderer = scene->GetRenderer();
	if (renderer) {
		// 1. ミサイルの推進炎
		engineFlameVfx_ = scene->CreateEntity("MissileFlame_VFX");
		scene->SetTag(engineFlameVfx_, TagType::VFX);
		auto& flameTrans = registry.get<TransformComponent>(engineFlameVfx_);
		flameTrans.translate = missileTransform.translate;

		auto& pecFlame = registry.emplace<ParticleEmitterComponent>(engineFlameVfx_);
		pecFlame.emitter.params.name = "MissileFlame";
		pecFlame.emitter.params.texturePath = "Resources/Textures/particles/diamond_flare.png";
		pecFlame.emitter.params.emitRate = 140.0f; // 高密度
		pecFlame.emitter.params.shape = Engine::EmissionShape::Cone;
		pecFlame.emitter.params.shapeRadius = 0.15f;
		pecFlame.emitter.params.shapeAngle = 0.2f;
		pecFlame.emitter.params.lifeTime = 0.35f;
		pecFlame.emitter.params.lifeTimeVariance = 0.1f;
		pecFlame.emitter.params.startVelocity = {0.0f, 0.0f, 0.0f}; // Updateで毎フレーム進行方向の逆に更新
		pecFlame.emitter.params.velocityVariance = {0.5f, 0.5f, 0.5f};
		pecFlame.emitter.params.damping = 0.3f;
		pecFlame.emitter.params.startColor = {0.2f, 0.6f, 1.0f, 1.0f}; // 青
		pecFlame.emitter.params.endColor = {1.0f, 0.4f, 0.0f, 0.0f};   // からオレンジへのグラデーション
		pecFlame.emitter.params.startSize = {0.5f, 0.5f, 0.5f};
		pecFlame.emitter.params.endSize = {0.05f, 0.05f, 0.05f};
		pecFlame.emitter.params.isAdditive = true;

		pecFlame.emitter.Initialize(*renderer, "MissileFlame");
		pecFlame.isInitialized = true;

		// 2. トレイル螺旋煙A
		trailSmokeVfxA_ = scene->CreateEntity("MissileTrailSmokeA_VFX");
		scene->SetTag(trailSmokeVfxA_, TagType::VFX);
		auto& smokeATrans = registry.get<TransformComponent>(trailSmokeVfxA_);
		smokeATrans.translate = missileTransform.translate;

		auto& pecSmokeA = registry.emplace<ParticleEmitterComponent>(trailSmokeVfxA_);
		pecSmokeA.emitter.params.name = "MissileTrailSmokeA";
		pecSmokeA.emitter.params.shaderName = "ProceduralSmoke"; // プレイヤーのブーストと同じプロシージャル3D煙
		pecSmokeA.emitter.params.texturePath = "Resources/Textures/white1x1.png";
		pecSmokeA.emitter.params.emitRate = 55.0f; // 螺旋が滑らかに繋がる高密度
		pecSmokeA.emitter.params.shape = Engine::EmissionShape::Sphere;
		pecSmokeA.emitter.params.shapeRadius = 0.15f;
		pecSmokeA.emitter.params.lifeTime = 1.4f; // 螺旋軌跡として適度に美しく残す
		pecSmokeA.emitter.params.lifeTimeVariance = 0.3f;
		pecSmokeA.emitter.params.startVelocity = {0.0f, 0.3f, 0.0f};
		pecSmokeA.emitter.params.velocityVariance = {0.2f, 0.1f, 0.2f};
		pecSmokeA.emitter.params.acceleration = {0.0f, 0.5f, 0.0f};       // 上昇気流（プロシージャル煙と同じ）
		pecSmokeA.emitter.params.damping = 1.0f;                          // プロシージャル煙と同じ
		pecSmokeA.emitter.params.startColor = {0.85f, 0.9f, 0.95f, 0.7f}; // プレイヤーブーストと同じ極上の白煙色
		pecSmokeA.emitter.params.endColor = {0.6f, 0.65f, 0.7f, 0.0f};    // プレイヤーブーストと同じ
		pecSmokeA.emitter.params.startSize = {0.6f, 0.6f, 0.6f};          // 螺旋軌跡の開始サイズ
		pecSmokeA.emitter.params.endSize = {2.2f, 2.2f, 2.2f};            // 広がりながら消滅
		pecSmokeA.emitter.params.isAdditive = false;

		pecSmokeA.emitter.Initialize(*renderer, "MissileTrailSmokeA");
		pecSmokeA.isInitialized = true;

		// 3. トレイル螺旋煙B
		trailSmokeVfxB_ = scene->CreateEntity("MissileTrailSmokeB_VFX");
		scene->SetTag(trailSmokeVfxB_, TagType::VFX);
		auto& smokeBTrans = registry.get<TransformComponent>(trailSmokeVfxB_);
		smokeBTrans.translate = missileTransform.translate;

		auto& pecSmokeB = registry.emplace<ParticleEmitterComponent>(trailSmokeVfxB_);
		pecSmokeB.emitter.params.name = "MissileTrailSmokeB";
		pecSmokeB.emitter.params.shaderName = "ProceduralSmoke"; // プレイヤーのブーストと同じプロシージャル3D煙
		pecSmokeB.emitter.params.texturePath = "Resources/Textures/white1x1.png";
		pecSmokeB.emitter.params.emitRate = 55.0f;
		pecSmokeB.emitter.params.shape = Engine::EmissionShape::Sphere;
		pecSmokeB.emitter.params.shapeRadius = 0.15f;
		pecSmokeB.emitter.params.lifeTime = 1.4f;
		pecSmokeB.emitter.params.lifeTimeVariance = 0.3f;
		pecSmokeB.emitter.params.startVelocity = {0.0f, 0.3f, 0.0f};
		pecSmokeB.emitter.params.velocityVariance = {0.2f, 0.1f, 0.2f};
		pecSmokeB.emitter.params.acceleration = {0.0f, 0.5f, 0.0f};
		pecSmokeB.emitter.params.damping = 1.0f;
		pecSmokeB.emitter.params.startColor = {0.85f, 0.9f, 0.95f, 0.7f};
		pecSmokeB.emitter.params.endColor = {0.6f, 0.65f, 0.7f, 0.0f};
		pecSmokeB.emitter.params.startSize = {0.6f, 0.6f, 0.6f};
		pecSmokeB.emitter.params.endSize = {2.2f, 2.2f, 2.2f};
		pecSmokeB.emitter.params.isAdditive = false;

		pecSmokeB.emitter.Initialize(*renderer, "MissileTrailSmokeB");
		pecSmokeB.isInitialized = true;
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

	auto DetachVfx = [&](entt::entity vfx, float delay) {
		if (registry.valid(vfx)) {
			if (registry.all_of<ParticleEmitterComponent>(vfx)) {
				ParticleEmitterComponent& pec = registry.get<ParticleEmitterComponent>(vfx);
				pec.emitter.isPlaying = false;
			}

			if (!registry.all_of<ScriptComponent>(vfx)) {
				ScriptComponent& scriptComponent = registry.emplace<ScriptComponent>(vfx);
				scriptComponent.scripts.push_back({"BulletScript", "", nullptr});
			}

			VariableComponent& variableComponent = registry.get_or_emplace<VariableComponent>(vfx);
			variableComponent.SetValue("Speed", 0.0f);
			variableComponent.SetValue("MaxLifeTime", delay);
		}
	};

	lifeTime_ += dt;
	flightTime_ += dt;

	if (lifeTime_ >= maxLifeTime_) {
		DetachVfx(engineFlameVfx_, 0.5f);
		DetachVfx(trailSmokeVfxA_, 1.5f);
		DetachVfx(trailSmokeVfxB_, 1.5f);
		scene->DestroyObject(static_cast<uint32_t>(entity));
		return;
	}

	bool canUseTarget = false;

	if (registry.valid(target_)) {
		if (registry.all_of<TransformComponent>(target_)) {
			canUseTarget = true;
		}
	}

	float targetX = 0.0f;
	float targetY = 0.0f;
	float targetZ = 0.0f;

	if (canUseTarget) {
		TransformComponent& targetTransform = registry.get<TransformComponent>(target_);

		targetX = targetTransform.translate.x;
		targetY = targetTransform.translate.y;
		targetZ = targetTransform.translate.z;

		lastTargetPosition_.x = targetX;
		lastTargetPosition_.y = targetY;
		lastTargetPosition_.z = targetZ;
		hasLastTargetPosition_ = true;
	} else {
		hasTarget_ = false;

		if (!hasLastTargetPosition_) {
			DetachVfx(engineFlameVfx_, 0.5f);
			DetachVfx(trailSmokeVfxA_, 1.5f);
			DetachVfx(trailSmokeVfxB_, 1.5f);
			scene->DestroyObject(static_cast<uint32_t>(entity));
			return;
		}

		targetX = lastTargetPosition_.x;
		targetY = lastTargetPosition_.y;
		targetZ = lastTargetPosition_.z;
	}

	float t = flightTime_ / maxFlightTime_;

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

	float hitDistance = 1.5f;

	float hitDiffX = targetX - missileTransform.translate.x;
	float hitDiffY = targetY - missileTransform.translate.y;
	float hitDiffZ = targetZ - missileTransform.translate.z;

	float hitDistanceLength = std::sqrt(hitDiffX * hitDiffX + hitDiffY * hitDiffY + hitDiffZ * hitDiffZ);

	if (hitDistanceLength <= hitDistance) {
		CreateExplosionAttackArea(entity, scene);

		DetachVfx(engineFlameVfx_, 0.5f);
		DetachVfx(trailSmokeVfxA_, 1.5f);
		DetachVfx(trailSmokeVfxB_, 1.5f);

		scene->DestroyObject(static_cast<uint32_t>(entity));
		return;
	}

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

	if (registry.valid(engineFlameVfx_) || registry.valid(trailSmokeVfxA_) || registry.valid(trailSmokeVfxB_)) {
		DirectX::XMFLOAT3 bulletPos = missileTransform.translate;
		float pitch = missileTransform.rotate.x;
		float yaw = missileTransform.rotate.y;

		float cosP = std::cos(pitch);
		float sinP = std::sin(pitch);
		float sinY = std::sin(yaw);
		float cosY = std::cos(yaw);

		DirectX::XMFLOAT3 backDir = {-sinY * cosP, sinP, -cosY * cosP};
		DirectX::XMFLOAT3 rightVec = {cosY, 0.0f, -sinY};
		DirectX::XMFLOAT3 upVec = {sinY * sinP, cosP, cosY * sinP};

		float offsetBack = 1.0f;
		DirectX::XMFLOAT3 flamePos = {bulletPos.x + backDir.x * offsetBack, bulletPos.y + backDir.y * offsetBack, bulletPos.z + backDir.z * offsetBack};

		if (registry.valid(engineFlameVfx_) && registry.all_of<ParticleEmitterComponent>(engineFlameVfx_)) {
			TransformComponent& flameTransform = registry.get<TransformComponent>(engineFlameVfx_);
			flameTransform.translate = flamePos;
			flameTransform.rotate = missileTransform.rotate;

			ParticleEmitterComponent& flameEmitter = registry.get<ParticleEmitterComponent>(engineFlameVfx_);
			flameEmitter.emitter.params.position = {flamePos.x, flamePos.y, flamePos.z};
			flameEmitter.emitter.params.startVelocity = {backDir.x * 15.0f, backDir.y * 15.0f, backDir.z * 15.0f};
		}

		float angle = flightTime_ * 22.0f;
		float radius = 0.55f;

		DirectX::XMFLOAT3 smokePosA = {
		    bulletPos.x + backDir.x * 0.6f + (rightVec.x * std::cos(angle) + upVec.x * std::sin(angle)) * radius,
		    bulletPos.y + backDir.y * 0.6f + (rightVec.y * std::cos(angle) + upVec.y * std::sin(angle)) * radius,
		    bulletPos.z + backDir.z * 0.6f + (rightVec.z * std::cos(angle) + upVec.z * std::sin(angle)) * radius};

		DirectX::XMFLOAT3 smokePosB = {
		    bulletPos.x + backDir.x * 0.6f - (rightVec.x * std::cos(angle) + upVec.x * std::sin(angle)) * radius,
		    bulletPos.y + backDir.y * 0.6f - (rightVec.y * std::cos(angle) + upVec.y * std::sin(angle)) * radius,
		    bulletPos.z + backDir.z * 0.6f - (rightVec.z * std::cos(angle) + upVec.z * std::sin(angle)) * radius};

		if (registry.valid(trailSmokeVfxA_) && registry.all_of<ParticleEmitterComponent>(trailSmokeVfxA_)) {
			TransformComponent& smokeTransformA = registry.get<TransformComponent>(trailSmokeVfxA_);
			smokeTransformA.translate = smokePosA;

			ParticleEmitterComponent& smokeEmitterA = registry.get<ParticleEmitterComponent>(trailSmokeVfxA_);
			smokeEmitterA.emitter.params.position = {smokePosA.x, smokePosA.y, smokePosA.z};
		}

		if (registry.valid(trailSmokeVfxB_) && registry.all_of<ParticleEmitterComponent>(trailSmokeVfxB_)) {
			TransformComponent& smokeTransformB = registry.get<TransformComponent>(trailSmokeVfxB_);
			smokeTransformB.translate = smokePosB;

			ParticleEmitterComponent& smokeEmitterB = registry.get<ParticleEmitterComponent>(trailSmokeVfxB_);
			smokeEmitterB.emitter.params.position = {smokePosB.x, smokePosB.y, smokePosB.z};
		}
	}

	if (flightTime_ >= maxFlightTime_) {
		flightTime_ = maxFlightTime_;
		CreateExplosionAttackArea(entity, scene);

		DetachVfx(engineFlameVfx_, 0.5f);
		DetachVfx(trailSmokeVfxA_, 1.5f);
		DetachVfx(trailSmokeVfxB_, 1.5f);

		scene->DestroyObject(static_cast<uint32_t>(entity));
		return;
	}
}
void MissileBulletScript::CreateExplosionAttackArea(entt::entity entity, GameScene* scene) {
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

	const TransformComponent& missileTransform = registry.get<TransformComponent>(entity);

	entt::entity explosionAttackArea = registry.create();

	Engine::Renderer* renderer = scene->GetRenderer();

	TagComponent& explosionTag = registry.emplace<TagComponent>(explosionAttackArea);
	explosionTag.tag = TagType::Bullet;

	TransformComponent& explosionTransform = registry.get_or_emplace<TransformComponent>(explosionAttackArea);
	explosionTransform.translate = missileTransform.translate;
	explosionTransform.rotate = {0.0f, 0.0f, 0.0f};
	explosionTransform.scale = {explosionRadius_, explosionRadius_, explosionRadius_};

	ScriptComponent& explosionScript = registry.emplace<ScriptComponent>(explosionAttackArea);
	explosionScript.scripts.push_back({"ExplosionAttackArea", "", nullptr});

	SetVar(explosionAttackArea, scene, "Damage", damage_);
	SetVar(explosionAttackArea, scene, "ExplosionRadius", explosionRadius_);

	// ==================== 大迫力爆発エフェクト (Impact VFX) ====================
	if (renderer) {
		DirectX::XMFLOAT3 impPos = missileTransform.translate;

		// 1. 中心白発光 (Core Flash) - 一瞬で広がり、オレンジに変わりつつ消える
		entt::entity coreFlash = scene->CreateEntity("MissileCoreFlash_VFX");
		scene->SetTag(coreFlash, TagType::VFX);
		auto& cfTrans = registry.get<TransformComponent>(coreFlash);
		cfTrans.translate = impPos;

		auto& pecCF = registry.emplace<ParticleEmitterComponent>(coreFlash);
		pecCF.emitter.params.name = "MissileCoreFlash";
		pecCF.emitter.params.texturePath = "Resources/Textures/particles/diamond_flare.png";
		pecCF.emitter.params.emitRate = 0.0f;
		pecCF.emitter.params.shape = Engine::EmissionShape::Sphere;
		pecCF.emitter.params.shapeRadius = 1.0f;
		pecCF.emitter.params.lifeTime = 0.35f;
		pecCF.emitter.params.lifeTimeVariance = 0.1f;
		pecCF.emitter.params.startVelocity = {0.0f, 0.0f, 0.0f};
		pecCF.emitter.params.velocityVariance = {1.5f, 1.5f, 1.5f};
		pecCF.emitter.params.startColor = {2.0f, 2.0f, 2.0f, 1.0f}; // 白く強烈に発光
		pecCF.emitter.params.endColor = {1.0f, 0.35f, 0.0f, 0.0f};  // オレンジフェード
		pecCF.emitter.params.startSize = {4.5f, 4.5f, 4.5f};
		pecCF.emitter.params.endSize = {0.5f, 0.5f, 0.5f};
		pecCF.emitter.params.isAdditive = true;

		// ★パーティクル放出位置を着弾座標に設定
		pecCF.emitter.params.position = {impPos.x, impPos.y, impPos.z};

		pecCF.emitter.Initialize(*renderer, "MissileCoreFlash");
		pecCF.isInitialized = true;
		pecCF.emitter.EmitBurst(6);

		// スクリプトと寿命
		auto& scCF = registry.emplace<ScriptComponent>(coreFlash);
		scCF.scripts.push_back({"BulletScript", "", nullptr});
		auto& vcCF = registry.emplace<VariableComponent>(coreFlash);
		vcCF.SetValue("Speed", 0.0f);
		vcCF.SetValue("MaxLifeTime", 0.6f);

		// 2. 周囲に渦巻く赤い炎 (Vortex Fire) - 回転しながら広がる爆風
		entt::entity vortexFire = scene->CreateEntity("MissileVortexFire_VFX");
		scene->SetTag(vortexFire, TagType::VFX);
		auto& vfTrans = registry.get<TransformComponent>(vortexFire);
		vfTrans.translate = impPos;

		auto& pecVF = registry.emplace<ParticleEmitterComponent>(vortexFire);
		pecVF.emitter.params.name = "MissileVortexFire";
		pecVF.emitter.params.texturePath = "Resources/Textures/particles/diamond_flare.png";
		pecVF.emitter.params.emitRate = 0.0f;
		pecVF.emitter.params.shape = Engine::EmissionShape::Sphere;
		pecVF.emitter.params.shapeRadius = explosionRadius_ * 0.3f;
		pecVF.emitter.params.lifeTime = 0.75f;
		pecVF.emitter.params.lifeTimeVariance = 0.25f;
		pecVF.emitter.params.startVelocity = {0.0f, 2.0f, 0.0f};
		pecVF.emitter.params.velocityVariance = {11.0f, 9.0f, 11.0f}; // 全方位に勢いよく
		pecVF.emitter.params.angularVelocity = {0.0f, 0.0f, 0.0f};
		pecVF.emitter.params.angularVelocityVariance = {5.0f, 6.0f, 5.0f}; // 渦を巻くような回転
		pecVF.emitter.params.damping = 1.8f;                               // 広がりながら急減速するリアルな爆風
		pecVF.emitter.params.startColor = {1.0f, 0.3f, 0.0f, 1.0f};        // 鮮やかな赤みの炎
		pecVF.emitter.params.endColor = {0.15f, 0.0f, 0.0f, 0.0f};         // 燃え尽きて暗赤へ
		pecVF.emitter.params.startSize = {1.4f, 1.4f, 1.4f};
		pecVF.emitter.params.endSize = {4.2f, 4.2f, 4.2f};
		pecVF.emitter.params.isAdditive = true;

		// ★パーティクル放出位置を着弾座標に設定
		pecVF.emitter.params.position = {impPos.x, impPos.y, impPos.z};

		pecVF.emitter.Initialize(*renderer, "MissileVortexFire");
		pecVF.isInitialized = true;
		pecVF.emitter.EmitBurst(25);

		auto& scVF = registry.emplace<ScriptComponent>(vortexFire);
		scVF.scripts.push_back({"BulletScript", "", nullptr});
		auto& vcVF = registry.emplace<VariableComponent>(vortexFire);
		vcVF.SetValue("Speed", 0.0f);
		vcVF.SetValue("MaxLifeTime", 1.2f);

		// 4. リング状の衝撃波（歪みエフェクト）
		entt::entity shockwave = scene->CreateEntity("MissileShockwave_VFX");
		scene->SetTag(shockwave, TagType::VFX);
		auto& swTrans = registry.get<TransformComponent>(shockwave);
		swTrans.translate = impPos;
		swTrans.translate.y += 0.3f; // 地面と干渉しないようわずかに上に
		swTrans.scale = {1.0f, 1.0f, 1.0f};

		auto& swMrc = registry.emplace<MeshRendererComponent>(shockwave);
		swMrc.shaderName = "Distortion";
		swMrc.texturePath = "Resources/Textures/normal.png";
		swMrc.modelPath = "Resources/Models/plane.obj";
		swMrc.modelHandle = renderer->LoadObjMesh(swMrc.modelPath);
		swMrc.textureHandle = renderer->LoadTexture2D(swMrc.texturePath);
		swMrc.color = {1.0f, 1.0f, 1.0f, 2.5f}; // 歪み強度強め

		auto& swSc = registry.emplace<ScriptComponent>(shockwave);
		swSc.scripts.push_back({"HitDistortionScript", "", std::make_shared<HitDistortionScript>(), false});
		auto& swVc = registry.emplace<VariableComponent>(shockwave);
		swVc.SetValue("Duration", 0.5f); // 0.5秒で高速伝播
		swVc.SetValue("StartScale", 0.5f);
		swVc.SetValue("EndScale", explosionRadius_ * 2.8f); // 爆発半径に合わせて超巨大化
		swVc.SetValue("InitialAlpha", 2.6f);
	}
}
void MissileBulletScript::OnDestroy(entt::entity entity, GameScene* scene) {
	(void)entity;
	(void)scene;
	if (!scene) {
		return;
	}

	

	
}

REGISTER_SCRIPT(MissileBulletScript);

} // namespace Game