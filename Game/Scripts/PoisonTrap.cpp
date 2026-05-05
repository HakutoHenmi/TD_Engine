#include "PoisonTrap.h"
#include "ObjectTypes.h"
#include "Scenes/GameScene.h"
#include "ScriptEngine.h"
#include <algorithm>
#include <cmath>
#include <unordered_set>
#include <vector>
#include "ScriptEngine.h"
#include "ScriptUtils.h"
#ifdef USE_IMGUI
#include "../../externals/imgui/imgui.h"
#endif

namespace Game {

static bool HasTag(entt::registry& registry, entt::entity entity, TagType tagName) {
	if (!registry.valid(entity)) {
		return false;
	}

	if (!registry.all_of<TagComponent>(entity)) {
		return false;
	}

	return registry.get<TagComponent>(entity).tag == tagName;
}

static bool IsConnectedSphere(entt::registry& registry, entt::entity a, entt::entity b, float connectRange) {
	if (!registry.valid(a)) {
		return false;
	}

	if (!registry.valid(b)) {
		return false;
	}

	if (!registry.all_of<TransformComponent>(a)) {
		return false;
	}

	if (!registry.all_of<TransformComponent>(b)) {
		return false;
	}

	const TransformComponent& transformA = registry.get<TransformComponent>(a);
	const TransformComponent& transformB = registry.get<TransformComponent>(b);

	float diffX = transformB.translate.x - transformA.translate.x;
	float diffY = transformB.translate.y - transformA.translate.y;
	float diffZ = transformB.translate.z - transformA.translate.z;

	float distance3D = std::sqrt(diffX * diffX + diffY * diffY + diffZ * diffZ);

	if (distance3D <= connectRange) {
		return true;
	}

	float distanceXZ = std::sqrt(diffX * diffX + diffZ * diffZ);
	float heightDifference = std::abs(diffY);

	if (heightDifference >= 0.1f) {
		if (distanceXZ <= connectRange) {
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

void PoisonTrap::Start(entt::entity /*entity*/, GameScene* /*scene*/) { poisonActiveTimer_ = 0.0f; }

void PoisonTrap::Update(entt::entity entity, GameScene* scene, float dt) {
	if (!scene) {
		return;
	}

	entt::registry& registry = scene->GetRegistry();

	if (!registry.valid(entity)) {
		return;
	}

	// 接続チェック
	connectionCheckTimer_ -= dt;
	if (connectionCheckTimer_ <= 0.0f) {
		connectionCheckTimer_ = 0.5f;
		UpdateConnection(entity, scene);
	}

	if (!isConnectedToTank_) {
		return;
	}

	if (!IsEnemyInRange(entity, scene, poisonRange_)) {
		return;
	}

	//--------------------------------
	// ① 毒を出している時間（青ゲージ）
	//--------------------------------
	if (poisonActiveTimer_ > 0.0f) {
		poisonActiveTimer_ -= dt;

		float rate = poisonActiveTimer_ / poisonActiveTime_;

		if (rate < 0.0f) {
			rate = 0.0f;
		}

		SetVar(entity, scene, "PoisonGaugeRate", rate);
		SetVar(entity, scene, "PoisonGaugeState", 1.0f); // 青

		// 毒終了 → クールダウンへ
		if (poisonActiveTimer_ <= 0.0f) {
			poisonCoolTimer_ = poisonCoolTime_;
		}

		return;
	}

	//--------------------------------
	// ② クールダウン（グレー）
	//--------------------------------
	if (poisonCoolTimer_ > 0.0f) {
		poisonCoolTimer_ -= dt;

		float rate = poisonCoolTimer_ / poisonCoolTime_;

		if (rate < 0.0f) {
			rate = 0.0f;
		}

		SetVar(entity, scene, "PoisonGaugeRate", rate);
		SetVar(entity, scene, "PoisonGaugeState", 2.0f); // グレー

		return;
	}

	//--------------------------------
	// ③ 発射（ここで毒スタート）
	//--------------------------------

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

	if (gm != entt::null) {
		skillPowerRate_ = GetVar(gm, scene, "AttackPowerRatePoison", 1.0f);
		skillRangeRate_ = GetVar(gm, scene, "AttackRangeRatePoison", 1.0f);
	}

	float finalDamage = poisonDamage_ * skillPowerRate_;
	float finalRange = poisonRange_ * skillRangeRate_;

	CreatePoisonAttackArea(entity, scene, finalDamage, finalRange);

	// タイマー開始
	poisonActiveTimer_ = poisonActiveTime_;

	SetVar(entity, scene, "PoisonGaugeRate", 1.0f);
	SetVar(entity, scene, "PoisonGaugeState", 1.0f);
}

void PoisonTrap::OnDestroy(entt::entity /*entity*/, GameScene* /*scene*/) {}

void PoisonTrap::OnEditorUI() {

}

void PoisonTrap::UpdateConnection(entt::entity entity, GameScene* scene) {
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
#pragma region HelperFunctions
bool PoisonTrap::IsEnemyInRange(entt::entity entity, GameScene* scene, float range) {
	if (!scene) {
		return false;
	}

	entt::registry& registry = scene->GetRegistry();

	if (!registry.valid(entity)) {
		return false;
	}

	if (!registry.all_of<TransformComponent>(entity)) {
		return false;
	}

	const TransformComponent& trapTransform = registry.get<TransformComponent>(entity);
	const std::vector<entt::entity>& enemies = scene->GetEntitiesByTag(TagType::Enemy);

	for (entt::entity enemy : enemies) {
		if (!registry.valid(enemy)) {
			continue;
		}

		if (!registry.all_of<TransformComponent>(enemy)) {
			continue;
		}

		const TransformComponent& enemyTransform = registry.get<TransformComponent>(enemy);

		float diffX = enemyTransform.translate.x - trapTransform.translate.x;
		float diffY = enemyTransform.translate.y - trapTransform.translate.y;
		float diffZ = enemyTransform.translate.z - trapTransform.translate.z;

		float distance = std::sqrt(diffX * diffX + diffY * diffY + diffZ * diffZ);

		if (distance <= range) {
			return true;
		}
	}

	return false;
}

#pragma endregion

void PoisonTrap::CreatePoisonAttackArea(entt::entity entity, GameScene* scene, float damage, float range) {
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

	const TransformComponent& trapTransform = registry.get<TransformComponent>(entity);

	entt::entity poisonAttackArea = registry.create();
	auto* renderer = scene->GetRenderer();
	if (renderer) {
		MeshRendererComponent& poisonMeshRenderer = registry.emplace<MeshRendererComponent>(poisonAttackArea);
		poisonMeshRenderer.modelHandle = renderer->LoadObjMesh("Resources/Models/cube/cube.obj");
		poisonMeshRenderer.textureHandle = renderer->LoadTexture2D("Resources/Textures/white1x1.png");
	}
	TagComponent& poisonTag = registry.emplace<TagComponent>(poisonAttackArea);
	poisonTag.tag = TagType::Poison;

	TransformComponent& poisonTransform = registry.emplace<TransformComponent>(poisonAttackArea);
	poisonTransform.translate = trapTransform.translate;
	poisonTransform.rotate = trapTransform.rotate;
	poisonTransform.scale = {range / 2.0f, range / 2.0f, range / 2.0f};

	HitboxComponent& poisonHitbox = registry.emplace<HitboxComponent>(poisonAttackArea);
	poisonHitbox.isActive = true;
	poisonHitbox.damage = damage;
	poisonHitbox.tag = TagType::Poison;
	poisonHitbox.size = {range, range, range};

	ScriptComponent& poisonScript = registry.emplace<ScriptComponent>(poisonAttackArea);
	poisonScript.scripts.push_back({"PoisonAttackArea", "", nullptr});
}

void PoisonTrap::Debug(bool /*connected*/) {
	// 以前はここで ImGui::Begin を呼んでいたが、Update からの呼び出しは危険なため廃止。
	// 代わりに OnEditorUI を使用する。
}

REGISTER_SCRIPT(PoisonTrap);

} // namespace Game