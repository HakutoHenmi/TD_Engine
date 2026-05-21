#include "IceCanon.h"
#include "ObjectTypes.h"
#include "Scenes/GameScene.h"
#include "ScriptEngine.h"
#include "ScriptUtils.h"

#include <cmath>
#include <unordered_set>
#include <vector>
namespace Game {
static bool IsConnectedSphere(entt::registry& registry, entt::entity a, entt::entity b, float connectRange) {
	if (!registry.valid(a) || !registry.valid(b)) {
		return false;
	}

	if (!registry.all_of<TransformComponent>(a) || !registry.all_of<TransformComponent>(b)) {
		return false;
	}

	const TransformComponent& transformA = registry.get<TransformComponent>(a);
	const TransformComponent& transformB = registry.get<TransformComponent>(b);

	float diffX = transformB.translate.x - transformA.translate.x;
	float diffY = transformB.translate.y - transformA.translate.y;
	float diffZ = transformB.translate.z - transformA.translate.z;

	float connectRangeSq = connectRange * connectRange;
	float dist3DSq = diffX * diffX + diffY * diffY + diffZ * diffZ;

	if (dist3DSq <= connectRangeSq) {
		return true;
	}

	float distXZSq = diffX * diffX + diffZ * diffZ;
	float heightDifference = std::abs(diffY);

	if (heightDifference >= 0.1f) {
		if (distXZSq <= connectRangeSq) {
			return true;
		}
	}

	return false;
}
static void CollectConnectedBulletTanks(
    entt::registry& registry, entt::entity currentPipe, std::unordered_set<entt::entity>& visitedPipes, std::unordered_set<entt::entity>& foundTanks, const std::vector<entt::entity>& allPipes,
    const std::vector<entt::entity>& allTanks, float connectRange) {

	visitedPipes.insert(currentPipe);

	for (entt::entity tank : allTanks) {
		if (IsConnectedSphere(registry, currentPipe, tank, connectRange)) {
			foundTanks.insert(tank);
		}
	}

	for (entt::entity otherPipe : allPipes) {
		if (otherPipe == currentPipe) {
			continue;
		}

		if (visitedPipes.count(otherPipe) > 0) {
			continue;
		}

		if (!IsConnectedSphere(registry, currentPipe, otherPipe, connectRange)) {
			continue;
		}

		CollectConnectedBulletTanks(registry, otherPipe, visitedPipes, foundTanks, allPipes, allTanks, connectRange);
	}
}
void IceCanon::UpdateConnection(entt::entity entity, GameScene* scene) {
	if (!scene) {
		return;
	}

	entt::registry& registry = scene->GetRegistry();
	float connectRange = 3.0f;

	const std::vector<entt::entity>& allPipes = scene->GetEntitiesByTag(TagType::Pipe);
	const std::vector<entt::entity>& allTanks = scene->GetEntitiesByTag(TagType::BulletTank);

	std::unordered_set<entt::entity> foundTanks;
	std::unordered_set<entt::entity> visitedPipesForTanks;

	for (entt::entity pipe : allPipes) {
		if (!IsConnectedSphere(registry, entity, pipe, connectRange)) {
			continue;
		}

		CollectConnectedBulletTanks(registry, pipe, visitedPipesForTanks, foundTanks, allPipes, allTanks, connectRange);
	}

	connectedTankCount_ = static_cast<int>(foundTanks.size());

	if (connectedTankCount_ > 0) {
		isConnectedToTank_ = true;
	} else {
		isConnectedToTank_ = false;
	}
}




void IceCanon::Start(entt::entity entity, GameScene* scene) {
	attackTimer_ = 0.0f;
	persistentVfxCreated_ = false;
	vfxDelayTimer_ = 0.2f; // プレハブ初期座標からの瞬間移動によるパーティクルの軌跡を防ぐため遅延
	persistentMistVfx_ = entt::null;
	persistentCrystalVfx_ = entt::null;

	auto& registry = scene->GetRegistry();
	
	// 霜の降りたビジュアル表現：マテリアルカラーを青白くブレンド
	if (registry.all_of<MeshRendererComponent>(entity)) {
		auto& mr = registry.get<MeshRendererComponent>(entity);
		mr.color = { 0.65f, 0.85f, 1.0f, 1.0f };
	}

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
}

void IceCanon::Update(entt::entity entity, GameScene* scene, float dt) {
	if (!scene) {
		return;
	}

	entt::registry& registry = scene->GetRegistry();

	if (!registry.valid(entity)) {
		return;
	}
	connectionCheckTimer_ -= dt;
	if (connectionCheckTimer_ <= 0.0f) {
		connectionCheckTimer_ = 2.0f;
		UpdateConnection(entity, scene);
	}

	if (!registry.all_of<TransformComponent>(entity)) {
		return;
	}

	TransformComponent& canonTransform = registry.get<TransformComponent>(entity);

	// --- 待機時エフェクト (Idle VFX) ---
	{
		if (!persistentVfxCreated_) {
			vfxDelayTimer_ -= dt;
			if (vfxDelayTimer_ <= 0.0f) {
				CreatePersistentVFX(entity, scene);
			}
		} else {
			// パイプが接続されている時のみ冷気を発生させる
			float targetMistEmitRate = isConnectedToTank_ ? 22.0f : 0.0f;
			float targetCrystalEmitRate = isConnectedToTank_ ? 12.0f : 0.0f;

			if (registry.valid(persistentMistVfx_) && registry.all_of<ParticleEmitterComponent>(persistentMistVfx_)) {
				auto& pec = registry.get<ParticleEmitterComponent>(persistentMistVfx_);
				pec.emitter.params.emitRate = targetMistEmitRate;
			}
			if (registry.valid(persistentCrystalVfx_) && registry.all_of<ParticleEmitterComponent>(persistentCrystalVfx_)) {
				auto& pec = registry.get<ParticleEmitterComponent>(persistentCrystalVfx_);
				pec.emitter.params.emitRate = targetCrystalEmitRate;
			}
		}
	}

	if (!isConnectedToTank_) {
		return;
	}

	attackTimer_ -= dt;
	if (attackTimer_ > 0.0f) {
		return;
	}

	entt::entity gm = entt::null;
	auto viewScript = registry.view<ScriptComponent>();

	for (entt::entity e : viewScript) {
		const ScriptComponent& sc = viewScript.get<ScriptComponent>(e);

		for (const auto& instance : sc.scripts) {
			if (instance.scriptPath == "PhaseSystemScript" || instance.scriptPath == "TutorialScript") {
				gm = e;
				break;
			}
		}

		if (gm != entt::null) {
			break;
		}
	}

	float attackPowerRate = 1.0f;
	float attackRangeRate = 1.0f;
	float attackSpeedRate = 1.0f;
	float stopTimeRate = 1.0f;
	float bulletCountRate = 1.0f;

	if (gm != entt::null) {
		attackPowerRate = GetVar(gm, scene, "AttackPowerRateIceCanon", 1.0f);
		attackRangeRate = GetVar(gm, scene, "AttackRangeRateIceCanon", 1.0f);
		attackSpeedRate = GetVar(gm, scene, "AttackSpeedRateIceCanon", 1.0f);
		stopTimeRate = GetVar(gm, scene, "StopTimeRateIceCanon", 1.0f);
		bulletCountRate = GetVar(gm, scene, "BulletCountRateIceCanon", 1.0f);
	}

	float currentDamage = damage_ * attackPowerRate;
	float currentAttackRange = attackRange_ * attackRangeRate;
	float currentAttackInterval = attackInterval_ / attackSpeedRate;
	float currentStopTime = stopTime_ * stopTimeRate;
	int bulletCount = static_cast<int>(6.0f * bulletCountRate);

	if (bulletCount < 1) {
		bulletCount = 1;
	}

	entt::entity target = entt::null;
	float bestDistance = currentAttackRange;
	SetVar(entity, scene, "AttackRange", currentAttackRange);

	const std::vector<entt::entity>& enemies = scene->GetEntitiesByTag(TagType::Enemy);

	for (entt::entity other : enemies) {
		if (!registry.valid(other)) {
			continue;
		}


		if (!isConnectedToTank_) {
			return;
		}
		if (!registry.all_of<TransformComponent>(other)) {
			continue;
		}

		TransformComponent& enemyTransform = registry.get<TransformComponent>(other);

		float dx = enemyTransform.translate.x - canonTransform.translate.x;
		float dy = enemyTransform.translate.y - canonTransform.translate.y;
		float dz = enemyTransform.translate.z - canonTransform.translate.z;

		float distance = std::sqrt(dx * dx + dy * dy + dz * dz);

		if (distance < bestDistance) {
			bestDistance = distance;
			target = other;
		}
	}

	if (target == entt::null) {
		return;
	}

	TransformComponent& targetTransform = registry.get<TransformComponent>(target);

	float toX = targetTransform.translate.x - canonTransform.translate.x;
	float toZ = targetTransform.translate.z - canonTransform.translate.z;

	float yaw = std::atan2(toX, toZ);
	canonTransform.rotate.y = yaw;

	for (int i = 0; i < bulletCount; i++) {
		entt::entity bullet = registry.create();

		TagComponent& bulletTag = registry.emplace<TagComponent>(bullet);
		bulletTag.tag = TagType::Bullet;

		TransformComponent& bulletTransform = registry.emplace<TransformComponent>(bullet);
		bulletTransform.translate = canonTransform.translate;
		bulletTransform.translate.y += 1.0f;

		float angle = (6.283185f / static_cast<float>(bulletCount)) * static_cast<float>(i);
		float flowerRadius = 0.6f;

		bulletTransform.translate.x += std::cos(angle) * flowerRadius;
		bulletTransform.translate.z += std::sin(angle) * flowerRadius;

		float directionX = std::cos(angle);
		float directionZ = std::sin(angle);

		bulletTransform.rotate = canonTransform.rotate;
		bulletTransform.rotate.y = std::atan2(directionX, directionZ);
		bulletTransform.rotate.x = -0.35f;

		bulletTransform.scale = {0.3f, 0.3f, 0.3f};

		Engine::Renderer* renderer = scene->GetRenderer();
		if (renderer) {
			MeshRendererComponent& mesh = registry.emplace<MeshRendererComponent>(bullet);
			mesh.modelHandle = renderer->LoadObjMesh("Resources/Models/cube/cube.obj");
			mesh.textureHandle = renderer->LoadTexture2D("Resources/Textures/white1x1.png");
		}

		HitboxComponent& hitbox = registry.emplace<HitboxComponent>(bullet);
		hitbox.isActive = true;
		hitbox.damage = currentDamage;
		hitbox.tag = TagType::Bullet;
		hitbox.size = {0.5f, 0.5f, 0.5f};

		ScriptComponent& sc = registry.emplace<ScriptComponent>(bullet);
		sc.scripts.push_back({"IceBulletScript", "", nullptr});

		SetVar(bullet, scene, "HasTarget", 1.0f);
		SetVar(bullet, scene, "TargetEntity", (float)(uint32_t)target);
		SetVar(bullet, scene, "StopTime", currentStopTime);
	}

	// --- 発射時エフェクト (Muzzle VFX: 氷の破片を伴う青白い冷気のブラスト) ---
	{
		Engine::Renderer* renderer = scene->GetRenderer();
		if (renderer) {
			float fireYaw = canonTransform.rotate.y;
			float firePitch = -0.35f;
			float fireCosX = std::cos(firePitch);
			float fireSinX = std::sin(firePitch);
			DirectX::XMFLOAT3 fireDir = {
				std::sin(fireYaw) * fireCosX,
				-fireSinX,
				std::cos(fireYaw) * fireCosX
			};

			DirectX::XMFLOAT3 muzzlePos = canonTransform.translate;
			muzzlePos.y += 1.0f;
			float offset = 1.0f;
			muzzlePos.x += fireDir.x * offset;
			muzzlePos.y += fireDir.y * offset;
			muzzlePos.z += fireDir.z * offset;

			// 1. 青白い冷気のブラスト (吹雪)
			entt::entity blastVfx = scene->CreateEntity("IceBlast_VFX");
			scene->SetTag(blastVfx, TagType::VFX);
			auto& bTrans = registry.get<TransformComponent>(blastVfx);
			bTrans.translate = muzzlePos;

			auto& pecBlast = registry.emplace<ParticleEmitterComponent>(blastVfx);
			pecBlast.emitter.params.name = "IceBlast";
			pecBlast.emitter.params.texturePath = "Resources/Textures/white1x1.png";
			pecBlast.emitter.params.emitRate = 0.0f;
			pecBlast.emitter.params.shape = Engine::EmissionShape::Cone;
			pecBlast.emitter.params.shapeRadius = 0.2f;
			pecBlast.emitter.params.shapeAngle = 0.45f;
			pecBlast.emitter.params.lifeTime = 0.5f;
			pecBlast.emitter.params.lifeTimeVariance = 0.15f;
			pecBlast.emitter.params.startVelocity = { fireDir.x * 12.0f, fireDir.y * 12.0f, fireDir.z * 12.0f };
			pecBlast.emitter.params.velocityVariance = { 3.0f, 3.0f, 3.0f };
			pecBlast.emitter.params.damping = 0.4f;
			pecBlast.emitter.params.startColor = { 0.5f, 0.8f, 1.0f, 0.8f };
			pecBlast.emitter.params.endColor = { 0.8f, 0.95f, 1.0f, 0.0f };
			pecBlast.emitter.params.startSize = { 0.5f, 0.5f, 0.5f };
			pecBlast.emitter.params.endSize = { 2.4f, 2.4f, 2.4f };
			pecBlast.emitter.params.isAdditive = true;
			pecBlast.emitter.params.position = { muzzlePos.x, muzzlePos.y, muzzlePos.z };

			pecBlast.emitter.Initialize(*renderer, "IceBlast_Emitter");
			pecBlast.isInitialized = true;
			pecBlast.emitter.EmitBurst(25);

			// スクリプトと寿命
			auto& scBlast = registry.emplace<ScriptComponent>(blastVfx);
			scBlast.scripts.push_back({ "BulletScript", "", nullptr });
			auto& vcBlast = registry.emplace<VariableComponent>(blastVfx);
			vcBlast.SetValue("Speed", 0.0f);
			vcBlast.SetValue("MaxLifeTime", 0.8f);

			// 2. 鋭い氷の破片 (きらめくスパーク)
			entt::entity shardVfx = scene->CreateEntity("IceShard_VFX");
			scene->SetTag(shardVfx, TagType::VFX);
			auto& sTrans = registry.get<TransformComponent>(shardVfx);
			sTrans.translate = muzzlePos;

			auto& pecShard = registry.emplace<ParticleEmitterComponent>(shardVfx);
			pecShard.emitter.params.name = "IceShard";
			pecShard.emitter.params.texturePath = "Resources/Textures/particles/diamond_flare.png";
			pecShard.emitter.params.emitRate = 0.0f;
			pecShard.emitter.params.shape = Engine::EmissionShape::Cone;
			pecShard.emitter.params.shapeRadius = 0.1f;
			pecShard.emitter.params.shapeAngle = 0.3f;
			pecShard.emitter.params.lifeTime = 0.7f;
			pecShard.emitter.params.lifeTimeVariance = 0.2f;
			pecShard.emitter.params.startVelocity = { fireDir.x * 15.0f, fireDir.y * 15.0f, fireDir.z * 15.0f };
			pecShard.emitter.params.velocityVariance = { 4.0f, 4.0f, 4.0f };
			pecShard.emitter.params.damping = 0.6f;
			pecShard.emitter.params.startColor = { 0.8f, 0.95f, 1.0f, 1.8f };
			pecShard.emitter.params.endColor = { 0.3f, 0.7f, 1.0f, 0.0f };
			pecShard.emitter.params.startSize = { 0.25f, 0.25f, 0.25f };
			pecShard.emitter.params.endSize = { 0.03f, 0.03f, 0.03f };
			pecShard.emitter.params.isAdditive = true;
			pecShard.emitter.params.position = { muzzlePos.x, muzzlePos.y, muzzlePos.z };

			pecShard.emitter.Initialize(*renderer, "IceShard_Emitter");
			pecShard.isInitialized = true;
			pecShard.emitter.EmitBurst(15);

			// スクリプトと寿命
			auto& scShard = registry.emplace<ScriptComponent>(shardVfx);
			scShard.scripts.push_back({ "BulletScript", "", nullptr });
			auto& vcShard = registry.emplace<VariableComponent>(shardVfx);
			vcShard.SetValue("Speed", 0.0f);
			vcShard.SetValue("MaxLifeTime", 1.0f);
		}
	}

	attackTimer_ = currentAttackInterval;
}

void IceCanon::OnDestroy(entt::entity /*entity*/, GameScene* scene) {
	if (!scene) return;
	auto& registry = scene->GetRegistry();

	if (persistentVfxCreated_) {
		if (registry.valid(persistentMistVfx_)) {
			scene->DestroyObject(static_cast<uint32_t>(persistentMistVfx_));
			persistentMistVfx_ = entt::null;
		}
		if (registry.valid(persistentCrystalVfx_)) {
			scene->DestroyObject(static_cast<uint32_t>(persistentCrystalVfx_));
			persistentCrystalVfx_ = entt::null;
		}
		persistentVfxCreated_ = false;
	}
}

void IceCanon::CreatePersistentVFX(entt::entity entity, GameScene* scene) {
	if (!scene || persistentVfxCreated_) return;
	auto& registry = scene->GetRegistry();
	Engine::Renderer* renderer = scene->GetRenderer();
	if (!renderer) return;

	auto& canonTransform = registry.get<TransformComponent>(entity);
	DirectX::XMFLOAT3 basePos = canonTransform.translate;

	// 1. 底面に向けて青白い冷気が滝のようにゆっくり流れ落ちる霧エミッター
	persistentMistVfx_ = scene->CreateEntity("IceMist_Persistent");
	scene->SetTag(persistentMistVfx_, TagType::VFX);
	auto& mTrans = registry.get<TransformComponent>(persistentMistVfx_);
	mTrans.translate = basePos;
	mTrans.translate.y += 1.0f; // 中央から開始

	auto& pecMist = registry.emplace<ParticleEmitterComponent>(persistentMistVfx_);
	pecMist.emitter.params.name = "IceMist";
	pecMist.emitter.params.texturePath = "Resources/Textures/white1x1.png";
	pecMist.emitter.params.emitRate = 22.0f; // 途切れずに出す
	pecMist.emitter.params.shape = Engine::EmissionShape::Sphere;
	pecMist.emitter.params.shapeRadius = 1.0f;
	pecMist.emitter.params.lifeTime = 2.2f;
	pecMist.emitter.params.lifeTimeVariance = 0.5f;
	pecMist.emitter.params.startVelocity = {0.0f, -0.8f, 0.0f}; // ゆっくり下へ
	pecMist.emitter.params.velocityVariance = {0.4f, 0.15f, 0.4f};
	pecMist.emitter.params.acceleration = {0.0f, -0.4f, 0.0f};  // 下向き加速
	pecMist.emitter.params.damping = 0.1f;
	pecMist.emitter.params.startColor = {0.55f, 0.85f, 1.0f, 0.35f}; // 青白い冷気
	pecMist.emitter.params.endColor = {0.8f, 0.95f, 1.0f, 0.0f};
	pecMist.emitter.params.startSize = {0.8f, 0.8f, 0.8f};
	pecMist.emitter.params.endSize = {2.2f, 2.2f, 2.2f};
	pecMist.emitter.params.isAdditive = true;
	pecMist.emitter.params.position = { mTrans.translate.x, mTrans.translate.y, mTrans.translate.z };
	
	pecMist.emitter.Initialize(*renderer, "IceMist");
	pecMist.isInitialized = true;

	// 2. 周囲に細かな氷の結晶がキラキラ舞うエミッター
	persistentCrystalVfx_ = scene->CreateEntity("IceCrystal_Persistent");
	scene->SetTag(persistentCrystalVfx_, TagType::VFX);
	auto& cTrans = registry.get<TransformComponent>(persistentCrystalVfx_);
	cTrans.translate = basePos;
	cTrans.translate.y += 0.8f;

	auto& pecCrystal = registry.emplace<ParticleEmitterComponent>(persistentCrystalVfx_);
	pecCrystal.emitter.params.name = "IceCrystal";
	pecCrystal.emitter.params.texturePath = "Resources/Textures/particles/diamond_flare.png"; // きらめくダイヤモンド
	pecCrystal.emitter.params.emitRate = 12.0f;
	pecCrystal.emitter.params.shape = Engine::EmissionShape::Sphere;
	pecCrystal.emitter.params.shapeRadius = 1.6f;
	pecCrystal.emitter.params.lifeTime = 1.6f;
	pecCrystal.emitter.params.lifeTimeVariance = 0.3f;
	pecCrystal.emitter.params.startVelocity = {0.0f, 0.2f, 0.0f};
	pecCrystal.emitter.params.velocityVariance = {0.6f, 0.5f, 0.6f};
	pecCrystal.emitter.params.damping = 0.2f;
	pecCrystal.emitter.params.startColor = {0.7f, 0.9f, 1.0f, 1.3f};
	pecCrystal.emitter.params.endColor = {0.3f, 0.8f, 1.0f, 0.0f};
	pecCrystal.emitter.params.startSize = {0.14f, 0.14f, 0.14f};
	pecCrystal.emitter.params.endSize = {0.02f, 0.02f, 0.02f};
	pecCrystal.emitter.params.angularVelocity = {1.0f, 2.0f, 1.0f};
	pecCrystal.emitter.params.angularVelocityVariance = {2.0f, 2.0f, 2.0f};
	pecCrystal.emitter.params.isAdditive = true;
	pecCrystal.emitter.params.position = { cTrans.translate.x, cTrans.translate.y, cTrans.translate.z };

	pecCrystal.emitter.Initialize(*renderer, "IceCrystal");
	pecCrystal.isInitialized = true;

	persistentVfxCreated_ = true;
}

REGISTER_SCRIPT(IceCanon);

} // namespace Game