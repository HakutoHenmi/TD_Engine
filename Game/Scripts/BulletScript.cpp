#include "BulletScript.h"
#include "ObjectTypes.h"
#include "Scenes/GameScene.h"
#include "ScriptEngine.h"
#include "ScriptUtils.h"
#include <cmath>

namespace Game {

static void SpawnExplosion(entt::registry& registry, GameScene* scene, const TransformComponent& posTrans) {
	if (!scene)
		return;

	entt::entity explosionVfx = scene->CreateEntity("CanonExplosion_VFX");
	scene->SetTag(explosionVfx, TagType::VFX);

	auto& vfxTrans = registry.get<TransformComponent>(explosionVfx);
	vfxTrans.translate = posTrans.translate;

	// 1. 火花（きらめくテクスチャを使用）
	auto& pec = registry.emplace<ParticleEmitterComponent>(explosionVfx);
	pec.emitter.params.name = "ImpactExplosion";
	pec.emitter.params.texturePath = "Resources/Textures/particles/diamond_flare.png";
	pec.emitter.params.emitRate = 0.0f;
	pec.emitter.params.shape = Engine::EmissionShape::Sphere;
	pec.emitter.params.shapeRadius = 0.5f;
	pec.emitter.params.startVelocity = {0.0f, 6.0f, 0.0f};
	pec.emitter.params.velocityVariance = {4.0f, 4.0f, 4.0f};
	pec.emitter.params.acceleration = {0.0f, -9.8f, 0.0f}; // 重力落下
	pec.emitter.params.startColor = {1.0f, 0.8f, 0.3f, 1.0f};
	pec.emitter.params.endColor = {1.0f, 0.2f, 0.0f, 0.0f};
	pec.emitter.params.startSize = {0.4f, 0.4f, 0.4f};
	pec.emitter.params.endSize = {0.05f, 0.05f, 0.05f};
	pec.emitter.params.lifeTime = 0.6f;
	pec.emitter.params.lifeTimeVariance = 0.2f;
	pec.emitter.params.damping = 1.0f;
	pec.emitter.params.isAdditive = true;

	// 明示的に初期化し、その場でバースト放出！
	pec.emitter.Initialize(*scene->GetRenderer(), "ImpactExplosion_Emitter");
	pec.isInitialized = true;
	pec.emitter.EmitBurst(12);

	// 2. 煙（白煙）
	entt::entity smokeVfx = scene->CreateEntity("CanonExplosion_Smoke_VFX");
	scene->SetTag(smokeVfx, TagType::VFX);
	auto& sTrans = registry.get<TransformComponent>(smokeVfx);
	sTrans.translate = posTrans.translate;

	auto& spec = registry.emplace<ParticleEmitterComponent>(smokeVfx);
	spec.emitter.params.name = "ImpactSmoke";
	spec.emitter.params.texturePath = "Resources/Textures/white1x1.png";
	spec.emitter.params.emitRate = 0.0f;
	spec.emitter.params.shape = Engine::EmissionShape::Sphere;
	spec.emitter.params.shapeRadius = 0.8f;
	spec.emitter.params.startVelocity = {0.0f, 2.0f, 0.0f};
	spec.emitter.params.velocityVariance = {1.5f, 1.0f, 1.5f};
	spec.emitter.params.startColor = {0.4f, 0.4f, 0.4f, 0.15f};
	spec.emitter.params.endColor = {0.2f, 0.2f, 0.2f, 0.0f};
	spec.emitter.params.startSize = {0.8f, 0.8f, 0.8f};
	spec.emitter.params.endSize = {1.8f, 1.8f, 1.8f};
	spec.emitter.params.lifeTime = 0.8f;
	spec.emitter.params.lifeTimeVariance = 0.3f;
	spec.emitter.params.damping = 1.5f;
	spec.emitter.params.isAdditive = false;

	// 明示的に初期化し、その場でバースト放出！
	spec.emitter.Initialize(*scene->GetRenderer(), "ImpactSmoke_Emitter");
	spec.isInitialized = true;
	spec.emitter.EmitBurst(8);

	// スクリプト登録
	auto& sc = registry.emplace<ScriptComponent>(explosionVfx);
	sc.scripts.push_back({"BulletScript", "", nullptr});
	auto& vc = registry.emplace<VariableComponent>(explosionVfx);
	vc.SetValue("Speed", 0.0f);
	vc.SetValue("MaxLifeTime", 1.0f); // パーティクルが消え終わるまでオブジェクトを生かしておく

	auto& sSc = registry.emplace<ScriptComponent>(smokeVfx);
	sSc.scripts.push_back({"BulletScript", "", nullptr});
	auto& sVc = registry.emplace<VariableComponent>(smokeVfx);
	sVc.SetValue("Speed", 0.0f);
	sVc.SetValue("MaxLifeTime", 1.5f);

	// 地面が安っぽく光るのを完全に防ぐため、光源強度と半径を極小に制限
	auto& pointLight = registry.emplace<PointLightComponent>(explosionVfx);
	pointLight.color = {1.0f, 0.6f, 0.2f};
	pointLight.intensity = 1.5f;
	pointLight.range = 3.0f;
	pointLight.atten = {1.0f, 0.8f, 0.2f};
}

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

