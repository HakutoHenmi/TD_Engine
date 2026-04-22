#include "BaseEnemy.h"
#include "../../Engine/ThirdParty/nlohmann/json.hpp"
#ifdef USE_IMGUI
#include "../../externals/imgui/imgui.h"
#endif
#include "ObjectTypes.h"
#include "Scenes/GameScene.h"
#include "../ScriptEngine.h"

#include "../PhaseSystemScript.h"

namespace Game {

static bool HasTag(entt::registry& registry, entt::entity entity, TagType tagName) {
	if (!registry.valid(entity) || !registry.all_of<TagComponent>(entity)) return false;
	return registry.get<TagComponent>(entity).tag == tagName;
}

void BaseEnemy::Start(entt::entity entity, GameScene* scene) {
	ownerId_ = static_cast<uint32_t>(entity);
	pCurrentScene_ = scene;
	auto& registry = scene->GetRegistry();
	auto& tc = registry.get<TransformComponent>(entity);

	// ★修正: 物理演算を有効化し、ノックバックを受け入れ可能にする
	if (registry.all_of<RigidbodyComponent>(entity)) {
		registry.get<RigidbodyComponent>(entity).isKinematic = false;
	}

	// 出現時に地面の高さを即座に計算（初動の埋まり防止）
	float h = scene->GetHeightAt(tc.translate.x, tc.translate.z, tc.translate.y + 1.0f, static_cast<uint32_t>(entity));
	if (h > -9999.0f) {
		groundHeight_ = h;
	} else {
		groundHeight_ = tc.translate.y - 1.0f; // 地面が見つからない場合は現在の位置を基準にする
	}

	// Flyタイプの場合は重力を無効化し、初期高度を設定
	if (type_ == Fly) {
		if (registry.all_of<RigidbodyComponent>(entity)) {
			auto& rb = registry.get<RigidbodyComponent>(entity);
			rb.useGravity = false;
			rb.velocity = {0, 0, 0};
		}

		// スポナーの高さではなく、即座に正しい浮遊高度へ移動
		float baseHeight = 9.0f;
		tc.translate.y = groundHeight_ + baseHeight;
	} else {
		// Walkタイプも埋まり防止のためにオフセットを乗せる
		tc.translate.y = groundHeight_ + 1.0f;
	}
}

void BaseEnemy::Update(entt::entity entity, GameScene* scene, float dt) {
	auto& registry = scene->GetRegistry();

	// 定期的に索敵(共通)
	scanTimer_ += dt;
	if (scanTimer_ > 0.3f) {
		scanTimer_ = 0.0f;
		SearchTarget(entity, scene);
	}

	// 攻撃のクールタイムを減らす
	if (attackCooltime_ > 0.0f) {
		attackCooltime_ -= dt;
	}

	// ターゲットとの距離をチェック
	bool inAttackRange = false;
	if (registry.valid(currentTarget_)) {
		auto& myTc = registry.get<TransformComponent>(entity);
		auto& tarTc = registry.get<TransformComponent>(currentTarget_);
		float dx = tarTc.translate.x - myTc.translate.x;
		float dz = tarTc.translate.z - myTc.translate.z;
		float distSq = dx * dx + dz * dz;

		if (distSq <= attackRange_ * attackRange_) {
			inAttackRange = true;
		}

		// 遠すぎたらターゲットを外す
		if (distSq > loseTargetRange_ * loseTargetRange_) {
			currentTarget_ = entt::null;
		}
	}

	// 実際の行動
	if (inAttackRange&& attackCooltime_ <= 0.0f) {
		// 攻撃範囲内且つクールタイムを満たしていれば足を止めて攻撃
		ExecuteAttack(entity, scene, dt);
	}
	else {
		// 攻撃関数を呼んでない時は攻撃判定を消す
		if (registry.all_of<HitboxComponent>(entity)) {
			registry.get<HitboxComponent>(entity).isActive = false;
		}

		DefaultMove(entity, scene, dt);
	}

	

}

void BaseEnemy::OnDestroy(entt::entity entity, GameScene* scene) {
	// エンティティがHealthComponentを持っているか確認


	if (scene->GetRegistry().all_of<HealthComponent>(entity)) {
		float hp = scene->GetRegistry().get<HealthComponent>(entity).hp;

		// ちゃんとHPが0以下になって倒された場合のみコインを増やす
		if (hp <= 0.0f) {
			PhaseSystemScript::PlusCoinCount(10);///////ますやごう
		}
	}

	// その他、終了時のクリーンアップなどを記述
}

void BaseEnemy::OnEditorUI() {
#if defined(USE_IMGUI) && !defined(NDEBUG)
	// 敵の移動タイプ(地面や空中)
	int typeNum = static_cast<int>(type_);
	const char* types[] = {"Walk", "Fly"};
	if (ImGui::Combo("Enemy Type", &typeNum, types, IM_ARRAYSIZE(types))) {
		type_ = static_cast<MoveType>(typeNum);
	}
#endif
}

void BaseEnemy::DefaultMove(entt::entity entity, GameScene* scene, float /*dt*/) {
	auto& registry = scene->GetRegistry();
	auto& tc = registry.get<TransformComponent>(entity);

	float dirX = 0.0f;
	float dirZ = 0.0f;

	// ターゲットが有効なら追尾
	bool chaseTarget = false;
	if (registry.valid(currentTarget_) && registry.all_of<TransformComponent>(currentTarget_)) {
		auto& tarTc = registry.get<TransformComponent>(currentTarget_);
		float dx = tarTc.translate.x - tc.translate.x;
		float dz = tarTc.translate.z - tc.translate.z;
		float dist = std::sqrt(dx * dx + dz * dz);
		if (dist > 0.001f) {
			dirX = dx / dist;
			dirZ = dz / dist;
			chaseTarget = true;
		}
	}

	if (!chaseTarget) {
		// 1. NavigationManager を取得
		auto& nav = scene->GetNavigationManager();

		// 2. 自分の足元の「進むべき方向」をマネージャーに聞く
		nav.GetDirection(tc.translate.x, tc.translate.z, dirX, dirZ);
	}

	// 3. 物理コンポーネントがあるかチェック
	if (registry.all_of<RigidbodyComponent>(entity)) {
		auto& rb = registry.get<RigidbodyComponent>(entity);

		// 移動速度を計算
		float vx = dirX * speed_;
		float vz = dirZ * speed_;

		if (type_ == Walk) {
			// 地面を歩くタイプ
			rb.velocity.x = vx;
			rb.velocity.z = vz;

			// 地面の高さに合わせて y 座標を補正（埋まり・浮き防止）
			float h = scene->GetHeightAt(tc.translate.x, tc.translate.z, tc.translate.y + 1.0f, static_cast<uint32_t>(entity));
			if (h > -9000.0f) {
				tc.translate.y = h + 0.1f; // 少しだけ浮かせて接地させる
			}
		} 
		//else {
		//	// 飛行タイプ（y軸はふわふわさせる）
		//	rb.velocity.x = vx;
		//	rb.velocity.z = vz;

		//	float floatHeight = 5.0f; // 地面から5m上を飛ぶ
		//	float targetY = groundHeight_ + floatHeight + std::sin(scene->GetContext().playTime * 2.0f) * 0.5f;
		//	tc.translate.y += (targetY - tc.translate.y) * 2.0f * dt;
		//}

		// 4. 進んでいる方向を向く
		if (std::abs(vx) > 0.1f || std::abs(vz) > 0.1f) {
			float targetAngle = std::atan2(vx, vz);
			// 角度の線形補間
			tc.rotate.y = targetAngle; 
		}
	}
}

void BaseEnemy::Debug() {
}

std::string BaseEnemy::SerializeParameters() {
	nlohmann::json j;
	j["moveType"] = (int)type_;
	j["speed"] = speed_;
	return j.dump();
}

void BaseEnemy::DeserializeParameters(const std::string& data) {
	if (data.empty())
		return;
	try {
		auto j = nlohmann::json::parse(data);
		if (j.contains("moveType"))
			type_ = (MoveType)j["moveType"].get<int>();
		if (j.contains("speed"))
			speed_ = j["speed"].get<float>();
	} catch (...) {
	}
}

void BaseEnemy::SearchTarget(entt::entity entity, GameScene* scene) {
	auto& registry = scene->GetRegistry();
	auto& myTc = registry.get<TransformComponent>(entity);

	entt::entity bestTarget = entt::null;
	float minDistanceSq = searchRange_ * searchRange_;


	// プレイヤーとDefenderの中から一番近いものを探す
	// プレイヤーを探す
	const auto& players = scene->GetEntitiesByTag(TagType::Player);
	for (auto p : players) {
		auto& t = registry.get<TransformComponent>(p);
		float distSq = (t.translate.x - myTc.translate.x) * (t.translate.x - myTc.translate.x) + 
			(t.translate.z - myTc.translate.z) * (t.translate.z - myTc.translate.z);
		if (distSq < minDistanceSq) {
			minDistanceSq = distSq;
			bestTarget = p;
		}
	}

	// 防衛設備（Defender）を探す(プレイヤーより近ければターゲットを上書き)
	const auto& defenders = scene->GetEntitiesByTag(TagType::Defender);
	for (auto d : defenders) {
		auto& t = registry.get<TransformComponent>(d);
		float distSq = (t.translate.x - myTc.translate.x) * (t.translate.x - myTc.translate.x) + 
			(t.translate.z - myTc.translate.z) * (t.translate.z - myTc.translate.z);
		if (distSq < minDistanceSq) {
			minDistanceSq = distSq;
			bestTarget = d;
		}
	}

	// 最優先にすべきタグはCoreなので最後に上書き
	// 防衛設備（Defender）を探す(プレイヤーより近ければターゲットを上書き)
	const auto& core = scene->GetEntitiesByTag(TagType::Core);
	for (auto c : core) {
		auto& t = registry.get<TransformComponent>(c);
		float distSq = (t.translate.x - myTc.translate.x) * (t.translate.x - myTc.translate.x) + 
			(t.translate.z - myTc.translate.z) * (t.translate.z - myTc.translate.z);
		if (distSq < minDistanceSq) {
			minDistanceSq = distSq;
			bestTarget = c;
		}
	}

	currentTarget_ = bestTarget;
}

} // namespace Game
