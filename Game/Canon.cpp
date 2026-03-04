#include "Canon.h"
#include "ObjectTypes.h"
#include "Scenes/GameScene.h"
#include "ScriptEngine.h"
#include <cmath>

#include "../imgui/imgui.h"
namespace Game {

struct TilePos {
	int x;
	int z;
};

//static int RoundToInt(float v) {
//	if (v >= 0.0f) {
//		return (int)(v + 0.5f);
//	}
//	return (int)(v - 0.5f);
//}
//
//static TilePos GetTilePosXZ(const SceneObject& obj) {
//	TilePos pos;
//	pos.x = RoundToInt(obj.translate.x);
//	pos.z = RoundToInt(obj.translate.z);
//	return pos;
//}

static bool HasTag(const SceneObject& obj, const char* tagName) {
	for (int i = 0; i < (int)obj.tags.size(); ++i) {
		if (obj.tags[i].tag == tagName) {
			return true;
		}
	}
	return false;
}
//static bool HasPipeAt(GameScene* scene, int tx, int tz) {
//	for (const SceneObject& other : scene->GetObjects()) {
//
//		if (!HasTag(other, "Pipe")) {
//			continue;
//		}
//
//		TilePos p = GetTilePosXZ(other);
//		if (p.x == tx && p.z == tz) {
//			return true;
//		}
//	}
//	return false;
//}

static bool IsCanonConnectedToPipe(GameScene* scene, const SceneObject& canonObj) {
	const float connectRange = 2.0f;  // つながる距離
	const float axisTolerance = 0.6f; // 軸のズレ許容（小さいほど4方向っぽい）

	for (const SceneObject& other : scene->GetObjects()) {

		if (!HasTag(other, "Pipe")) {
			continue;
		}

		float dx = other.translate.x - canonObj.translate.x;
		float dz = other.translate.z - canonObj.translate.z;

		float dist = std::sqrt(dx * dx + dz * dz);
		if (dist > connectRange) {
			continue;
		}

		float absDx = std::fabs(dx);
		float absDz = std::fabs(dz);

		// 4方向っぽくする：XかZのどちらかがほぼ同じラインならOK
		if (absDx <= axisTolerance || absDz <= axisTolerance) {
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


	for (const SceneObject& other : scene->GetObjects()) {
		objectCount += 1;

		if (HasTag(other, "Pipe")) {
			pipeCount += 1;
		}

		if (HasTag(other, "Enemy")) {
			enemyCount += 1;
		}
	}

	ImGui::Begin("Debug Pipe");
	ImGui::Text("Objects: %d", objectCount);
	ImGui::Text("Pipes  : %d", pipeCount);
	ImGui::Text("Enemies: %d", enemyCount);

	bool connected = IsCanonConnectedToPipe(scene, obj);
	ImGui::Text("Canon connected: %s", connected ? "YES" : "NO");
	ImGui::End();

	// objectCount と pipeCount を表示

	// クールダウン
	if (attackTimer_ > 0.0f) {
		attackTimer_ -= dt;
	}

	if (!IsCanonConnectedToPipe(scene, obj)) {
		return; // Pipeが繋がってない
	}
	// 一番近い Enemy を探す（範囲内）
	const SceneObject* target = nullptr;
	float bestDistance = attackRange_;

	for (const SceneObject& other : scene->GetObjects()) {

		if (!HasTag(other, "Enemy")) {
			continue;
		}

		float dx = other.translate.x - obj.translate.x;
		float dz = other.translate.z - obj.translate.z;
		float distance = std::sqrt(dx * dx + dz * dz);

		if (distance < bestDistance) {
			bestDistance = distance;
			target = &other;
		}
	}

	// ターゲットがいないなら何もしない
	if (target == nullptr) {
		return;
	}

	// 敵方向（XZ）
	float toX = target->translate.x - obj.translate.x;
	float toZ = target->translate.z - obj.translate.z;

	if (std::fabs(toX) < 0.0001f && std::fabs(toZ) < 0.0001f) {
		return;
	}

	// 大砲を敵の方向へ向ける（毎フレーム）
	float desiredYaw = std::atan2(toX, toZ);
	obj.rotate.y = desiredYaw;

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

	// 砲口を前に出す（大砲っぽく）
	float muzzleOffset = 2.0f;
	bullet.translate.x += std::sin(desiredYaw) * muzzleOffset;
	bullet.translate.z += std::cos(desiredYaw) * muzzleOffset;

	bullet.rotate = obj.rotate;
	bullet.scale = {0.3f, 0.3f, 0.3f};

	// 見た目
	auto* renderer = scene->GetRenderer();
	if (renderer) {
		bullet.modelHandle = renderer->LoadObjMesh("Resources/cube/cube.obj");
		bullet.textureHandle = renderer->LoadTexture2D("Resources/white1x1.png");

		MeshRendererComponent mr;
		mr.modelHandle = bullet.modelHandle;
		mr.textureHandle = bullet.textureHandle;
		bullet.meshRenderers.push_back(mr);
	}

	// 当たり判定
	HitboxComponent hb;
	hb.isActive = true;
	hb.damage = damage_;
	hb.tag = "Bullet";
	hb.size = {0.3f, 0.3f, 0.3f};
	bullet.hitboxes.push_back(hb);

	// 体力
	HealthComponent hc;
	hc.hp = 1.0f;
	hc.maxHp = 1.0f;
	bullet.healths.push_back(hc);

	// タグ
	TagComponent tc;
	tc.tag = "Bullet";
	bullet.tags.push_back(tc);

	// 弾スクリプト
	ScriptComponent sc;
	sc.scriptPath = "BulletScript";
	bullet.scripts.push_back(sc);

	scene->SpawnObject(bullet);

	// クールダウン再セット
	attackTimer_ = attackInterval_;
}

void Canon::OnDestroy(SceneObject& /*obj*/, GameScene* /*scene*/) {}

REGISTER_SCRIPT(Canon);

} // namespace Game