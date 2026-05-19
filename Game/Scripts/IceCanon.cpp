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

	if (!isConnectedToTank_) {
		return;
	}
	if (!registry.all_of<TransformComponent>(entity)) {
		return;
	}

	TransformComponent& canonTransform = registry.get<TransformComponent>(entity);

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

	attackTimer_ = currentAttackInterval;
}

void IceCanon::OnDestroy(entt::entity /*entity*/, GameScene* /*scene*/) {}

REGISTER_SCRIPT(IceCanon);

} // namespace Game