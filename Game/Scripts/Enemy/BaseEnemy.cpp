#include "BaseEnemy.h"
#include "../../Engine/ThirdParty/nlohmann/json.hpp"
#ifdef USE_IMGUI
#include "../../externals/imgui/imgui.h"
#endif
#include "../ScriptEngine.h"
#include "ObjectTypes.h"
#include "Scenes/GameScene.h"

#include "../PhaseSystemScript.h"

namespace Game {

static bool HasTag(entt::registry& registry, entt::entity entity, TagType tagName) {
	if (!registry.valid(entity) || !registry.all_of<TagComponent>(entity))
		return false;
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
	} else {
		// 攻撃関数を呼んでない時は攻撃判定を消す
		if (registry.all_of<HitboxComponent>(entity)) {
			registry.get<HitboxComponent>(entity).isActive = false;
		}

		DefaultMove(entity, scene, dt);
	}
}

void BaseEnemy::OnDestroy(entt::entity /*entity*/, GameScene* scene) {
	// エンティティがHealthComponentを持っているか確認

	scene->GetEventSystem().Emit("GainExp", expDrop_);

	PhaseSystemScript::PlusCoinCount(10);

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
	ImGui::DragFloat("Drop EXP", &expDrop_, 1.0f, 0.0f, 10000.0f);
	ImGui::DragFloat("Caution Range", &cautionRange_, 1.0f, 0.0f, 200.0f);
	ImGui::DragFloat("Max Wait Time", &maxWaitTime_, 0.1f, 0.0f, 10.0f);
#endif
}

