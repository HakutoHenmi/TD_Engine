#include "MissileCanonScript.h"
#include "HitDistortionScript.h"
#include "ObjectTypes.h"
#include "Scenes/GameScene.h"
#include "ScriptEngine.h"
#include "ScriptUtils.h"
#include "TutorialScript.h"
#include <cmath>
#include <unordered_set>
#include <vector>

#ifdef USE_IMGUI
#include "../../externals/imgui/imgui.h"
#endif

namespace Game {

static float LerpAngle(float current, float target, float speed, float dt) {
	float diff = target - current;

	while (diff > 3.14159265f) {
		diff -= 6.28318530f;
	}

	while (diff < -3.14159265f) {
		diff += 6.28318530f;
	}

	current += diff * speed * dt;

	return current;
}

void MissileCanonScript::Start(entt::entity entity, GameScene* scene) {
	attackTimer_ = 0.0f;
	idleSparkTimer_ = 0.0f;
	idleDistortionTimer_ = 0.0f;
	auto& registry = scene->GetRegistry();

	if (!registry.all_of<HealthComponent>(entity)) {
		auto& hc = registry.emplace<HealthComponent>(entity);
		hc.hp = 100.0f;
		hc.maxHp = 100.0f;
	}
	if (!registry.all_of<HurtboxComponent>(entity)) {
		auto& hurtbox = registry.emplace<HurtboxComponent>(entity);
		hurtbox.size = {2.0f, 2.0f, 2.0f};
	}
	if (!registry.all_of<WorldSpaceUIComponent>(entity)) {
		registry.emplace<WorldSpaceUIComponent>(entity);
	}
	if (!registry.all_of<BuffComponent>(entity)) {
		registry.emplace<BuffComponent>(entity);
	}
	CreateBase(entity, scene);
}

// update
void MissileCanonScript::Update(entt::entity entity, GameScene* scene, float dt) {
	if (!scene) {
		return;
	}

	entt::registry& registry = scene->GetRegistry();

	if (!registry.valid(entity)) {
		return;
	}
	UpdateBase(entity, scene);

	attackTimer_ -= dt;

	if (attackTimer_ < 0.0f) {
		attackTimer_ = 0.0f;
	}

	if (!registry.all_of<TransformComponent>(entity)) {
		return;
	}

	TransformComponent& canonTransform = registry.get<TransformComponent>(entity);

	// --- 待機時エフェクト (Idle VFX) --- ★最適化: 永続エミッター方式
	// エンティティを毎フレーム生成せず、1回だけ作成して位置を更新する
	{
		if (!persistentVfxCreated_) {
			CreatePersistentVFX(entity, scene);
		}

		// 永続エミッターの位置を毎フレーム更新
		float launchHeightOffset = 0.8f;
		DirectX::XMFLOAT3 muzzlePos = canonTransform.translate;
		muzzlePos.y += launchHeightOffset;

		if (registry.valid(persistentSparkVfx_) && registry.all_of<TransformComponent>(persistentSparkVfx_)) {
			auto& sTrans = registry.get<TransformComponent>(persistentSparkVfx_);
			sTrans.translate = muzzlePos;
			if (registry.all_of<ParticleEmitterComponent>(persistentSparkVfx_)) {
				auto& pec = registry.get<ParticleEmitterComponent>(persistentSparkVfx_);
				pec.emitter.params.position = {muzzlePos.x, muzzlePos.y, muzzlePos.z};
			}
		}

		if (registry.valid(persistentSmokeVfx_) && registry.all_of<TransformComponent>(persistentSmokeVfx_)) {
			auto& smTrans = registry.get<TransformComponent>(persistentSmokeVfx_);
			smTrans.translate = canonTransform.translate;
			smTrans.translate.y -= 0.2f;
			if (registry.all_of<ParticleEmitterComponent>(persistentSmokeVfx_)) {
				auto& pecSmoke = registry.get<ParticleEmitterComponent>(persistentSmokeVfx_);
				pecSmoke.emitter.params.position = {smTrans.translate.x, smTrans.translate.y, smTrans.translate.z};
			}
		}
	}

	entt::entity gameManagerEntity = entt::null;

	auto scriptView = registry.view<ScriptComponent>();
	for (entt::entity checkEntity : scriptView) {
		const ScriptComponent& scriptComponent = scriptView.get<ScriptComponent>(checkEntity);

		for (const auto& scriptInstance : scriptComponent.scripts) {
			if (scriptInstance.scriptPath == "PhaseSystemScript" || scriptInstance.scriptPath == "TutorialScript") {
				gameManagerEntity = checkEntity;
				break;
			}
		}

		if (gameManagerEntity != entt::null) {
			break;
		}
	}

	float attackPowerRateMisile = 1.0f;
	float attackAreaRateMisile = 1.0f;
	float missileCoolDownRate = 1.0f;

	if (gameManagerEntity != entt::null) {
		attackPowerRateMisile = GetVar(gameManagerEntity, scene, "AttackPowerRateMisile", 1.0f);
		attackAreaRateMisile = GetVar(gameManagerEntity, scene, "AttackAreaRateMisile", 1.0f);
		missileCoolDownRate = GetVar(gameManagerEntity, scene, "MissileCoolDownRate", 1.0f);
	}

	float currentAttackInterval = attackInterval_ * missileCoolDownRate;

	// ★追加: プレイヤーからの距離によるバフ適用
	if (registry.all_of<BuffComponent>(entity)) {
		auto& buff = registry.get<BuffComponent>(entity);
		buff.isBuffed = false;

		auto players = scene->GetEntitiesByTag(TagType::Player);
		if (!players.empty() && registry.valid(players[0])) {
			bool auraEnabled = true;
			if (auto* tutorial = TutorialScript::GetInstance(); tutorial && !tutorial->IsAuraEnabled()) {
				auraEnabled = false;
			}
			if (auraEnabled) {
				if (registry.all_of<TransformComponent>(players[0])) {
					auto& pTrans = registry.get<TransformComponent>(players[0]);
					auto& cTrans = registry.get<TransformComponent>(entity);
					float dx = pTrans.translate.x - cTrans.translate.x;
					float dy = pTrans.translate.y - cTrans.translate.y;
					float dz = pTrans.translate.z - cTrans.translate.z;
					float dist = std::sqrt(dx*dx + dy*dy + dz*dz);

					if (dist <= buff.buffRadius) {
						buff.isBuffed = true;
						// ミサイルのバフ効果：リロード速度上昇、爆発範囲アップ
						currentAttackInterval /= buff.buffMultiplier;
						attackAreaRateMisile *= 1.3f;
					}
				}
			}
		}
	}

	float missileGaugeRate = 1.0f - (attackTimer_ / currentAttackInterval);

	if (missileGaugeRate < 0.0f) {
		missileGaugeRate = 0.0f;
	}

	if (missileGaugeRate > 1.0f) {
		missileGaugeRate = 1.0f;
	}

	float missileGaugeState = 2.0f;

	if (attackTimer_ <= 0.0f) {
		missileGaugeState = 1.0f;
	}

	SetVar(entity, scene, "MissileGaugeRate", missileGaugeRate);
	SetVar(entity, scene, "MissileGaugeState", missileGaugeState);

	float finalDamage = damage_ * attackPowerRateMisile;
	float finalExplosionRadius = explosionRadius_ * attackAreaRateMisile;
	SetVar(entity, scene, "AttackRange", attackRange_);

	// ターゲットの更新と攻撃処理
	UpdateTarget(registry, scene, canonTransform);

	if (currentTarget_ == entt::null) {
		return;
	}

	const TransformComponent& targetTransform = registry.get<TransformComponent>(currentTarget_);

	float toTargetX = targetTransform.translate.x - canonTransform.translate.x;
	float toTargetY = targetTransform.translate.y - canonTransform.translate.y;
	float toTargetZ = targetTransform.translate.z - canonTransform.translate.z;

	float distanceXZ = std::sqrt(toTargetX * toTargetX + toTargetZ * toTargetZ);

	if (distanceXZ <= 0.0001f) {
		return;
	}

	float targetYaw = std::atan2(toTargetX, toTargetZ);
	float targetPitch = -std::atan2(toTargetY, distanceXZ);

	float rotateSmoothSpeed = 6.0f;

	canonTransform.rotate.y = LerpAngle(canonTransform.rotate.y, targetYaw, rotateSmoothSpeed, dt);

	canonTransform.rotate.x = LerpAngle(canonTransform.rotate.x, targetPitch, rotateSmoothSpeed, dt);

	if (attackTimer_ > 0.0f) {
		return;
	}

	// 攻撃
	FireMissile(entity, registry, scene, canonTransform, finalDamage, finalExplosionRadius, currentAttackInterval);
}

void MissileCanonScript::OnDestroy(entt::entity entity, GameScene* scene) {
	(void)entity;
	if (!scene) {
		return;
	}

	entt::registry& registry = scene->GetRegistry();

	if (registry.valid(baseEntity_)) {
		registry.destroy(baseEntity_);
	}

	if (registry.valid(persistentSparkVfx_)) {
		registry.destroy(persistentSparkVfx_);
	}

	if (registry.valid(persistentSmokeVfx_)) {
		registry.destroy(persistentSmokeVfx_);
	}
}

void MissileCanonScript::OnEditorUI() {
#if defined(USE_IMGUI) && !defined(NDEBUG)
	ImGui::DragFloat("Attack Range", &attackRange_, 0.1f, 1.0f, 100.0f);
	ImGui::DragFloat("Attack Interval", &attackInterval_, 0.01f, 0.1f, 10.0f);
	ImGui::DragFloat("Damage", &damage_, 1.0f, 1.0f, 500.0f);
	ImGui::DragFloat("Explosion Radius", &explosionRadius_, 0.1f, 0.1f, 50.0f);

	ImGui::Separator();

#endif
}

void MissileCanonScript::Debug(bool /*connected*/) {}

void MissileCanonScript::CreateBase(entt::entity entity, GameScene* scene) {
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

	// updatebase
	// UpdateBase(entity, scene);

	const TransformComponent& canonTransform = registry.get<TransformComponent>(entity);

	baseEntity_ = scene->CreateEntity("MissileCanonBase");
	scene->SetTag(baseEntity_, TagType::Canon);

	TransformComponent& baseTransform = registry.get<TransformComponent>(baseEntity_);
	baseTransform.translate = canonTransform.translate;
	baseTransform.translate.y -= 0.6f;
	baseTransform.rotate = {0.0f, 0.0f, 0.0f};
	baseTransform.scale = {1.0f, 1.0f, 1.0};

	Engine::Renderer* renderer = scene->GetRenderer();
	if (renderer) {
		MeshRendererComponent& meshRenderer = registry.emplace<MeshRendererComponent>(baseEntity_);
		meshRenderer.modelHandle = renderer->LoadObjMesh("Resources/Models/Misiilebase/MisiileBase.obj");
		meshRenderer.textureHandle = renderer->LoadTexture2D("Resources/Models/Misiilebase/MisileBase.png");
	}
}

// ★追加: 永続VFXの初期化（1回だけ呼ばれる）
void MissileCanonScript::CreatePersistentVFX(entt::entity entity, GameScene* scene) {
	if (!scene || persistentVfxCreated_)
		return;
	auto& registry = scene->GetRegistry();
	Engine::Renderer* renderer = scene->GetRenderer();
	if (!renderer)
		return;

	auto& canonTransform = registry.get<TransformComponent>(entity);
	DirectX::XMFLOAT3 muzzlePos = canonTransform.translate;
	muzzlePos.y += 0.8f;

	// 1. 永続スパークエミッター（emitRate方式: 毎フレームエンティティ生成を完全廃止）
	persistentSparkVfx_ = scene->CreateEntity("MissileIdleSpark_Persistent");
	scene->SetTag(persistentSparkVfx_, TagType::VFX);
	auto& sTrans = registry.get<TransformComponent>(persistentSparkVfx_);
	sTrans.translate = muzzlePos;

	auto& pec = registry.emplace<ParticleEmitterComponent>(persistentSparkVfx_);
	pec.emitter.params.name = "MissileIdleSpark";
	pec.emitter.params.texturePath = "Resources/Textures/particles/diamond_flare.png";
	pec.emitter.params.emitRate = 50.0f; // ★変更: バースト方式→連続放出（8÷0.12≈67を少し控えめに）
	pec.emitter.params.shape = Engine::EmissionShape::Sphere;
	pec.emitter.params.shapeRadius = 2.0f;
	pec.emitter.params.startVelocity = {0.0f, 0.0f, 0.0f};
	pec.emitter.params.velocityVariance = {6.0f, 5.0f, 6.0f};
	pec.emitter.params.startColor = {0.3f, 0.8f, 1.0f, 3.0f};
	pec.emitter.params.endColor = {0.0f, 0.1f, 1.0f, 0.0f};
	pec.emitter.params.startSize = {0.26f, 0.26f, 0.26f};
	pec.emitter.params.endSize = {0.01f, 0.01f, 0.01f};
	pec.emitter.params.lifeTime = 0.18f;
	pec.emitter.params.lifeTimeVariance = 0.08f;
	pec.emitter.params.damping = 0.9f;
	pec.emitter.params.isAdditive = true;
	pec.emitter.params.position = {muzzlePos.x, muzzlePos.y, muzzlePos.z};
	pec.emitter.Initialize(*renderer, "MissileIdleSpark");
	pec.isInitialized = true;

	// 2. 永続煙エミッター
	persistentSmokeVfx_ = scene->CreateEntity("MissileIdleSmoke_Persistent");
	scene->SetTag(persistentSmokeVfx_, TagType::VFX);
	auto& smTrans = registry.get<TransformComponent>(persistentSmokeVfx_);
	smTrans.translate = canonTransform.translate;
	smTrans.translate.y -= 0.2f;

	auto& pecSmoke = registry.emplace<ParticleEmitterComponent>(persistentSmokeVfx_);
	pecSmoke.emitter.params.name = "MissileIdleSmoke";
	pecSmoke.emitter.params.shaderName = "ProceduralSmoke";
	pecSmoke.emitter.params.texturePath = "Resources/Textures/white1x1.png";
	pecSmoke.emitter.params.emitRate = 15.0f; // ★変更: バースト方式→連続放出（6÷0.22≈27を控えめに）
	pecSmoke.emitter.params.shape = Engine::EmissionShape::Sphere;
	pecSmoke.emitter.params.shapeRadius = 1.4f;
	pecSmoke.emitter.params.lifeTime = 1.5f;
	pecSmoke.emitter.params.lifeTimeVariance = 0.3f;
	pecSmoke.emitter.params.startVelocity = {0.0f, 2.0f, 0.0f};
	pecSmoke.emitter.params.velocityVariance = {1.0f, 0.3f, 1.0f};
	pecSmoke.emitter.params.acceleration = {0.0f, 0.8f, 0.0f};
	pecSmoke.emitter.params.damping = 1.0f;
	pecSmoke.emitter.params.isAdditive = false;
	pecSmoke.emitter.params.startColor = {0.85f, 0.88f, 0.9f, 0.7f};
	pecSmoke.emitter.params.endColor = {0.6f, 0.62f, 0.65f, 0.0f};
	pecSmoke.emitter.params.startSize = {1.2f, 1.2f, 1.2f};
	pecSmoke.emitter.params.endSize = {3.6f, 3.6f, 3.6f};
	pecSmoke.emitter.params.position = {smTrans.translate.x, smTrans.translate.y, smTrans.translate.z};
	pecSmoke.emitter.Initialize(*renderer, "MissileIdleSmoke");
	pecSmoke.isInitialized = true;

	persistentVfxCreated_ = true;
}

void MissileCanonScript::UpdateBase(entt::entity entity, GameScene* scene) {
	if (!scene) {
		return;
	}

	entt::registry& registry = scene->GetRegistry();

	if (!registry.valid(entity)) {
		return;
	}

	if (!registry.valid(baseEntity_)) {
		return;
	}

	if (!registry.all_of<TransformComponent>(entity)) {
		return;
	}

	if (!registry.all_of<TransformComponent>(baseEntity_)) {
		return;
	}

	const TransformComponent& canonTransform = registry.get<TransformComponent>(entity);
	TransformComponent& baseTransform = registry.get<TransformComponent>(baseEntity_);

	baseTransform.translate = canonTransform.translate;
	baseTransform.translate.y -= 0.6f;

	baseTransform.rotate = {0.0f, 0.0f, 0.0f};
}

void MissileCanonScript::UpdateTarget(entt::registry& registry, GameScene* scene, const TransformComponent& canonTransform) {
	if (currentTarget_ != entt::null) {

		if (!registry.valid(currentTarget_)) {
			currentTarget_ = entt::null;
		}

		if (currentTarget_ != entt::null && !registry.all_of<TransformComponent>(currentTarget_)) {

			currentTarget_ = entt::null;
		}

		if (currentTarget_ != entt::null) {

			const TransformComponent& targetTransform = registry.get<TransformComponent>(currentTarget_);

			float diffX = targetTransform.translate.x - canonTransform.translate.x;

			float diffY = targetTransform.translate.y - canonTransform.translate.y;

			float diffZ = targetTransform.translate.z - canonTransform.translate.z;

			float distance = std::sqrt(diffX * diffX + diffY * diffY + diffZ * diffZ);

			if (distance > attackRange_) {
				currentTarget_ = entt::null;
			}
		}
	}

	if (currentTarget_ == entt::null) {

		float bestDistance = attackRange_;

		const std::vector<entt::entity>& enemies = scene->GetEntitiesByTag(TagType::Enemy);

		for (entt::entity enemy : enemies) {

			if (!registry.valid(enemy)) {
				continue;
			}

			if (!registry.all_of<TransformComponent>(enemy)) {
				continue;
			}

			const TransformComponent& enemyTransform = registry.get<TransformComponent>(enemy);

			float diffX = enemyTransform.translate.x - canonTransform.translate.x;

			float diffY = enemyTransform.translate.y - canonTransform.translate.y;

			float diffZ = enemyTransform.translate.z - canonTransform.translate.z;

			float distance = std::sqrt(diffX * diffX + diffY * diffY + diffZ * diffZ);

			if (distance < bestDistance) {
				bestDistance = distance;
				currentTarget_ = enemy;
			}
		}
	}
}


void MissileCanonScript::FireMissile(entt::entity entity, entt::registry& registry, GameScene* scene, const TransformComponent& canonTransform, float finalDamage, float finalExplosionRadius, float currentAttackInterval) {
	entt::entity bullet = registry.create();

	TagComponent& bulletTag = registry.emplace<TagComponent>(bullet);

	bulletTag.tag = TagType::Bullet;

	TransformComponent& bulletTransform = registry.get_or_emplace<TransformComponent>(bullet);

	bulletTransform.translate = canonTransform.translate;

	float launchForwardOffset = 2.5f;
	float launchHeightOffset = 1.5f;

	bulletTransform.translate.x += std::sin(canonTransform.rotate.y) * launchForwardOffset;

	bulletTransform.translate.y += launchHeightOffset;

	bulletTransform.translate.z += std::cos(canonTransform.rotate.y) * launchForwardOffset;

	bulletTransform.rotate = canonTransform.rotate;
	bulletTransform.rotate.x = -0.8f;

	bulletTransform.scale = {0.5f, 0.5f, 1.2f};

	Engine::Renderer* renderer = scene->GetRenderer();

	if (renderer) {

		MeshRendererComponent& bulletMeshRenderer = registry.emplace<MeshRendererComponent>(bullet);

		bulletMeshRenderer.modelHandle = renderer->LoadObjMesh("Resources/MisiileBullet/MisiileBullet.obj");

		bulletMeshRenderer.textureHandle = renderer->LoadTexture2D("Resources/MisiileBullet/MisiileBullet.png");
	}

	ScriptComponent& bulletScriptComponent = registry.emplace<ScriptComponent>(bullet);

	bulletScriptComponent.scripts.push_back({"MissileBulletScript", "", nullptr});

	SetVar(bullet, scene, "HasTarget", 1.0f);

	SetVar(bullet, scene, "TargetEntity", static_cast<float>(static_cast<uint32_t>(currentTarget_)));

	SetVar(bullet, scene, "Damage", finalDamage);

	SetVar(bullet, scene, "ExplosionRadius", finalExplosionRadius);

	SetVar(entity, scene, "MissileGaugeRate", 0.0f);

	SetVar(entity, scene, "MissileGaugeState", 2.0f);

	attackTimer_ = currentAttackInterval;
}
REGISTER_SCRIPT(MissileCanonScript);

} // namespace Game