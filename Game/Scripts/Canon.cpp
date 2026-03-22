#include "Canon.h"
#include "ObjectTypes.h"
#include "Scenes/GameScene.h"
#include "ScriptEngine.h"
#include <cmath>
#include <vector>

#include "../imgui/imgui.h"

namespace Game {

// タグ走査
static bool HasTag(const SceneObject& obj, const char* tagName) {
	for (int i = 0; i < (int)obj.tags.size(); ++i) {
		if (obj.tags[i].tag == tagName) {
			return true;
		}
	}
	return false;
}

// 球体接続判定
static bool IsConnectedSphere(const SceneObject& a, const SceneObject& b, float connectRange) {
	float dx = b.translate.x - a.translate.x;
	float dy = b.translate.y - a.translate.y;
	float dz = b.translate.z - a.translate.z;

	float dist = std::sqrt(dx * dx + dy * dy + dz * dz);

	if (dist > connectRange) {
		return false;
	}

	return true;
}

// すでに訪問したオブジェクトかどうか（ループ防止）
static bool IsAlreadyVisited(const std::vector<const SceneObject*>& visitedObjects, const SceneObject& obj) {
	for (int i = 0; i < (int)visitedObjects.size(); ++i) {
		if (visitedObjects[i] == &obj) {
			return true;
		}
	}
	return false;
}

static bool IsPipeConnectedToBulletTankRecursive(GameScene* scene, const SceneObject& currentPipe, std::vector<const SceneObject*>& visitedObjects, float connectRange) {

	visitedObjects.push_back(&currentPipe);

	for (const SceneObject& other : scene->GetObjects()) {

		if (&other == &currentPipe) {
			continue;
		}

		if (!IsConnectedSphere(currentPipe, other, connectRange)) {
			continue;
		}

		if (HasTag(other, "BulletTank")) {
			return true;
		}

		if (HasTag(other, "Pipe")) {

			if (IsAlreadyVisited(visitedObjects, other)) {
				continue;
			}

			bool connected = IsPipeConnectedToBulletTankRecursive(scene, other, visitedObjects, connectRange);

			if (connected) {
				return true;
			}
		}
	}

	return false;
}

static bool IsCanonConnectedToBulletTank(GameScene* scene, const SceneObject& canonObj) {
	const float connectRange = 2.5f;

	for (const SceneObject& other : scene->GetObjects()) {

		if (!HasTag(other, "Pipe")) {
			continue;
		}

		if (!IsConnectedSphere(canonObj, other, connectRange)) {
			continue;
		}

		std::vector<const SceneObject*> visitedObjects;

		bool connected = IsPipeConnectedToBulletTankRecursive(scene, other, visitedObjects, connectRange);

		if (connected) {
			return true;
		}
	}

	return false;
}

void Canon::Start(SceneObject& obj, GameScene* /*scene*/) {
	(void)obj;

	attackTimer_ = 0.0f;
}

void Canon::Update(SceneObject& obj, GameScene* scene, float dt) {

	objectCount = 0;
	pipeCount = 0;
	enemyCount = 0;

	for (const SceneObject& other : scene->GetObjects()) {
		objectCount += 1;

		if (HasTag(other, "Pipe")) {
			pipeCount += 1;
		}

		if (HasTag(other, "Enemy")) {
			enemyCount += 1;
		}
	}

	bool connected = IsCanonConnectedToBulletTank(scene, obj);

	Debug(connected);

	// クールダウン
	if (attackTimer_ > 0.0f) {
		attackTimer_ -= dt;
	}

	// 弾倉と繋がっていないなら何もしない
	if (!connected) {
		return;
	}

	// 一番近い Enemy を探す（範囲内）
	const SceneObject* target = nullptr;
	float bestDistance = attackRange_;

	for (const SceneObject& other : scene->GetObjects()) {

		if (!HasTag(other, "Enemy")) {
			continue;
		}

		float dx = other.translate.x - obj.translate.x;
		float dy = other.translate.y - obj.translate.y;
		float dz = other.translate.z - obj.translate.z;
		float distance = std::sqrt(dx * dx + dy * dy + dz * dz);

		if (distance < bestDistance) {
			bestDistance = distance;
			target = &other;
		}
	}

	// ターゲットがいないなら何もしない
	if (target == nullptr) {
		return;
	}

	// 敵方向（XYZ）
	float toX = target->translate.x - obj.translate.x;
	float toY = target->translate.y - obj.translate.y;
	float toZ = target->translate.z - obj.translate.z;

	if (std::fabs(toX) < 0.0001f && std::fabs(toY) < 0.0001f && std::fabs(toZ) < 0.0001f) {
		return;
	}

	// 左右角度
	float desiredYaw = std::atan2(toX, toZ);

	// 上下角度
	float horizontalDistance = std::sqrt(toX * toX + toZ * toZ);
	float desiredPitch = std::atan2(toY, horizontalDistance);

	// 大砲を敵の方向へ向ける
	obj.rotate.y = desiredYaw;
	obj.rotate.x = -desiredPitch;

	// クールダウン中なら撃たない（向くだけ）
	if (attackTimer_ > 0.0f) {
		return;
	}

	// =========================
	// 弾を生成して撃つ
	// =========================
	SceneObject bullet;
	bullet.name = "Bullet";

	bullet.translate = obj.translate;
	bullet.translate.y += 2.0f;

	// 前方向ベクトル（上下込み）
	float forwardX = std::sin(desiredYaw) * std::cos(desiredPitch);
	float forwardY = std::sin(desiredPitch);
	float forwardZ = std::cos(desiredYaw) * std::cos(desiredPitch);

	// 砲口を前に出す
	float muzzleOffset = 2.0f;
	bullet.translate.x += forwardX * muzzleOffset;
	bullet.translate.y += forwardY * muzzleOffset;
	bullet.translate.z += forwardZ * muzzleOffset;

	bullet.rotate = obj.rotate;
	bullet.rotate.x = -desiredPitch;
	bullet.rotate.y = desiredYaw;
	bullet.scale = {0.3f, 0.3f, 0.3f};

	// 見た目
	auto* renderer = scene->GetRenderer();
	if (renderer != nullptr) {
		bullet.modelHandle = renderer->LoadObjMesh("Resources/cube/cube.obj");
		bullet.textureHandle = renderer->LoadTexture2D("Resources/white1x1.png");

		MeshRendererComponent meshRenderer;
		meshRenderer.modelHandle = bullet.modelHandle;
		meshRenderer.textureHandle = bullet.textureHandle;
		bullet.meshRenderers.push_back(meshRenderer);
	}

	// 当たり判定
	HitboxComponent hitbox;
	hitbox.isActive = true;
	hitbox.damage = damage_;
	hitbox.tag = "Bullet";
	hitbox.size = {0.3f, 0.3f, 0.3f};
	bullet.hitboxes.push_back(hitbox);

	// 体力
	HealthComponent health;
	health.hp = 1.0f;
	health.maxHp = 1.0f;
	bullet.healths.push_back(health);

	// タグ
	TagComponent tag;
	tag.tag = "Bullet";
	bullet.tags.push_back(tag);

	// 弾スクリプト
	ScriptComponent script;
	script.scriptPath = "BulletScript";
	bullet.scripts.push_back(script);

	scene->SpawnObject(bullet);

	// クールダウン再セット
	attackTimer_ = attackInterval_;
}

void Canon::OnDestroy(SceneObject& /*obj*/, GameScene* /*scene*/) {}

void Canon::Debug(bool connected) {
	(void)connected;
#ifndef NDEBUG
	const char* connectedText = "NO";
	if (connected) {
		connectedText = "YES";
	}

	ImGui::Begin("Debug Pipe");
	ImGui::Text("Objects: %d", objectCount);
	ImGui::Text("Pipes  : %d", pipeCount);
	ImGui::Text("Enemies: %d", enemyCount);
	ImGui::Text("Canon connected to tank: %s", connectedText);
	ImGui::End();
#endif
}

REGISTER_SCRIPT(Canon);

} // namespace Game