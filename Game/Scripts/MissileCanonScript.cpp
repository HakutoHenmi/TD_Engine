#include "MissileCanonScript.h"
#include "ObjectTypes.h"
#include "Scenes/GameScene.h"
#include "ScriptEngine.h"
#include "ScriptUtils.h"
#include "HitDistortionScript.h"
#include <cmath>
#include <unordered_set>
#include <vector>

#ifdef USE_IMGUI
#include "../../externals/imgui/imgui.h"
#endif

namespace Game {

static bool IsConnectedSphere(entt::registry& registry, entt::entity entityA, entt::entity entityB, float connectRange) {
	if (!registry.valid(entityA) || !registry.valid(entityB)) return false;
	if (!registry.all_of<TransformComponent>(entityA) || !registry.all_of<TransformComponent>(entityB)) return false;

	const TransformComponent& transformA = registry.get<TransformComponent>(entityA);
	const TransformComponent& transformB = registry.get<TransformComponent>(entityB);

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

static void CollectConnectedCanons(
    entt::registry& registry, entt::entity currentPipe, std::unordered_set<entt::entity>& visitedPipes, std::unordered_set<entt::entity>& foundCanons, const std::vector<entt::entity>& allPipes,
    const std::vector<entt::entity>& allCanons, float connectRange) {
	visitedPipes.insert(currentPipe);

	for (entt::entity canon : allCanons) {
		if (IsConnectedSphere(registry, currentPipe, canon, connectRange)) {
			foundCanons.insert(canon);
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

		CollectConnectedCanons(registry, otherPipe, visitedPipes, foundCanons, allPipes, allCanons, connectRange);
	}
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
}

void MissileCanonScript::Update(entt::entity entity, GameScene* scene, float dt) {
	if (!scene) {
		return;
	}

	entt::registry& registry = scene->GetRegistry();

	if (!registry.valid(entity)) {
		return;
	}

	connectionCheckTimer_ -= dt;
	if (connectionCheckTimer_ <= 0.0f) {
		connectionCheckTimer_ = 2.0f; // ★最適化: チェック間隔を0.5秒から2.0秒に延長してCPUスパイクを劇的削減
		UpdateConnection(entity, scene);
	}

	float powerRate = 0.0f;

	if (connectedCanonCount > 0) {
		powerRate = static_cast<float>(connectedTankCount) / static_cast<float>(connectedCanonCount);
	}

	float currentAttackInterval = attackInterval_;

	if (powerRate > 0.0f) {
		currentAttackInterval = attackInterval_ / powerRate;
	}

	if (attackTimer_ > 0.0f) {
		attackTimer_ -= dt;
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

	if (!isConnectedToTank_) {
		return;
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
				pec.emitter.params.position = { muzzlePos.x, muzzlePos.y, muzzlePos.z };
			}
		}

		if (registry.valid(persistentSmokeVfx_) && registry.all_of<TransformComponent>(persistentSmokeVfx_)) {
			auto& smTrans = registry.get<TransformComponent>(persistentSmokeVfx_);
			smTrans.translate = canonTransform.translate;
			smTrans.translate.y -= 0.2f;
			if (registry.all_of<ParticleEmitterComponent>(persistentSmokeVfx_)) {
				auto& pecSmoke = registry.get<ParticleEmitterComponent>(persistentSmokeVfx_);
				pecSmoke.emitter.params.position = { smTrans.translate.x, smTrans.translate.y, smTrans.translate.z };
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

	if (gameManagerEntity != entt::null) {
		attackPowerRateMisile = GetVar(gameManagerEntity, scene, "AttackPowerRateMisile", 1.0f);
		attackAreaRateMisile = GetVar(gameManagerEntity, scene, "AttackAreaRateMisile", 1.0f);
	}

	float finalDamage = damage_ * attackPowerRateMisile;
	float finalExplosionRadius = explosionRadius_ * attackAreaRateMisile;
	SetVar(entity, scene, "AttackRange", attackRange_);

	currentTarget_ = entt::null;
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

	canonTransform.rotate.y = std::atan2(toTargetX, toTargetZ);
	canonTransform.rotate.x = -std::atan2(toTargetY, distanceXZ);

	if (attackTimer_ > 0.0f) {
		return;
	}

	entt::entity bullet = registry.create();

	TagComponent& bulletTag = registry.emplace<TagComponent>(bullet);
	bulletTag.tag = TagType::Bullet;

	TransformComponent& bulletTransform = registry.emplace<TransformComponent>(bullet);
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
		bulletMeshRenderer.modelHandle = renderer->LoadObjMesh("Resources/Models/cube/cube.obj");
		bulletMeshRenderer.textureHandle = renderer->LoadTexture2D("Resources/Textures/white1x1.png");
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
void MissileCanonScript::OnDestroy(entt::entity /*entity*/, GameScene* /*scene*/) {}

void MissileCanonScript::OnEditorUI() {
#if defined(USE_IMGUI) && !defined(NDEBUG)
	ImGui::DragFloat("Attack Range", &attackRange_, 0.1f, 1.0f, 100.0f);
	ImGui::DragFloat("Attack Interval", &attackInterval_, 0.01f, 0.1f, 10.0f);
	ImGui::DragFloat("Damage", &damage_, 1.0f, 1.0f, 500.0f);
	ImGui::DragFloat("Explosion Radius", &explosionRadius_, 0.1f, 0.1f, 50.0f);

	ImGui::Separator();
	ImGui::Text("Connected Tanks: %d", connectedTankCount);
	ImGui::Text("Connected Canons: %d", connectedCanonCount);

	if (isConnectedToTank_) {
		ImGui::Text("Connected to Tank: YES");
	} else {
		ImGui::Text("Connected to Tank: NO");
	}
#endif
}

void MissileCanonScript::UpdateConnection(entt::entity entity, GameScene* scene) {
	if (!scene) {
		return;
	}

	entt::registry& registry = scene->GetRegistry();
	float connectRange = 2.5f;

	const std::vector<entt::entity>& allPipes = scene->GetEntitiesByTag(TagType::Pipe);
	const std::vector<entt::entity>& allTanks = scene->GetEntitiesByTag(TagType::BulletTank);
	const std::vector<entt::entity>& allCanons = scene->GetEntitiesByTag(TagType::Canon);

	std::unordered_set<entt::entity> foundTanks;
	std::unordered_set<entt::entity> visitedPipesForTanks;

	for (entt::entity pipe : allPipes) {
		if (!IsConnectedSphere(registry, entity, pipe, connectRange)) {
			continue;
		}

		CollectConnectedBulletTanks(registry, pipe, visitedPipesForTanks, foundTanks, allPipes, allTanks, connectRange);
	}

	connectedTankCount = static_cast<int>(foundTanks.size());

	if (connectedTankCount > 0) {
		isConnectedToTank_ = true;
	} else {
		isConnectedToTank_ = false;
	}

	std::unordered_set<entt::entity> foundCanons;
	std::unordered_set<entt::entity> visitedPipesForCanons;

	for (entt::entity pipe : allPipes) {
		if (!IsConnectedSphere(registry, entity, pipe, connectRange)) {
			continue;
		}

		CollectConnectedCanons(registry, pipe, visitedPipesForCanons, foundCanons, allPipes, allCanons, connectRange);
	}

	foundCanons.insert(entity);
	connectedCanonCount = static_cast<int>(foundCanons.size());
}

void MissileCanonScript::Debug(bool /*connected*/) {}

// ★追加: 永続VFXの初期化（1回だけ呼ばれる）
void MissileCanonScript::CreatePersistentVFX(entt::entity entity, GameScene* scene) {
	if (!scene || persistentVfxCreated_) return;
	auto& registry = scene->GetRegistry();
	Engine::Renderer* renderer = scene->GetRenderer();
	if (!renderer) return;

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
	pec.emitter.params.position = { muzzlePos.x, muzzlePos.y, muzzlePos.z };
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
	pecSmoke.emitter.params.position = { smTrans.translate.x, smTrans.translate.y, smTrans.translate.z };
	pecSmoke.emitter.Initialize(*renderer, "MissileIdleSmoke");
	pecSmoke.isInitialized = true;

	persistentVfxCreated_ = true;
}

REGISTER_SCRIPT(MissileCanonScript);

} // namespace Game