void BaseEnemy::DefaultMove(entt::entity entity, GameScene* scene, float dt) {
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

	// ★追加：アタッカーがタワー(Defender等)に近づいた時の待機処理
	// ターゲットがタワーでなくても、進路上にタワーがあれば警戒する
	if (category_ == Attacker) {
		entt::entity nearestTower = entt::null;
		float minTowerDistSq = 9999999.0f;
		DirectX::XMFLOAT3 towerPos = {};
		float nearestTowerAttackRange = 50.0f; // デフォルトの射程

		TagType towerTags[] = { TagType::Defender, TagType::Canon, TagType::Cannon, TagType::IceCanon, TagType::PipeCannon };
		for (int i = 0; i < 5; ++i) {
			const auto& towers = scene->GetEntitiesByTag(towerTags[i]);
			for (auto t : towers) {
				if (!registry.valid(t) || !registry.all_of<TransformComponent>(t)) continue;
				
				float tAttackRange = cautionRange_; // デフォルトの警戒範囲
				float rawAttackRange = 50.0f;       // デフォルトの射程
				if (registry.all_of<VariableComponent>(t)) {
					float ar = registry.get<VariableComponent>(t).GetValue("AttackRange", 0.0f);
					if (ar > 0.0f) {
						rawAttackRange = ar;
						tAttackRange = ar + 25.0f; // 射程 + 25m を警戒範囲とする
					}
				}

				auto& tTc = registry.get<TransformComponent>(t);
				float dx = tTc.translate.x - tc.translate.x;
				float dz = tTc.translate.z - tc.translate.z;
				float distSq = dx * dx + dz * dz;
				
				// タワーの警戒範囲内であれば候補とする
				if (distSq < tAttackRange * tAttackRange) {
					if (distSq < minTowerDistSq) {
						minTowerDistSq = distSq;
						nearestTower = t;
						towerPos = tTc.translate;
						nearestTowerAttackRange = rawAttackRange;
					}
				}
			}
		}

		// 警戒範囲内にタワーがあった場合
		if (registry.valid(nearestTower)) {
			float dist = std::sqrt(minTowerDistSq);
			if (dist > attackRange_) {
				// 自分よりタワーに近いタンクがいるかチェック
				bool isTankAhead = false;
				bool isTankComing = false;

				const auto& enemies = scene->GetEntitiesByTag(TagType::Enemy);
				for (auto e : enemies) {
					if (e == entity) continue;
					if (registry.all_of<VariableComponent>(e)) {
						auto& vc = registry.get<VariableComponent>(e);
						if (vc.GetValue("EnemyCategory") == (float)Tank) {
							auto& tankTc = registry.get<TransformComponent>(e);
							float tankDx = towerPos.x - tankTc.translate.x;
							float tankDz = towerPos.z - tankTc.translate.z;
							float tankDist = std::sqrt(tankDx * tankDx + tankDz * tankDz);
							
							// タンクが自分よりタワーに十分に近ければOK (例: 射程内に入っている、あるいは15m以上前にいる)
							if (tankDist < nearestTowerAttackRange || tankDist < dist - 15.0f) {
								isTankAhead = true;
								break;
							}
							
							// タンクがこのタワー周辺（警戒範囲より少し外まで）にいるなら諦めずに待つ
							if (tankDist < nearestTowerAttackRange + 55.0f) {
								isTankComing = true;
							}
						}
					}
				}

				// タンクが前にいない場合、指定秒数だけ待機する
				if (!isTankAhead) {
					if (isTankComing) {
						currentWaitTime_ = 0.0f; // タンクが来る予定なら待機時間をリセットし続ける
						dirX = 0.0f;
						dirZ = 0.0f;
					} else {
						if (currentWaitTime_ < maxWaitTime_) {
							currentWaitTime_ += dt;
							dirX = 0.0f;
							dirZ = 0.0f;
						}
						// maxWaitTime_ を超えたら諦めて進む
					}
				} else {
					// タンクが前に出たら待機時間をリセット（別のタワーに備える）
					currentWaitTime_ = 0.0f;
				}
			} else {
				// 攻撃範囲に入ったら待機時間をリセット
				currentWaitTime_ = 0.0f;
			}
		} else {
			// 警戒範囲内にタワーがなければリセット
			currentWaitTime_ = 0.0f;
		}
	}

	// 3. 物理コンポーネントがあるかチェック
	if (registry.all_of<RigidbodyComponent>(entity)) {
		auto& rb = registry.get<RigidbodyComponent>(entity);
		// ★追加: バフによる移動速度の計算
		float currentSpeed = speed_;
		if (registry.all_of<VariableComponent>(entity)) {
			auto& vc = registry.get<VariableComponent>(entity);
			float buffTimer = vc.GetValue("SpeedBuffTimer", 0.0f);
			if (buffTimer > 0.0f) {
				currentSpeed = speed_ * vc.GetValue("SpeedBuffMultiplier", 1.0f);
				vc.SetValue("SpeedBuffTimer", buffTimer - dt); // タイマーを減らす
			}
		}
		// 移動速度を計算
		float vx = dirX * currentSpeed;
		float vz = dirZ * currentSpeed;

		if (type_ == Walk) {
			// 地面を歩くタイプ
			rb.velocity.x = vx;
			rb.velocity.z = vz;

			// ==== 物理エンジンに逆らわない地形追従 ====
			// 自由落下で下っている(速度 <= 0.0f) かつ、地面より下に沈んだ場合のみ上に押し上げる
			// 毎フレームの地形RayCastは重いため、確率で間引く
			float h = groundHeight_;
			if (rand() % 5 == 0) {
				h = scene->GetHeightAt(tc.translate.x, tc.translate.z, tc.translate.y, static_cast<uint32_t>(entity));
				if (h > -5000.0f) groundHeight_ = h;
			}
			
			if (h > -5000.0f) {
				float footY = tc.translate.y - 1.0f; // 脚元の高さ
				// 重力で落下中、もしくはめり込んでいる場合
				if (rb.velocity.y <= 0.01f && footY <= h + 0.05f) {
					tc.translate.y = h + 1.0f; // 脚の長さを保証
					rb.velocity.y = 0.0f;      // 重力を打ち消して接地面で止める
				}
			}
		} else if (type_ == Fly) {
			// 飛行タイプ
			rb.velocity.x = vx;
			rb.velocity.z = vz;
		}

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
	j["expDrop"] = expDrop_;
	j["cautionRange"] = cautionRange_;
	j["maxWaitTime"] = maxWaitTime_;
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
		if (j.contains("expDrop"))
			expDrop_ = j["expDrop"].get<float>();
		if (j.contains("cautionRange"))
			cautionRange_ = j["cautionRange"].get<float>();
		if (j.contains("maxWaitTime"))
			maxWaitTime_ = j["maxWaitTime"].get<float>();
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
		float distSq = (t.translate.x - myTc.translate.x) * (t.translate.x - myTc.translate.x) + (t.translate.z - myTc.translate.z) * (t.translate.z - myTc.translate.z);
		if (distSq < minDistanceSq) {
			minDistanceSq = distSq;
			bestTarget = p;
		}
	}

	// 防衛設備（DefenderやCanonなど）を探す(プレイヤーより近ければターゲットを上書き)
	TagType towerTags[] = { TagType::Defender, TagType::Canon, TagType::Cannon, TagType::IceCanon, TagType::PipeCannon };
	for (int i = 0; i < 5; ++i) {
		const auto& towers = scene->GetEntitiesByTag(towerTags[i]);
		for (auto d : towers) {
			if (!registry.valid(d) || !registry.all_of<TransformComponent>(d)) continue;
			
			auto& t = registry.get<TransformComponent>(d);
			float distSq = (t.translate.x - myTc.translate.x) * (t.translate.x - myTc.translate.x) + 
			               (t.translate.z - myTc.translate.z) * (t.translate.z - myTc.translate.z);
			
			if (distSq < minDistanceSq) {
				minDistanceSq = distSq;
				bestTarget = d;
			}
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

void BaseEnemy::SetCategory(entt::entity entity, GameScene* scene, EnemyCategory category) {
	category_ = category;
	auto& registry = scene->GetRegistry();
	if (!registry.all_of<VariableComponent>(entity)) {
		registry.emplace<VariableComponent>(entity);
	}
	registry.get<VariableComponent>(entity).SetValue("EnemyCategory", (float)category_);
}

} // namespace Game
