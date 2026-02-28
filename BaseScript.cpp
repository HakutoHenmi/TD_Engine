#include "BaseScript.h"
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

void BaseScript::Start(SceneObject& obj, GameScene* /*scene*/) {
	(void)obj;
	attackTimer_ = 0.0f;
}

void BaseScript::Update(SceneObject& obj, GameScene* scene, float dt) {

	// 発射クールダウン
	if (attackTimer_ > 0.0f) {
		attackTimer_ -= dt;
	}

	// 1) 一番近いEnemyを探す（範囲内）
	const SceneObject* target = nullptr;
	float bestDistSq = attackRange_ * attackRange_;

	for (const auto& other : scene->GetObjects()) {

		if (!HasTag(other, "Enemy")) {
			continue;
		}
		// 追加：死んでる敵は無視
		if (!other.healths.empty() && other.healths[0].hp <= 0.0f) {
			continue;
		}

		float dx = other.translate.x - obj.translate.x;
		float dz = other.translate.z - obj.translate.z;
		float distSq = dx * dx + dz * dz;

		if (distSq < bestDistSq) {
			bestDistSq = distSq;
			target = &other;
		}
	}

	if (target == nullptr) {
		return;
	}

	// 2) ターゲット方向へ向ける（Y回転だけ）
	float toX = target->translate.x - obj.translate.x;
	float toZ = target->translate.z - obj.translate.z;

	if (std::fabs(toX) < 0.0001f && std::fabs(toZ) < 0.0001f) {
		return;
	}

	// +Zが前の前提：PlayerScriptの sin/cos と合わせるため atan2(x, z)
	float desiredYaw = std::atan2(toX, toZ);

	// すぐ向ける（まずはこれでOK）
	obj.rotate.y = desiredYaw;

	// 3) クールダウン終わってたら撃つ
	if (attackTimer_ > 0.0f) {
		return;
	}

	SceneObject bullet;
	bullet.name = "Bullet";

	bullet.translate = obj.translate;
	bullet.translate.y += 2.0f;

	bullet.translate.x += std::sin(obj.rotate.y) * 1.5f;
	bullet.translate.z += std::cos(obj.rotate.y) * 1.5f;

	bullet.rotate = obj.rotate;
	bullet.scale = {0.2f, 0.2f, 0.2f};

	auto* renderer = scene->GetRenderer();
	if (renderer) {
		bullet.modelHandle = renderer->LoadObjMesh("Resources/cube/cube.obj");
		bullet.textureHandle = renderer->LoadTexture2D("Resources/white1x1.png");

		MeshRendererComponent mr;
		mr.modelHandle = bullet.modelHandle;
		mr.textureHandle = bullet.textureHandle;
		bullet.meshRenderers.push_back(mr);
	}

	HitboxComponent hb;
	hb.isActive = true;
	hb.damage = damage_;
	hb.tag = "Bullet"; // まずは Player と同じにしとく（当たるか確認）
	hb.size = {1.0f, 1.0f, 1.0f};
	bullet.hitboxes.push_back(hb);

	HealthComponent hc;
	hc.hp = 1.0f;
	hc.maxHp = 1.0f;
	bullet.healths.push_back(hc);

	TagComponent tc;
	tc.tag = "Bullet"; // まずは Player と同じ
	bullet.tags.push_back(tc);

	ScriptComponent sc;
	sc.scriptPath = "BulletScript";
	bullet.scripts.push_back(sc);

	scene->SpawnObject(bullet);

	attackTimer_ = attackInterval_;
}

void BaseScript::OnDestroy(SceneObject& /*obj*/, GameScene* /*scene*/) {}

REGISTER_SCRIPT(BaseScript);

} // namespace Game