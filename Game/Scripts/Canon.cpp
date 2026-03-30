#include "Canon.h"
#include "ObjectTypes.h"
#include "Scenes/GameScene.h"
#include "ScriptEngine.h"
#include <cmath>
#include <vector>
#include <unordered_set>
#include <algorithm>

#ifdef USE_IMGUI
#include "../../externals/imgui/imgui.h"
#endif

namespace Game {

// タグ走査
static bool HasTag(entt::registry& registry, entt::entity entity, const char* tagName) {
	if (!registry.valid(entity) || !registry.all_of<TagComponent>(entity))
		return false;
	return registry.get<TagComponent>(entity).tag == tagName;
}

// 球体接続判定
static bool IsConnectedSphere(entt::registry& registry, entt::entity a, entt::entity b, float connectRange) {
	if (!registry.valid(a) || !registry.all_of<TransformComponent>(a) || !registry.valid(b) || !registry.all_of<TransformComponent>(b))
		return false;

	const auto& posA = registry.get<TransformComponent>(a).translate;
	const auto& posB = registry.get<TransformComponent>(b).translate;

	float dx = posB.x - posA.x;
	float dy = posB.y - posA.y;
	float dz = posB.z - posA.z;

	float dist = std::sqrt(dx * dx + dy * dy + dz * dz);

	return dist <= connectRange;
}

// パイプから再帰的に接続をたどって、BulletTankに繋がっているか
static bool IsPipeConnectedToBulletTankRecursive(
    entt::registry& registry,
    entt::entity currentPipe,
    std::unordered_set<entt::entity>& visitedObjects,
    const std::vector<entt::entity>& allPipes,
    const std::vector<entt::entity>& allTanks,
    float connectRange)
{
    visitedObjects.insert(currentPipe);

    // パイプのリスト内だけで検索
    for (auto other : allPipes) {
        if (other == currentPipe || visitedObjects.count(other)) continue;
        if (!IsConnectedSphere(registry, currentPipe, other, connectRange)) continue;

        if (IsPipeConnectedToBulletTankRecursive(registry, other, visitedObjects, allPipes, allTanks, connectRange)) {
            return true;
        }
    }

    // 周囲にタンクがあるか確認
    for (auto tank : allTanks) {
        if (IsConnectedSphere(registry, currentPipe, tank, connectRange)) {
            return true;
        }
    }

    return false;
}

static void CollectConnectedBulletTanks(
    entt::registry& registry,
    entt::entity currentPipe,
    std::unordered_set<entt::entity>& visitedPipes,
    std::unordered_set<entt::entity>& foundTanks,
    const std::vector<entt::entity>& allPipes,
    const std::vector<entt::entity>& allTanks,
    float connectRange)
{
    visitedPipes.insert(currentPipe);

    // 近くのタンクを探す
    for (auto tank : allTanks) {
        if (IsConnectedSphere(registry, currentPipe, tank, connectRange)) {
            foundTanks.insert(tank);
        }
    }

    // 接続されているパイプを再帰的に探索
    for (auto other : allPipes) {
        if (other == currentPipe || visitedPipes.count(other)) continue;
        if (IsConnectedSphere(registry, currentPipe, other, connectRange)) {
            CollectConnectedBulletTanks(registry, other, visitedPipes, foundTanks, allPipes, allTanks, connectRange);
        }
    }
}

static void CollectConnectedCanons(
    entt::registry& registry,
    entt::entity currentPipe,
    std::unordered_set<entt::entity>& visitedPipes,
    std::unordered_set<entt::entity>& foundCanons,
    const std::vector<entt::entity>& allPipes,
    const std::vector<entt::entity>& allCanons,
    float connectRange)
{
    visitedPipes.insert(currentPipe);

    // 近くの大砲を探す
    for (auto canon : allCanons) {
        if (IsConnectedSphere(registry, currentPipe, canon, connectRange)) {
            foundCanons.insert(canon);
        }
    }

    // 接続されているパイプを再帰的に探索
    for (auto other : allPipes) {
        if (other == currentPipe || visitedPipes.count(other)) continue;
        if (IsConnectedSphere(registry, currentPipe, other, connectRange)) {
            CollectConnectedCanons(registry, other, visitedPipes, foundCanons, allPipes, allCanons, connectRange);
        }
    }
}

void Canon::Start(entt::entity /*entity*/, GameScene* /*scene*/) { attackTimer_ = 0.0f; }

void Canon::Update(entt::entity entity, GameScene* scene, float dt) {
	if (!scene || !scene->GetRegistry().valid(entity))
		return;
	auto& registry = scene->GetRegistry();

	// 接続チェックを一定時間ごとに実行 (0.5秒おき)
	connectionCheckTimer_ -= dt;
	if (connectionCheckTimer_ <= 0.0f) {
		connectionCheckTimer_ = 0.5f;
		UpdateConnection(entity, scene);
	}

	float powerRate = 0.0f;
	if (connectedCanonCount > 0) {
		powerRate = (float)connectedTankCount / (float)connectedCanonCount;
	}

	float currentAttackInterval = attackInterval_;
	if (powerRate > 0.0f) {
		currentAttackInterval = attackInterval_ / powerRate;
	}

	Debug(isConnectedToTank_);

	if (attackTimer_ > 0.0f)
		attackTimer_ -= dt;
	if (!isConnectedToTank_)
		return;

	// 一番近い Enemy を探す
	entt::entity target = entt::null;
	float bestDistance = attackRange_;

	if (!registry.all_of<TransformComponent>(entity))
		return;
	auto& canonTc = registry.get<TransformComponent>(entity);

	// ★ 高速タグ検索を利用
	const auto& enemies = scene->GetEntitiesByTag("Enemy");
	for (auto other : enemies) {
		if (!registry.valid(other) || !registry.all_of<TransformComponent>(other))
			continue;

		auto& otherTc = registry.get<TransformComponent>(other);
		float dx = otherTc.translate.x - canonTc.translate.x;
		float dy = otherTc.translate.y - canonTc.translate.y;
		float dz = otherTc.translate.z - canonTc.translate.z;
		float distance = std::sqrt(dx * dx + dy * dy + dz * dz);

		if (distance < bestDistance) {
			bestDistance = distance;
			target = other;
		}
	}

	if (target == entt::null)
		return;

	auto& targetTc = registry.get<TransformComponent>(target);
	float toX = targetTc.translate.x - canonTc.translate.x;
	float toZ = targetTc.translate.z - canonTc.translate.z;

	if (std::fabs(toX) < 0.0001f && std::fabs(toZ) < 0.0001f)
		return;

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
	// 大砲の根本（支点）からのオフセット
	float baseHeight = 0.0f;
	bTc.translate.y += baseHeight;

	float muzzleOffset = 2.5f;
	float cosX = std::cos(canonTc.rotate.x);
	float sinX = std::sin(canonTc.rotate.x);

	bTc.translate.x += std::sin(canonTc.rotate.y) * cosX * muzzleOffset;
	bTc.translate.y += -sinX * muzzleOffset;
	bTc.translate.z += std::cos(canonTc.rotate.y) * cosX * muzzleOffset;
	bTc.rotate = canonTc.rotate;
	bTc.scale = {0.3f, 0.3f, 0.3f};

	auto* renderer = scene->GetRenderer();
	if (renderer) {
		auto& bMr = registry.emplace<MeshRendererComponent>(bullet);
		bMr.modelHandle = renderer->LoadObjMesh("Resources/Models/cube/cube.obj");
		bMr.textureHandle = renderer->LoadTexture2D("Resources/Textures/white1x1.png");
		// SceneObjectでいうmeshRenderers[0]の代わり
	}

	auto& bHitbox = registry.emplace<HitboxComponent>(bullet);
	bHitbox.isActive = true;
	bHitbox.damage = damage_;
	bHitbox.tag = "Bullet";
	bHitbox.size = {1.0f, 1.0f, 1.0f}; // スケール 0.3f と合わせて 0.3m の立方体にする

	auto& bScript = registry.emplace<ScriptComponent>(bullet);
	bScript.scripts.push_back({"BulletScript", "", nullptr});

	attackTimer_ = currentAttackInterval;
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
	ImGui::Text("Connected Tanks: %d", connectedTankCount);
#endif
}

void Canon::UpdateConnection(entt::entity entity, GameScene* scene) {
    auto& registry = scene->GetRegistry();
    const float connectRange = 2.5f;

    // ★ 1. 高速タグ検索を利用してリストを取得 (O(1))
    const auto& allPipes = scene->GetEntitiesByTag("Pipe");
    const auto& allTanks = scene->GetEntitiesByTag("BulletTank");
    const auto& allCanons = scene->GetEntitiesByTag("Canon");

    // 2. 接続されているタンクを探す
    std::unordered_set<entt::entity> foundTanks;
    std::unordered_set<entt::entity> visitedPipesForTanks;

    for (auto pipe : allPipes) {
        if (IsConnectedSphere(registry, entity, pipe, connectRange)) {
            CollectConnectedBulletTanks(registry, pipe, visitedPipesForTanks, foundTanks, allPipes, allTanks, connectRange);
        }
    }
    connectedTankCount = (int)foundTanks.size();
    isConnectedToTank_ = (connectedTankCount > 0);

    // 3. 接続されている大砲を探す
    std::unordered_set<entt::entity> foundCanons;
    std::unordered_set<entt::entity> visitedPipesForCanons;

    for (auto pipe : allPipes) {
        if (IsConnectedSphere(registry, entity, pipe, connectRange)) {
            CollectConnectedCanons(registry, pipe, visitedPipesForCanons, foundCanons, allPipes, allCanons, connectRange);
        }
    }
    // 自分自身を含める
    foundCanons.insert(entity);
    connectedCanonCount = (int)foundCanons.size();
}

void Canon::Debug(bool connected) {
	(void)connected;
#ifndef NDEBUG
#ifdef USE_IMGUI
	ImGui::Begin("Canon Debug");
	ImGui::Text("Canon connected to tank: %s", isConnectedToTank_ ? "YES" : "NO");
	ImGui::Text("Connected Tanks: %d", connectedTankCount);
	ImGui::Text("Connected Canons: %d", connectedCanonCount);
	ImGui::End();
#endif
#endif
}

REGISTER_SCRIPT(Canon);

} // namespace Game
