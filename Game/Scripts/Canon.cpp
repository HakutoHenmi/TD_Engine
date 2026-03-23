#include "Canon.h"
#include "ObjectTypes.h"
#include "Scenes/GameScene.h"
#include "ScriptEngine.h"
#include <cmath>
#include <vector>

#ifdef USE_IMGUI
#include "../../externals/imgui/imgui.h"
#endif

namespace Game {

// タグ走査
static bool HasTag(entt::registry& registry, entt::entity entity, const char* tagName) {
	if (!registry.valid(entity) || !registry.all_of<TagComponent>(entity)) return false;
	return registry.get<TagComponent>(entity).tag == tagName;
}

// 球体接続判定
static bool IsConnectedSphere(entt::registry& registry, entt::entity a, entt::entity b, float connectRange) {
	if (!registry.valid(a) || !registry.all_of<TransformComponent>(a) ||
		!registry.valid(b) || !registry.all_of<TransformComponent>(b)) return false;

	const auto& posA = registry.get<TransformComponent>(a).translate;
	const auto& posB = registry.get<TransformComponent>(b).translate;

	float dx = posB.x - posA.x;
	float dy = posB.y - posA.y;
	float dz = posB.z - posA.z;

	float dist = std::sqrt(dx * dx + dy * dy + dz * dz);

	return dist <= connectRange;
}

// すでに訪問したか
static bool IsAlreadyVisited(const std::vector<entt::entity>& visitedObjects, entt::entity entity) {
	for (auto v : visitedObjects) {
		if (v == entity) return true;
	}
	return false;
}

static bool IsPipeConnectedToBulletTankRecursive(entt::registry& registry, entt::entity currentPipe, std::vector<entt::entity>& visitedObjects, float connectRange) {
	visitedObjects.push_back(currentPipe);

	auto view = registry.view<TransformComponent>();
	for (auto other : view) {
		if (other == currentPipe) continue;
		if (!IsConnectedSphere(registry, currentPipe, other, connectRange)) continue;

		if (HasTag(registry, other, "BulletTank")) return true;

		if (HasTag(registry, other, "Pipe")) {
			if (IsAlreadyVisited(visitedObjects, other)) continue;
			if (IsPipeConnectedToBulletTankRecursive(registry, other, visitedObjects, connectRange)) {
				return true;
			}
		}
	}
	return false;
}

static bool IsCanonConnectedToBulletTank(entt::registry& registry, entt::entity canonEntity) {
	const float connectRange = 2.5f;
	
	auto view = registry.view<TransformComponent>();
	for (auto other : view) {
		if (!HasTag(registry, other, "Pipe")) continue;
		if (!IsConnectedSphere(registry, canonEntity, other, connectRange)) continue;

		std::vector<entt::entity> visitedObjects;
		if (IsPipeConnectedToBulletTankRecursive(registry, other, visitedObjects, connectRange)) {
			return true;
		}
	}
	return false;
}

void Canon::Start(entt::entity /*entity*/, GameScene* /*scene*/) {
	attackTimer_ = 0.0f;
}

void Canon::Update(entt::entity entity, GameScene* scene, float dt) {
	if (!scene || !scene->GetRegistry().valid(entity)) return;
	auto& registry = scene->GetRegistry();

	objectCount = 0;
	pipeCount = 0;
	enemyCount = 0;

	auto allView = registry.view<TransformComponent>();
	for (auto other : allView) {
		objectCount += 1;
		if (HasTag(registry, other, "Pipe")) pipeCount += 1;
		if (HasTag(registry, other, "Enemy")) enemyCount += 1;
	}

	bool connected = IsCanonConnectedToBulletTank(registry, entity);
	Debug(connected);

	if (attackTimer_ > 0.0f) attackTimer_ -= dt;
	if (!connected) return;

	// 一番近い Enemy を探す
	entt::entity target = entt::null;
	float bestDistance = attackRange_;

	if (!registry.all_of<TransformComponent>(entity)) return;
	auto& canonTc = registry.get<TransformComponent>(entity);

	auto enemyView = registry.view<TagComponent, TransformComponent>();
	for (auto other : enemyView) {
		if (enemyView.get<TagComponent>(other).tag != "Enemy") continue;

		auto& otherTc = enemyView.get<TransformComponent>(other);
		float dx = otherTc.translate.x - canonTc.translate.x;
		float dy = otherTc.translate.y - canonTc.translate.y;
		float dz = otherTc.translate.z - canonTc.translate.z;
		float distance = std::sqrt(dx * dx + dy * dy + dz * dz);

		if (distance < bestDistance) {
			bestDistance = distance;
			target = other;
		}
	}

	if (target == entt::null) return;

	auto& targetTc = registry.get<TransformComponent>(target);
	float toX = targetTc.translate.x - canonTc.translate.x;
	float toZ = targetTc.translate.z - canonTc.translate.z;

	if (std::fabs(toX) < 0.0001f && std::fabs(toZ) < 0.0001f) return;

	// 大砲を敵の方向へ向ける
	float desiredYaw = std::atan2(toX, toZ);
	float toY = targetTc.translate.y - canonTc.translate.y;
	float distXZ = std::sqrt(toX * toX + toZ * toZ);
	float desiredPitch = std::atan2(toY, distXZ);
	
	canonTc.rotate.y = desiredYaw;
	canonTc.rotate.x = -desiredPitch;

	// クールダウン中なら撃たない（向くだけ）
	if (attackTimer_ > 0.0f) {
		return;
	}

	// =========================
	// 弾を生成して撃つ (enTT)
	// =========================
	entt::entity bullet = registry.create();
	
	auto& bTag = registry.emplace<TagComponent>(bullet);
	bTag.tag = "Bullet";
	
	auto& bTc = registry.emplace<TransformComponent>(bullet);
	bTc.translate = canonTc.translate;
	bTc.translate.y += 2.0f;
	
	float muzzleOffset = 2.0f;
	bTc.translate.x += std::sin(desiredYaw) * muzzleOffset;
	bTc.translate.z += std::cos(desiredYaw) * muzzleOffset;
	bTc.rotate = canonTc.rotate;
	bTc.scale = {0.3f, 0.3f, 0.3f};

	auto* renderer = scene->GetRenderer();
	if (renderer) {
		auto& bMr = registry.emplace<MeshRendererComponent>(bullet);
		bMr.modelHandle = renderer->LoadObjMesh("Resources/cube/cube.obj");
		bMr.textureHandle = renderer->LoadTexture2D("Resources/white1x1.png");
		// SceneObjectでいうmeshRenderers[0]の代わり
	}

	auto& bHitbox = registry.emplace<HitboxComponent>(bullet);
	bHitbox.isActive = true;
	bHitbox.damage = damage_;
	bHitbox.tag = "Bullet";
	bHitbox.size = {0.3f, 0.3f, 0.3f};

	auto& bHealth = registry.emplace<HealthComponent>(bullet);
	bHealth.hp = 1.0f;
	bHealth.maxHp = 1.0f;

	auto& bScript = registry.emplace<ScriptComponent>(bullet);
	bScript.scripts.push_back({ "BulletScript", "", nullptr });

	attackTimer_ = attackInterval_;
}

void Canon::OnDestroy(entt::entity /*entity*/, GameScene* /*scene*/) {}

void Canon::OnEditorUI() {
#if defined(USE_IMGUI) && !defined(NDEBUG)
	ImGui::DragFloat("Rotate Speed", &rotationSpeed_, 0.01f, 0.1f, 10.0f);
	ImGui::DragFloat("Attack Range", &attackRange_, 0.1f, 1.0f, 100.0f);
	ImGui::DragFloat("Attack Interval", &attackInterval_, 0.01f, 0.1f, 10.0f);
	
	ImGui::Separator();
	ImGui::Text("Status (Debug)");
	ImGui::Text("Rotation: %.2f", rotationSpeed_);
#endif
}

void Canon::Debug(bool connected) {
	(void)connected;
#ifndef NDEBUG
	const char* connectedText = "NO";
	if (connected) {
		connectedText = "YES";
	}

#ifdef USE_IMGUI
	ImGui::Begin("Canon Debug");
	ImGui::Text("Objects: %d", objectCount);
	ImGui::Text("Pipes  : %d", pipeCount);
	ImGui::Text("Enemies: %d", enemyCount);
	ImGui::Text("Canon connected to tank: %s", connectedText);
	ImGui::End();
#endif
#endif
}

REGISTER_SCRIPT(Canon);

} // namespace Game