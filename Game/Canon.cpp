#include "Canon.h"
#include "ObjectTypes.h"
#include "Scenes/GameScene.h"
#include "ScriptEngine.h"
#include <cmath>

namespace Game {

static bool HasTag(const SceneObject& obj, const char* tagName) {
	for (int i = 0; i < (int)obj.tags.size(); ++i) {
		if (obj.tags[i].tag == tagName) {
			return true;
		}
	}
	return false;
}

void Canon::Start(SceneObject& obj, GameScene* /*scene*/) { (void)obj;

attackTimer_ = 0.0f;

}

void Canon::Update(SceneObject& obj, GameScene* scene, float dt) {

	// クールダウン
	if (attackTimer_ > 0.0f) {
		attackTimer_ -= dt;
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