		float moveLen = std::sqrt(moveX * moveX + moveY * moveY + moveZ * moveZ);
		
		// ★直進弾のすり抜け防止: 敵の弾（EnemyBullet）の場合、直進線分とプレイヤー・コア・防衛設備の簡易衝突判定を行う
		if (registry.all_of<TagComponent>(entity) && registry.get<TagComponent>(entity).tag == TagType::EnemyBullet) {
			Engine::Vector3 lineA = {bulletTransform.translate.x, bulletTransform.translate.y, bulletTransform.translate.z};
			Engine::Vector3 lineB = {bulletTransform.translate.x + moveX, bulletTransform.translate.y + moveY, bulletTransform.translate.z + moveZ};
			Engine::Vector3 vecAB = {moveX, moveY, moveZ};
			float lenAB2 = moveLen * moveLen;

			if (lenAB2 > 0.0001f) {
				TagType targetTags[] = { TagType::Player, TagType::Core, TagType::Defender, TagType::Canon, TagType::Cannon, TagType::IceCanon, TagType::PipeCannon, TagType::Poison, TagType::Missile };
				bool hit = false;
				entt::entity hitTarget = entt::null;

				for (int tagIdx = 0; tagIdx < 9; ++tagIdx) {
					const auto& targets = scene->GetEntitiesByTag(targetTags[tagIdx]);
					for (auto tar : targets) {
						if (!registry.valid(tar) || !registry.all_of<TransformComponent>(tar)) continue;
						
						if (registry.all_of<HealthComponent>(tar)) {
							if (registry.get<HealthComponent>(tar).isDead || registry.get<HealthComponent>(tar).hp <= 0.0f) continue;
						}

						auto& tarTc = registry.get<TransformComponent>(tar);
						Engine::Vector3 center = {tarTc.translate.x, tarTc.translate.y, tarTc.translate.z};
						
						float radius = 1.2f;
						if (targetTags[tagIdx] == TagType::Core) {
							radius = 2.2f; 
						} else if (targetTags[tagIdx] == TagType::Player) {
							radius = 1.0f;
						}

						Engine::Vector3 vecAC = {center.x - lineA.x, center.y - lineA.y, center.z - lineA.z};
						float t = (vecAC.x * vecAB.x + vecAC.y * vecAB.y + vecAC.z * vecAB.z) / lenAB2;
						if (t < 0.0f) t = 0.0f;
						if (t > 1.0f) t = 1.0f;

						Engine::Vector3 closestP = {lineA.x + t * vecAB.x, lineA.y + t * vecAB.y, lineA.z + t * vecAB.z};
						float distSq = (closestP.x - center.x) * (closestP.x - center.x) + 
						               (closestP.y - center.y) * (closestP.y - center.y) + 
						               (closestP.z - center.z) * (closestP.z - center.z);

						if (distSq <= radius * radius) {
							hit = true;
							hitTarget = tar;
							bulletTransform.translate = { closestP.x, closestP.y, closestP.z }; // 衝突位置へ補正
							break;
						}
					}
					if (hit) break;
				}

				if (hit) {
					if (registry.all_of<HealthComponent>(hitTarget)) {
						auto& hc = registry.get<HealthComponent>(hitTarget);
						if (hc.invincibleTime <= 0.0f) {
							float damage = 10.0f;
							if (registry.all_of<HitboxComponent>(entity)) {
								damage = registry.get<HitboxComponent>(entity).damage;
							}
							hc.hp -= damage;
							hc.invincibleTime = 0.5f;
							hc.hitFlashTimer = 0.2f;
						}
					}
					if (registry.all_of<HitboxComponent>(entity)) {
						registry.get<HitboxComponent>(entity).isActive = false;
						registry.get<HitboxComponent>(entity).enabled = false;
					}
					SpawnExplosion(registry, scene, bulletTransform);
					scene->DestroyObject(static_cast<uint32_t>(entity));
					return;
				}
			}
		}

