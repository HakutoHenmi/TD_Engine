#include "BaseScript.h"
#include "ObjectTypes.h"
#include "Scenes/GameScene.h"
#include "ScriptEngine.h"
#include <cmath>

namespace Game {

static bool HasTag(const SceneObject& obj, const char* tagName) {
	for (int i = 0; i < (int)obj.tags.size(); ++i) { // タグ配列を最初から最後までループして、指定されたタグがあるか確認
		if (obj.tags[i].tag == tagName) {            // タグが見つかったらtrueを返す
			return true;
		}
	}
	return false; // タグが見つからなかったらfalseを返す
}

void BaseScript::Start(SceneObject& obj, GameScene* /*scene*/) {
	(void)obj;
	attackTimer_ = 0.0f; // クールダウン初期化
}

void BaseScript::Update(SceneObject& obj, GameScene* scene, float dt) {


	// タワーを回転させる
	obj.rotate.y += rotationSpeed_ * dt;


	// 発射クールダウン
	if (attackTimer_ > 0.0f) {
		attackTimer_ -= dt;
	}

	//  一番近いEnemyを探す（範囲内）
	const SceneObject* target = nullptr; // 最初はターゲットなし

	for (const auto& other : scene->GetObjects()) { // シーン内の全オブジェクトをループして見ます

		// この場合、otherが敵かどうかをタグで判断します
		if (!HasTag(other, "Enemy")) { // もし敵のタグがなければ無視
			continue;                  // Enemyではなかったらその回のforのループをスキップして次のオブジェクトをチェックします
		}
		// これハッシュタグを増やせば他の種類の敵も判定できるようになり

		//  ここからは Enemy のときだけ実行される
		// other(エネミー)とobj(タワー)の距離を計算します（Yは無視してXZ平面で）
		// これはXZ平面の距離を計算するためのコードそしてYは無視されるので円柱型の当たり判定になります。
		float dx = other.translate.x - obj.translate.x;
		float dz = other.translate.z - obj.translate.z;
		float dist = std::sqrt(dx * dx + dz * dz);

		// 距離が攻撃範囲内ならother(エネミー)をターゲットに設定する。
		if (dist < attackRange_) {
			target = &other;
		}
	}

	//  ターゲットがいなければ何もしない
	if (target == nullptr) {
		return;
	}

	// クールダウン終わってたら撃つ
	if (attackTimer_ > 0.0f) {
		return;
	}


	// 弾を生成して撃つ
	SceneObject bullet;
	bullet.name = "Bullet";//このオブジェクトは弾です？

	bullet.translate = obj.translate;
	bullet.translate.y += 2.0f;

	// 敵方向（XZ）
	float toX = target->translate.x - obj.translate.x;
	float toZ = target->translate.z - obj.translate.z;

	if (std::fabs(toX) < 0.0001f && std::fabs(toZ) < 0.0001f) {
		return;
	}

	float desiredYaw = std::atan2(toX, toZ);

	// ここを desiredYaw にする
	bullet.translate.x += std::sin(desiredYaw) * 1.5f;
	bullet.translate.z += std::cos(desiredYaw) * 1.5f;

	// 回転はVector3でセット
	bullet.rotate = obj.rotate;
	bullet.rotate.y = desiredYaw;

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