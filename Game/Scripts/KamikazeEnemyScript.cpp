#include "KamikazeEnemyScript.h"
#include "ObjectTypes.h"
#include "Scenes/GameScene.h"
#include "ScriptEngine.h"
#include <cmath>
#include <cstdlib>
#include <string>

namespace Game {

void KamikazeEnemyScript::Start(SceneObject& /*obj*/, GameScene* /*scene*/) {
	isExploded_ = false;
}

void KamikazeEnemyScript::Update(SceneObject& obj, GameScene* scene, float dt) {
	// 死亡済みなら何もしない
	if (!obj.healths.empty() && obj.healths[0].hp <= 0.0f) {
		return;
	}
	if (isExploded_) return;

	// プレイヤー検索
	DirectX::XMFLOAT3 targetPos = {0, 0, 0};
	// キャッシュが有効か確認
	bool found = false;
	if (playerIndexCache_ != static_cast<size_t>(-1) && playerIndexCache_ < scene->GetObjects().size()) {
		if (scene->GetObjects()[playerIndexCache_].name == "Player") {
			targetPos = scene->GetObjects()[playerIndexCache_].translate;
			found = true;
		}
	}

	// キャッシュが無効なら検索
	if (!found) {
		const auto& objects = scene->GetObjects();
		for (size_t i = 0; i < objects.size(); ++i) {
			if (objects[i].name == "Player") {
				targetPos = objects[i].translate;
				playerIndexCache_ = i;
				found = true;
				break;
			}
		}
	}

	if (!found) {
		// プレイヤーが見つからない場合、一度だけログを出して警告する
		static bool playerNotFoundWarned = false;
		if (!playerNotFoundWarned) {
			playerNotFoundWarned = true;
		}
		return;
	}

	if (found) {
		float dx = obj.translate.x - targetPos.x;
		float dy = obj.translate.y - targetPos.y;
		float dz = obj.translate.z - targetPos.z;
		float distSq = dx * dx + dy * dy + dz * dz;
		float dist = std::sqrt(distSq);

		// 視界内なら追尾
		if (dist <= sightRange_) {
			// 自爆距離に到達したら爆発
			if (dist <= triggerRange_) {
				Explode(obj, scene);
				return;
			}

			// プレイヤーに向かって移動
			if (dist > 0.001f) {
				obj.translate.x -= (dx / dist) * speed_ * dt;
				obj.translate.y -= (dy / dist) * speed_ * dt;
				obj.translate.z -= (dz / dist) * speed_ * dt;

				// 進行方向を向く (Y軸回転)
				obj.rotate.y = std::atan2(dx, dz) + 3.14159f;
			}
		}
	}
}

void KamikazeEnemyScript::Explode(SceneObject& obj, GameScene* scene) {
	isExploded_ = true;

	// 1. 攻撃判定 (Hitbox) を有効化して範囲攻撃にする
	if (obj.hitboxes.empty()) {
		obj.hitboxes.push_back(HitboxComponent());
	}
	// 既存のHitboxがあればそれを上書き、なければ新規設定
	auto& hb = obj.hitboxes[0];
	hb.isActive = true;
	hb.damage = damage_;
	hb.size = {explosionRadius_, explosionRadius_, explosionRadius_};
	hb.tag = "Explosion";

	// 2. 爆発エフェクト（破片）を生成
	int debrisCount = 15; // 派手に散らす
	for (int i = 0; i < debrisCount; ++i) {
		SceneObject debris;
		debris.name = "ExplosionDebris";
		debris.translate = obj.translate;

		// ランダムに散らす
		float randX = ((rand() % 100) / 50.0f - 1.0f);
		float randY = ((rand() % 100) / 50.0f); // 上方向重視
		float randZ = ((rand() % 100) / 50.0f - 1.0f);

		debris.translate.x += randX * 0.5f;
		debris.translate.y += randY * 0.5f;
		debris.translate.z += randZ * 0.5f;
		debris.scale = {obj.scale.x * 0.3f, obj.scale.y * 0.3f, obj.scale.z * 0.3f};

		// メッシュ継承（赤くして爆発感を出す）
		if (!obj.meshRenderers.empty()) {
			MeshRendererComponent mr = obj.meshRenderers[0];
			mr.color = {1.0f, 0.2f, 0.2f, 1.0f}; // 赤色
			debris.meshRenderers.push_back(mr);
		}

		// 物理演算で吹き飛ばす
		RigidbodyComponent rb;
		rb.useGravity = true;
		rb.isKinematic = false;
		float power = 10.0f;
		rb.velocity = {randX * power, randY * power + 3.0f, randZ * power};
		debris.rigidbodies.push_back(rb);

		scene->SpawnObject(debris);
	}

	// 3. 自身を死亡させる (HPを0にする)
	if (obj.healths.empty()) obj.healths.push_back(HealthComponent());
	obj.healths[0].hp = 0.0f;
}

void KamikazeEnemyScript::OnDestroy(SceneObject& /*obj*/, GameScene* /*scene*/) {}

// 自動登録
REGISTER_SCRIPT(KamikazeEnemyScript);

} // namespace Game