		// ★追加: レイキャストによる地形・オブジェクトとの衝突判定
		Engine::Vector3 rayOrig = {bulletTransform.translate.x, bulletTransform.translate.y, bulletTransform.translate.z};
		Engine::Vector3 rayDir = {moveX, moveY, moveZ};

		if (moveLen > 0.0001f) {
			rayDir.x /= moveLen;
			rayDir.y /= moveLen;
			rayDir.z /= moveLen;
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
				if (registry.all_of<TagComponent>(entity) && registry.get<TagComponent>(entity).tag == TagType::Bullet) {
					SpawnExplosion(registry, scene, bulletTransform);
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

		if (registry.all_of<TagComponent>(entity) && registry.get<TagComponent>(entity).tag == TagType::Bullet) {

			SpawnExplosion(registry, scene, bulletTransform);
		}

		scene->DestroyObject(static_cast<uint32_t>(entity));

		return;
	}

	directionX /= length;
	directionY /= length;
	directionZ /= length;

	float moveAmount = speed_ * dt;

	// ★すり抜け防止: 次の移動でターゲットを通り越す場合は、移動量を残り距離に制限する
	if (moveAmount >= length) {
		moveAmount = length;
	}

	// ★着弾判定: ターゲットのサイズに応じた適切な接近閾値を設定（コアは大きいため2.0m、プレイヤーは1.0m）
	float arrivalThreshold = 0.8f;
	if (registry.all_of<TagComponent>(target_)) {
		TagType tTag = registry.get<TagComponent>(target_).tag;
		if (tTag == TagType::Core) {
			arrivalThreshold = 2.0f; 
		} else if (tTag == TagType::Player) {
			arrivalThreshold = 1.0f;
		}
	}

	if (length <= arrivalThreshold || length <= moveAmount) {
		// 着弾した瞬間の位置を調整（コアにめり込みすぎるのを防ぐため、少し手前にクランプ）
		if (length > arrivalThreshold) {
			bulletTransform.translate.x += directionX * (length - arrivalThreshold);
			bulletTransform.translate.y += directionY * (length - arrivalThreshold);
			bulletTransform.translate.z += directionZ * (length - arrivalThreshold);
		}

		// 二重ダメージ防止のため、当たり判定を即座に無効化
		if (registry.all_of<HitboxComponent>(entity)) {
			registry.get<HitboxComponent>(entity).isActive = false;
			registry.get<HitboxComponent>(entity).enabled = false;
		}

		// 直接ダメージ処理を適用（コア等へのダメージと被弾フラッシュ演出を確実に発生させる）
		if (registry.all_of<HealthComponent>(target_)) {
			auto& hc = registry.get<HealthComponent>(target_);
			if (hc.invincibleTime <= 0.0f) {
				float damage = 10.0f;
				if (registry.all_of<HitboxComponent>(entity)) {
					damage = registry.get<HitboxComponent>(entity).damage;
				}
				hc.hp -= damage;
				hc.invincibleTime = 0.5f;
				hc.hitFlashTimer = 0.2f; // コアが被弾時に赤くフラッシュする演出
			}
		}

		// 爆発エフェクトを生成（味方の弾・敵の弾どちらでもエフェクトを出す）
		if (registry.all_of<TagComponent>(entity)) {
			TagType aTag = registry.get<TagComponent>(entity).tag;
			if (aTag == TagType::Bullet || aTag == TagType::EnemyBullet) {
				SpawnExplosion(registry, scene, bulletTransform);
			}
		}

		scene->DestroyObject(static_cast<uint32_t>(entity));
		return;
	}

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
		if (registry.all_of<TagComponent>(entity) && registry.get<TagComponent>(entity).tag == TagType::Bullet) {
			SpawnExplosion(registry, scene, bulletTransform);
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