#include "EnemyAIScript.h"
#include "ObjectTypes.h"
#include "Scenes/GameScene.h"
#include "ScriptEngine.h"
#include <cmath>
#include <cstdlib> // rand()用
#include <iostream>

namespace Game {

void EnemyAIScript::Start(SceneObject& /*obj*/, GameScene* /*scene*/) {
	exploded_ = false; // 再プレイ時にリセット
}

void EnemyAIScript::Update(SceneObject& obj, GameScene* scene, float dt) {
	// ==========================================
	// ★ 1. 死亡判定と爆発（破片生成）処理
	// ==========================================
	if (!obj.healths.empty() && obj.healths[0].hp <= 0.0f && !exploded_) {
		exploded_ = true;

		int debrisCount = 6; // 散らばる破片の数
		for (int i = 0; i < debrisCount; ++i) {
			SceneObject debris;
			debris.name = "Debris";
			debris.translate = obj.translate;

			// ランダムな方向に少しずらして配置
			float randX = ((rand() % 100) / 100.0f - 0.5f) * 2.0f; // -1.0 〜 1.0
			float randY = ((rand() % 100) / 100.0f) * 2.0f;        // 0.0 〜 2.0 (上に飛びやすく)
			float randZ = ((rand() % 100) / 100.0f - 0.5f) * 2.0f; // -1.0 〜 1.0

			debris.translate.x += randX;
			debris.translate.y += randY;
			debris.translate.z += randZ;

			// スケールを元のオブジェクトの40%の大きさにする
			debris.scale = {obj.scale.x * 0.4f, obj.scale.y * 0.4f, obj.scale.z * 0.4f};

			// --- 見た目 (元の敵のメッシュと色を引き継ぐ) ---
			if (!obj.meshRenderers.empty()) {
				MeshRendererComponent mr = obj.meshRenderers[0];
				debris.meshRenderers.push_back(mr);
			}

			// --- 物理演算 (吹き飛ぶ力) ---
			RigidbodyComponent rb;
			rb.useGravity = true;
			rb.isKinematic = false;
			float explosionPower = 6.0f; // 吹き飛ぶ強さ
			// ランダムな方向に初速を与える
			rb.velocity = {randX * explosionPower, 4.0f + randY * 4.0f, randZ * explosionPower};
			debris.rigidbodies.push_back(rb);

			// --- 当たり判定 (地面とぶつかってバウンドするように) ---
			if (!obj.boxColliders.empty()) {
				BoxColliderComponent bc;
				bc.size = {1.0f, 1.0f, 1.0f}; // 破片用のサイズ
				debris.boxColliders.push_back(bc);
			}

			// --- 寿命 ---
			// CleanupSystem が isDead=true のオブジェクトを処理するはずなので、
			// 破片がすぐに消えないようHPを持たせておく
			HealthComponent hc;
			hc.hp = 1.0f;
			debris.healths.push_back(hc);

			// ★ 新アーキテクチャの SpawnObject を使用して安全に追加！
			scene->SpawnObject(debris);
		}
		return; // 爆発処理を行ったら、以降の追尾処理はスキップする
	}

	// ==========================================
	// ★ 2. プレイヤー追尾処理 (HPが残っている場合のみ)
	// ==========================================
	if (obj.healths.empty() || obj.healths[0].hp > 0.0f) {
		DirectX::XMFLOAT3 targetPos = {0, 0, 0};
		// プレイヤーを探す (キャッシュを活用)
		bool found = false;
		if (playerIndexCache_ != static_cast<size_t>(-1) && playerIndexCache_ < scene->GetObjects().size()) {
			if (scene->GetObjects()[playerIndexCache_].name == "Player") {
				targetPos = scene->GetObjects()[playerIndexCache_].translate;
				found = true;
			}
		}

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

		if (found) {
			float dx = obj.translate.x - targetPos.x;
			float dy = obj.translate.y - targetPos.y;
			float dz = obj.translate.z - targetPos.z;
			float distSq = dx * dx + dy * dy + dz * dz;

			// 視界範囲内なら移動
			if (distSq <= sightRange_ * sightRange_ && distSq > 0.001f) {
				float dist = std::sqrt(distSq);
				obj.translate.x -= (dx / dist) * speed_ * dt;
				obj.translate.y -= (dy / dist) * speed_ * dt;
				obj.translate.z -= (dz / dist) * speed_ * dt;
			}
		}
	}
}

void EnemyAIScript::OnDestroy(SceneObject& /*obj*/, GameScene* /*scene*/) {}

// ★ スクリプト自動登録
REGISTER_SCRIPT(EnemyAIScript);

} // namespace Game