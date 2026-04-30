#include "PlayerScript.h"
#ifdef USE_IMGUI
#include "../../externals/imgui/imgui.h"
#endif
#include "ObjectTypes.h"
#include "PhaseSystemScript.h"
#include "BulletScript.h"
#include "Scenes/GameScene.h"
#include "ScriptEngine.h"
#include "../Systems/UISystem.h"
#include <cmath>
#include <iostream>
#include <algorithm>
#include <cstdlib>

namespace Game {

void PlayerScript::Start(entt::entity entity, GameScene* scene) {
	// ★追加: プレイヤーを物理演算から切り離し、CharacterController方式（レイキャスト）で制御する
	if (scene->GetRegistry().all_of<RigidbodyComponent>(entity)) {
		scene->GetRegistry().get<RigidbodyComponent>(entity).isKinematic = true;
	}
	if (scene->GetRegistry().all_of<CharacterMovementComponent>(entity)) {
		auto& cm = scene->GetRegistry().get<CharacterMovementComponent>(entity);
		cm.heightOffset = 1.0f; // 2m立方体キャラの中心がy=1.0になるように
		cm.jumpPower = jumpPower_; // ★追加: ヘッダで定義されているジャンプ力(8.0f)を反映し、ジャンプを弱くする
	}

	// プレイヤー自身のコライダーサイズを「見た目（2m立方体）」に合わせる
	if (scene->GetRegistry().all_of<BoxColliderComponent>(entity)) {
		scene->GetRegistry().get<BoxColliderComponent>(entity).size = { 2.0f, 2.0f, 2.0f };
	}
	
	// 食らい判定（Hurtbox）がなければ追加し、サイズとタグを設定する
	if (!scene->GetRegistry().all_of<HurtboxComponent>(entity)) {
		auto& hb = scene->GetRegistry().emplace<HurtboxComponent>(entity);
		hb.size = { 2.0f, 2.0f, 2.0f };
		hb.tag = TagType::Player;
	} else {
		auto& hb = scene->GetRegistry().get<HurtboxComponent>(entity);
		hb.size = { 2.0f, 2.0f, 2.0f };
		hb.tag = TagType::Player;
	}

	// 体力（Health）がなければ追加する
	if (!scene->GetRegistry().all_of<HealthComponent>(entity)) {
		auto& hc = scene->GetRegistry().emplace<HealthComponent>(entity);
		hc.hp = 100.0f;
		hc.maxHp = 100.0f;
	}

	// 剣がシーンに既にあるか確認
	entt::entity sword = entt::null;
	auto view = scene->GetRegistry().view<NameComponent>();
	for (auto e : view) {
		if (view.get<NameComponent>(e).name == swordName_) {
			sword = e;
			break;
		}
	}

	if (sword == entt::null) {
		// 剣がなければ生成
		sword = scene->GetRegistry().create();
		scene->GetRegistry().emplace<NameComponent>(sword).name = swordName_;
		auto& tc = scene->GetRegistry().emplace<TransformComponent>(sword);
		tc.scale = { 0.15f, 0.15f, 1.8f };
		
		auto& cc = scene->GetRegistry().emplace<ColorComponent>(sword);
		cc.color = { 0.9f, 0.9f, 0.9f, 1.0f };

		auto& tcTag = scene->GetRegistry().emplace<TagComponent>(sword);
		tcTag.tag = TagType::PlayerSword;

		// ★追加: 親子関係の設定
		auto& hierarchy = scene->GetRegistry().emplace<HierarchyComponent>(sword);
		hierarchy.parentId = entity;

		// ★追加: モーションコンポーネントの追加
		auto& motion = scene->GetRegistry().emplace<MotionComponent>(sword);
		motion.isPlaying = false;
		motion.activeClip = ""; // ★追加: デフォルトクリップによるTransform上書きを防ぐ

		auto* renderer = scene->GetRenderer();
		if (renderer) {
			auto& mr = scene->GetRegistry().emplace<MeshRendererComponent>(sword);
			mr.modelHandle = renderer->LoadObjMesh("Resources/Models/cube/cube.obj");
			mr.textureHandle = renderer->LoadTexture2D("Resources/Textures/white1x1.png"); // 必要ならテクスチャも指定可
			mr.color = { 0.9f, 0.9f, 0.9f, 1.0f };
			mr.enabled = true;
		}

		auto& hb = scene->GetRegistry().emplace<HitboxComponent>(sword);
		hb.isActive = false;
		hb.damage = 30.0f;
		hb.tag = TagType::Sword;
		hb.size = { 3.0f, 3.0f, 1.0f }; 
		hb.center = { 0.0f, 0.0f, 0.5f };
		hb.enabled = true;
	} else {
		// 既にタグがない場合は追加
		if (!scene->GetRegistry().all_of<TagComponent>(sword)) {
			scene->GetRegistry().emplace<TagComponent>(sword).tag = TagType::PlayerSword;
		}
		// ★追加: 親子関係の確認・設定
		if (!scene->GetRegistry().all_of<HierarchyComponent>(sword)) {
			auto& hc = scene->GetRegistry().emplace<HierarchyComponent>(sword);
			hc.parentId = entity;
		} else {
			auto& hc = scene->GetRegistry().get<HierarchyComponent>(sword);
			hc.parentId = entity; // デシリアライズ後などはIDが変わっている可能性があるため強制上書き
		}
		// ★追加: モーションコンポーネントの確認・設定
		if (!scene->GetRegistry().all_of<MotionComponent>(sword)) {
			auto& motion = scene->GetRegistry().emplace<MotionComponent>(sword);
			motion.isPlaying = false;
			motion.activeClip = ""; // ★追加: デフォルトクリップによるTransform上書きを防ぐ
		}

		// 既にある場合は基本的なプロパティを維持
		scene->GetRegistry().get<TransformComponent>(sword).scale = { 0.15f, 0.15f, 1.8f };

		// ★最重要: Hitbox がなければ追加する
		if (!scene->GetRegistry().all_of<HitboxComponent>(sword)) {
			auto& hb = scene->GetRegistry().emplace<HitboxComponent>(sword);
			hb.isActive = false;
			hb.damage = 30.0f;
			hb.tag = TagType::Sword;
			hb.size = { 3.0f, 3.0f, 1.0f }; 
			hb.center = { 0.0f, 0.0f, 0.5f };
			hb.enabled = true;
		}

		if (scene->GetRegistry().all_of<MeshRendererComponent>(sword)) {
			auto& mr = scene->GetRegistry().get<MeshRendererComponent>(sword);
			auto* renderer = scene->GetRenderer();
			if (renderer) {
				mr.modelHandle = renderer->LoadObjMesh("Resources/Models/cube/cube.obj");
				mr.textureHandle = renderer->LoadTexture2D("Resources/Textures/white1x1.png");
				mr.enabled = true;
			}
		}
	}

	if (scene->GetRegistry().all_of<NameComponent>(entity)) {
		std::cout << "PlayerScript Started: " << scene->GetRegistry().get<NameComponent>(entity).name << std::endl;
	}

	// ★追加: 剣にコンボモーションがない場合はデフォルトで作成
	sword = scene->FindObjectByName(swordName_);
	if (sword != entt::null) {
		if (auto* motion = scene->GetRegistry().try_get<MotionComponent>(sword)) {
			auto setupDefaultCombo = [&](const std::string& name, int index) {
				if (motion->clips.find(name) == motion->clips.end()) {
					MotionComponent::MotionClip clip;
					clip.name = name;
					clip.totalDuration = 0.4f;
					clip.loop = false;
					
					if (index == 1) { // 横薙ぎ（より広く、伸縮追加）
						clip.keyframes.push_back({0.00f, { 1.2f, 0.8f, -0.3f }, { 0.0f, 1.57f, 0.0f }, { 0.15f, 0.15f, 1.8f }});
						clip.keyframes.push_back({0.20f, { 0.0f, 0.8f, 1.8f }, { 0.0f, 0.0f, 0.0f }, { 0.15f, 0.15f, 2.5f }});
						clip.keyframes.push_back({0.40f, {-1.2f, 0.8f, -0.3f }, { 0.0f, -1.57f, 0.0f }, { 0.15f, 0.15f, 1.8f }});
					} else if (index == 2) { // 斜め斬り（右上から左下）
						clip.keyframes.push_back({0.00f, { 1.2f, 2.0f, -0.5f }, { -0.7f, 0.0f, 0.7f }, { 0.15f, 0.15f, 1.8f }});
						clip.keyframes.push_back({0.20f, { 0.0f, 1.0f, 2.0f }, { 0.0f, 0.0f, 0.0f }, { 0.15f, 0.15f, 2.5f }});
						clip.keyframes.push_back({0.40f, { -1.2f, -0.5f, 0.2f }, { 0.7f, 0.0f, -0.7f }, { 0.15f, 0.15f, 1.8f }});
					} else { // 突き/回転（超推力・多回転）
						clip.keyframes.push_back({0.00f, { 0.0f, 0.8f, 0.5f }, { 0.0f, 0.0f, 0.0f }, { 0.15f, 0.15f, 1.8f }});
						clip.keyframes.push_back({0.15f, { 0.0f, 0.8f, 2.5f }, { 0.0f, 0.0f, 6.28f }, { 0.15f, 0.15f, 3.0f }});
						clip.keyframes.push_back({0.40f, { 0.0f, 0.8f, 1.0f }, { 0.0f, 0.0f, 12.56f }, { 0.15f, 0.15f, 1.8f }});
					}
					motion->clips[name] = clip;
				}
			};
			setupDefaultCombo("Combo1", 1);
			setupDefaultCombo("Combo2", 2);
			setupDefaultCombo("Combo3", 3);

			// ★追加: ソードスキルのモーション
			if (motion->clips.find("SwordSkill") == motion->clips.end()) {
				MotionComponent::MotionClip clip;
				clip.name = "SwordSkill";
				clip.totalDuration = 0.6f;
				clip.loop = false;
				clip.keyframes.push_back({0.0f, {0.0f, 0.8f, 1.0f}, {0.0f, 0.0f, 0.0f}, {0.15f, 0.15f, 2.5f}});
				clip.keyframes.push_back({0.15f, {0.0f, 0.8f, 1.0f}, {0.0f, 6.28f, 0.0f}, {0.15f, 0.15f, 2.5f}});
				clip.keyframes.push_back({0.3f, {0.0f, 0.8f, 1.0f}, {0.0f, 12.56f, 0.0f}, {0.15f, 0.15f, 2.5f}});
				clip.keyframes.push_back({0.45f, {0.0f, 0.8f, 1.0f}, {0.0f, 18.84f, 0.0f}, {0.15f, 0.15f, 2.5f}});
				clip.keyframes.push_back({0.6f, {0.0f, 0.8f, 1.0f}, {0.0f, 25.12f, 0.0f}, {0.15f, 0.15f, 2.5f}});
				motion->clips["SwordSkill"] = clip;
			}
		}
	}

	// ★追加: 銃の生成
	entt::entity gun = scene->FindObjectByName(gunName_);
	if (gun == entt::null) {
		gun = scene->GetRegistry().create();
		scene->GetRegistry().emplace<NameComponent>(gun).name = gunName_;
		auto& tc = scene->GetRegistry().emplace<TransformComponent>(gun);
		tc.scale = { 0.1f, 0.1f, 0.8f };
		
		auto& cc = scene->GetRegistry().emplace<ColorComponent>(gun);
		cc.color = { 0.2f, 0.2f, 0.2f, 1.0f };

		scene->GetRegistry().emplace<TagComponent>(gun).tag = TagType::Untagged;
		scene->GetRegistry().emplace<HierarchyComponent>(gun).parentId = entity;

		auto* renderer = scene->GetRenderer();
		if (renderer) {
			auto& mr = scene->GetRegistry().emplace<MeshRendererComponent>(gun);
			mr.modelHandle = renderer->LoadObjMesh("Resources/Models/cube/cube.obj");
			mr.textureHandle = renderer->LoadTexture2D("Resources/Textures/white1x1.png");
			mr.color = { 0.2f, 0.2f, 0.2f, 1.0f };
			mr.enabled = false;
		}
	} else {
		if (!scene->GetRegistry().all_of<HierarchyComponent>(gun)) {
			scene->GetRegistry().emplace<HierarchyComponent>(gun).parentId = entity;
		} else {
			scene->GetRegistry().get<HierarchyComponent>(gun).parentId = entity;
		}
		if (scene->GetRegistry().all_of<MeshRendererComponent>(gun)) {
			scene->GetRegistry().get<MeshRendererComponent>(gun).enabled = false;
		}
	}
}

void PlayerScript::Update(entt::entity entity, GameScene* scene, float dt) {
	if (scene->GetRegistry().all_of<HealthComponent>(entity)) {
		if (scene->GetRegistry().get<HealthComponent>(entity).isDead) return;
	}

	if (!isSubscribed_) {
		scene->GetEventSystem().Subscribe("GainExp", [this, scene](float amount) {
			experience_ += amount;
			if (experience_ >= nextExperience_) {
				experience_ -= nextExperience_;
				level_++;
				nextExperience_ *= 1.5f;
				scene->GetEventSystem().Emit("GainSkillPoint", 1.0f);
			}
			debugReceiveCount_ += 1;
			debugLastValue_ = amount;
		});
		scene->GetEventSystem().Subscribe("PlayerSwordHit", [this, scene](float) {
			if (!scene || !scene->GetContext().camera) return;
			auto* cam = scene->GetContext().camera;
			if (comboCount_ <= 1) {
				cam->StartShake(0.1f, 0.15f); // 弱
			} else if (comboCount_ == 2) {
				cam->StartShake(0.15f, 0.35f); // 中
			} else {
				cam->StartShake(0.2f, 0.6f);  // 強
			}
		});
		debugSubscribeCount_ += 1;
		isSubscribed_ = true;
	}

    bool hasPhaseSystem = false;
	{
		auto scView = scene->GetRegistry().view<ScriptComponent>();
		for (auto e : scView) {
			auto& sc = scView.get<ScriptComponent>(e);
			for (auto& entry : sc.scripts) {
              if (entry.scriptPath == "PhaseSystemScript" || entry.scriptPath == "TutorialScript") {
					hasPhaseSystem = true;
					break;
				}
			}
			if (hasPhaseSystem) break;
		}
	}

	if (skillCooldown_ > 0.0f) skillCooldown_ -= dt;
	if (gunShootTimer_ > 0.0f) gunShootTimer_ -= dt;

	bool currentSwitchKeyDown = (GetAsyncKeyState('T') & 0x8000) != 0;
	if (currentSwitchKeyDown && !prevPlayerSwitchKeyDown_) {
		SwitchPlayerType(entity, scene);
	}
	prevPlayerSwitchKeyDown_ = currentSwitchKeyDown;

	bool currentSkillKeyDown = (GetAsyncKeyState('E') & 0x8000) != 0;
	if (currentSkillKeyDown && !prevSkillKeyDown_) {
		ExecuteSkill(entity, scene);
	}
	prevSkillKeyDown_ = currentSkillKeyDown;

	// ★追加: カーソル表示切り替え (Left Altキー)
	bool currentCursorToggle = (GetAsyncKeyState(VK_LMENU) & 0x8000) != 0;
	if (currentCursorToggle && !prevCursorToggle_) {
		isCursorVisible_ = !isCursorVisible_;
		if (isCursorVisible_) {
			while (ShowCursor(TRUE) < 0);
		} else {
			while (ShowCursor(FALSE) >= 0);
		}
	}
	prevCursorToggle_ = currentCursorToggle;

	// ★追加: カーソル非表示時は画面中心に固定
	if (!isCursorVisible_) {
		HWND hwnd = GetActiveWindow();
		if (hwnd) {
			RECT rect;
			GetClientRect(hwnd, &rect);
			POINT pt = { (rect.right - rect.left) / 2, (rect.bottom - rect.top) / 2 };
			ClientToScreen(hwnd, &pt);
			SetCursorPos(pt.x, pt.y);
		}
	}

	if (hasPhaseSystem && PhaseSystemScript::IsPhase() == PhaseSystemScript::PreparationPhase) {
		if (scene->GetRegistry().all_of<CameraTargetComponent>(entity)) scene->GetRegistry().get<CameraTargetComponent>(entity).enabled = false;
		if (scene->GetRegistry().all_of<PlayerInputComponent>(entity))  scene->GetRegistry().get<PlayerInputComponent>(entity).enabled = false;
		UpdateSword(entity, scene, dt);
		UpdateGun(entity, scene, dt);
	} else {
		UpdateMovement(entity, scene, dt);
		if (playerType_ == PlayerType::Sword) {
			UpdateAttack(entity, scene, dt);
			UpdateSword(entity, scene, dt);
		} else {
			UpdateGunAttack(entity, scene, dt);
			UpdateGun(entity, scene, dt);
		}
		if (scene->GetRegistry().all_of<CameraTargetComponent>(entity)) scene->GetRegistry().get<CameraTargetComponent>(entity).enabled = true;
		if (scene->GetRegistry().all_of<PlayerInputComponent>(entity))  scene->GetRegistry().get<PlayerInputComponent>(entity).enabled = true;
	}

	// Update 内での ImGui 呼び出しは例外の原因となる可能性があるため、OnEditorUI に移動しました。
}

void PlayerScript::UpdateMovement(entt::entity entity, GameScene* scene, float /*dt*/) {
	if (!scene->GetRegistry().all_of<PlayerInputComponent>(entity)) return;
	auto& input = scene->GetRegistry().get<PlayerInputComponent>(entity);
	
	if (gunComboAnimTimer_ > 0.0f) {
		input.moveDir.x = 0.0f;
		input.moveDir.y = 0.0f;
		return;
	}

	float speedMul = 1.0f;
	if (isAttacking_) {
		speedMul = 0.2f;
	}
	if (isAiming_ && playerType_ == PlayerType::Gun) {
		speedMul = 0.4f;
	}
	input.moveDir.x *= speedMul;
	input.moveDir.y *= speedMul;
}

void PlayerScript::UpdateAttack(entt::entity /*entity*/, GameScene* scene, float /*dt*/) {
	bool currentAttackKeyDown = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
	if (currentAttackKeyDown && !prevAttackKeyDown_) {
		attackQueued_ = true;
	}
	prevAttackKeyDown_ = currentAttackKeyDown;

	entt::entity sword = scene->FindObjectByName(swordName_);
	if (sword == entt::null) return;

	auto& motion = scene->GetRegistry().get<MotionComponent>(sword);

	if (isAttacking_) {
		sheatheTimer_ = 0.0f;
		isSheathed_ = false;

		if (!motion.isPlaying) {
			if (attackQueued_ && comboCount_ < 3) {
				comboCount_++;
				attackQueued_ = false;
				motion.PlayAnimation("Combo" + std::to_string(comboCount_));
			} else {
				isAttacking_ = false;
				comboCount_ = 0;
				attackQueued_ = false;
			}
		}
	} else {
		if (attackQueued_) {
			isAttacking_ = true;
			isSheathed_ = false;
			comboCount_ = 1;
			attackQueued_ = false;
			motion.PlayAnimation("Combo1");
		} else {
			if (!isSheathed_) {
				sheatheTimer_ += scene->GetContext().dt;
				if (sheatheTimer_ >= AUTO_SHEATHE_TIME) {
					isSheathed_ = true;
				}
			}
		}
	}
}

void PlayerScript::UpdateSword(entt::entity /*entity*/, GameScene* scene, float dt) {
	entt::entity sword = scene->FindObjectByName(swordName_);
	if (sword == entt::null) return;

	auto& swordTc = scene->GetRegistry().get<TransformComponent>(sword);
	
	if (!isAttacking_) {
		// 非攻撃時は常に背中に背負う配置にする
		swordTc.translate = { -0.3f, 1.0f, -0.4f };
		swordTc.rotate = { DirectX::XMConvertToRadians(35.0f), 0.0f, DirectX::XMConvertToRadians(25.0f) };
		swordTc.scale = { 0.15f, 0.15f, 1.8f }; // ★待機中の細長さ（スケール）を維持する

		// ★MotionSystemによる待機中のスケール＆座標リセットを防ぐため、再生クリップを空にする
		if (auto* motion = scene->GetRegistry().try_get<MotionComponent>(sword)) {
			motion->activeClip = "";
		}

		isSheathed_ = true;
		sheatheTimer_ = AUTO_SHEATHE_TIME;
	}

	if (auto* motion = scene->GetRegistry().try_get<MotionComponent>(sword)) {
		if (motion->isPlaying) {
			auto it = motion->clips.find(motion->activeClip);
			if (it != motion->clips.end()) {
				float duration = it->second.totalDuration;
				float t = motion->currentTime;
				float start = duration * 0.10f;
				float end = duration * 0.85f;
				bool active = (t > start && t < end);
				if (auto* hb = scene->GetRegistry().try_get<HitboxComponent>(sword)) {
					hb->isActive = active;
				}
			}
		} else {
			if (auto* hb = scene->GetRegistry().try_get<HitboxComponent>(sword)) {
				hb->isActive = false;
			}
		}
	}

	bool hitboxActive = false;
	if (scene->GetRegistry().all_of<HitboxComponent>(sword)) {
		hitboxActive = scene->GetRegistry().get<HitboxComponent>(sword).isActive;
	}

	if (isAttacking_ && hitboxActive) {
		Engine::Matrix4x4 worldMat = scene->GetWorldMatrix((int)sword);
		DirectX::XMMATRIX m = DirectX::XMLoadFloat4x4(reinterpret_cast<DirectX::XMFLOAT4X4*>(&worldMat));
		float bladeLen = 1.6f;
		DirectX::XMVECTOR basePos = DirectX::XMVector3TransformCoord(DirectX::XMVectorSet(0, 0, -bladeLen*0.2f, 1), m);
		DirectX::XMVECTOR tipPos = DirectX::XMVector3TransformCoord(DirectX::XMVectorSet(0, 0, bladeLen*0.8f, 1), m);

		TrailPoint tp;
		DirectX::XMStoreFloat3(reinterpret_cast<DirectX::XMFLOAT3*>(&tp.base), basePos);
		DirectX::XMStoreFloat3(reinterpret_cast<DirectX::XMFLOAT3*>(&tp.tip), tipPos);
		tp.life = 0.35f;
		tp.maxLife = 0.35f;
		trailPoints_.push_back(tp);
		if (trailPoints_.size() > 60) trailPoints_.pop_front();
	}

	for (auto& tp : trailPoints_) tp.life -= dt;
	while (!trailPoints_.empty() && trailPoints_.front().life <= 0) trailPoints_.pop_front();

	auto* renderer = scene->GetRenderer();
	if (renderer && trailPoints_.size() >= 2) {
		for (size_t i = 1; i < trailPoints_.size(); ++i) {
			float alpha = trailPoints_[i].life / trailPoints_[i].maxLife;
			Engine::Vector4 col = {0.2f, 0.6f, 1.0f, alpha * 0.95f};
			// 少しずらした線を追加して「厚み」を出す
			Engine::Vector3 tip1 = trailPoints_[i-1].tip;
			Engine::Vector3 tip2 = trailPoints_[i].tip;
			Engine::Vector3 base1 = trailPoints_[i-1].base;
			Engine::Vector3 base2 = trailPoints_[i].base;

			renderer->DrawLine3D(tip1, tip2, col, true);
			renderer->DrawLine3D(base1, base2, col, true);
			renderer->DrawLine3D(tip1, base1, col, true);

			Engine::Vector3 off = { 0.0f, 0.03f, 0.0f };
			renderer->DrawLine3D(tip1 + off, tip2 + off, col, true);
			renderer->DrawLine3D(base1 + off, base2 + off, col, true);
			renderer->DrawLine3D(tip1 - off, tip2 - off, col, true);
			renderer->DrawLine3D(base1 - off, base2 - off, col, true);
		}
	}
}

void PlayerScript::UpdateGun(entt::entity /*entity*/, GameScene* scene, float /*dt*/) {
	entt::entity gun = scene->FindObjectByName(gunName_);
	if (gun == entt::null) return;

	auto& gunTc = scene->GetRegistry().get<TransformComponent>(gun);
	
	if (isAiming_) {
		// エイム時：構える
		gunTc.translate = { 0.4f, 1.2f, 0.8f };
		gunTc.rotate = { 0.0f, 0.0f, 0.0f };
	} else {
		// 非エイム時：腰だめ
		gunTc.translate = { 0.5f, 0.8f, 0.5f };
		gunTc.rotate = { 0.0f, 0.0f, 0.0f };
	}
}

void PlayerScript::UpdateGunAttack(entt::entity entity, GameScene* scene, float dt) {
	isAiming_ = (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0;
	bool currentAttackKeyDown = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;

	// ★追加: ロック機能中かつ攻撃アクション中は、プレイヤーが必ず敵の方を向くようにする
	if (scene->GetRegistry().all_of<CameraTargetComponent>(entity)) {
		auto& ct = scene->GetRegistry().get<CameraTargetComponent>(entity);
		if (ct.lockedTarget != entt::null && scene->GetRegistry().valid(ct.lockedTarget)) {
			bool isActionActive = gunComboAnimTimer_ > 0.0f || gunShootTimer_ > 0.0f || currentAttackKeyDown;
			if (isActionActive) {
				auto& pTc = scene->GetRegistry().get<TransformComponent>(entity);
				auto& eTc = scene->GetRegistry().get<TransformComponent>(ct.lockedTarget);
				float dx = eTc.translate.x - pTc.translate.x;
				float dz = eTc.translate.z - pTc.translate.z;
				float targetYaw = std::atan2(dx, dz);
				
				// 高速で敵の方へ向き直る
				float diff = targetYaw - pTc.rotate.y;
				while (diff >  DirectX::XM_PI) diff -= DirectX::XM_2PI;
				while (diff < -DirectX::XM_PI) diff += DirectX::XM_2PI;
				pTc.rotate.y += diff * std::min(1.0f, 30.0f * dt);
			}
		}
	}

	if (gunComboResetTimer_ > 0.0f) {
		gunComboResetTimer_ -= dt;
		if (gunComboResetTimer_ <= 0.0f && gunComboAnimTimer_ <= 0.0f) {
			gunComboStep_ = 0;
		}
	}

	if (currentAttackKeyDown && !prevAttackKeyDown_) { // クリック時のみ進行
		if (gunShootTimer_ <= 0.0f && gunComboAnimTimer_ <= 0.0f) {
			gunComboStep_++;
			if (gunComboStep_ > 3) gunComboStep_ = 1;
			
			if (gunComboStep_ == 1) {
				gunShootTimer_ = 0.3f;
				gunComboResetTimer_ = 1.0f;
				ShootGun(entity, scene); // 1回目：普通に射撃
			} else if (gunComboStep_ == 2) {
				gunShootTimer_ = 0.6f;
				gunComboResetTimer_ = 1.5f;
				gunComboAnimTimer_ = 0.6f;
				// 2回目：反復横跳び（アニメーション中に自動で連射する）
			} else if (gunComboStep_ == 3) {
				gunShootTimer_ = 0.8f;
				gunComboResetTimer_ = 1.5f;
				gunComboAnimTimer_ = 0.4f; // 0.4秒の短く鋭いダッシュ
				
				// ★追加: 3段目発動時のA/Dキー入力で回り込む方向を決定する
				bool isA_Pressed = (GetAsyncKeyState('A') & 0x8000) != 0;
				bool isD_Pressed = (GetAsyncKeyState('D') & 0x8000) != 0;
				if (isA_Pressed) {
					gunCombo3Dir_ = -1.0f; // 左へ回り込む
				} else if (isD_Pressed) {
					gunCombo3Dir_ = 1.0f; // 右へ回り込む
				} else {
					gunCombo3Dir_ = 1.0f; // 指定がなければデフォルトは右
				}
			}
		}
	}
	prevAttackKeyDown_ = currentAttackKeyDown;

	if (gunComboAnimTimer_ > 0.0f) {
		gunComboAnimTimer_ -= dt;
		auto& pTc = scene->GetRegistry().get<TransformComponent>(entity);

		if (gunComboStep_ == 2) {
			// 左右に反復横跳び
			float progress = 1.0f - (gunComboAnimTimer_ / 0.6f);
			float rightX = std::cos(pTc.rotate.y);
			float rightZ = -std::sin(pTc.rotate.y);
			
			// 左右に素早く揺れる (cos波の速度で位置をズラす、振幅を2倍に)
			float oscillationSpeed = std::cos(progress * DirectX::XM_PI * 6.0f) * 50.0f; 
			pTc.translate.x += rightX * oscillationSpeed * dt;
			pTc.translate.z += rightZ * oscillationSpeed * dt;

			// アニメーション中に連射
			static float rapidTimer2 = 0.0f;
			rapidTimer2 -= dt;
			if (rapidTimer2 <= 0.0f) {
				ShootGun(entity, scene);
				rapidTimer2 = (gunType_ == GunType::Shotgun) ? 0.35f : 0.08f;
			}
		} else if (gunComboStep_ == 3) {
			// ★変更: 入力された方向（左or右）へ大きく回り込む高速ダッシュ
			// 常に敵の方を向いているので、横へ移動すると自動的に敵を中心に円弧を描く動きになる
			
			// gunCombo3Dir_ を掛けて右(1.0)か左(-1.0)かを決定
			float sideX = std::cos(pTc.rotate.y) * gunCombo3Dir_;
			float sideZ = -std::sin(pTc.rotate.y) * gunCombo3Dir_;
			
			float dashSpeed = 40.0f; 
			
			// アニメーションの最初と最後で減速させる（イージング）
			float t = 1.0f - (gunComboAnimTimer_ / 0.4f); // 0.0 ~ 1.0
			float currentSpeed = dashSpeed * std::sin(t * DirectX::XM_PI); // サイン波で滑らかに加速・減速

			pTc.translate.x += sideX * currentSpeed * dt;
			pTc.translate.z += sideZ * currentSpeed * dt;

			// ダッシュ中に猛烈な連射
			static float rapidTimer3 = 0.0f;
			rapidTimer3 -= dt;
			if (rapidTimer3 <= 0.0f) {
				ShootGun(entity, scene);
				rapidTimer3 = (gunType_ == GunType::Shotgun) ? 0.35f : 0.08f; 
			}
		}
	}

	// ★追加: コンボダッシュ中に残像を生成
	if (gunComboAnimTimer_ > 0.0f) {
		afterImageTimer_ -= dt;
		if (afterImageTimer_ <= 0.0f) {
			auto& pTc2 = scene->GetRegistry().get<TransformComponent>(entity);
			AfterImage ai;
			ai.pos = pTc2.translate;
			ai.rotate = pTc2.rotate;
			ai.scale = pTc2.scale;
			ai.life = 0.2f;
			ai.maxLife = 0.2f;
			afterImages_.push_back(ai);
			if (afterImages_.size() > 15) afterImages_.pop_front();
			afterImageTimer_ = 0.03f; // 約30FPSで残像を発生
		}
	}
}

void PlayerScript::ShootGun(entt::entity entity, GameScene* scene) {
	if (gunType_ == GunType::AssaultRifle) {
		// AR: 威力15、寿命2.0秒（長射程）
		SpawnBullet(entity, scene, 0.0f, 0.0f, 15.0f, 2.0f);
	} else if (gunType_ == GunType::Shotgun) {
		// SG: 威力4x5発=20、寿命0.2秒（約16mで消滅する超短射程）、拡散範囲を広く
		for (int i = 0; i < 5; ++i) {
			float spreadYaw = (rand() % 100 / 100.0f - 0.5f) * 0.4f; 
			float spreadPitch = (rand() % 100 / 100.0f - 0.5f) * 0.4f;
			SpawnBullet(entity, scene, spreadYaw, spreadPitch, 4.0f, 0.2f);
		}
	}
}

void PlayerScript::SpawnBullet(entt::entity entity, GameScene* scene, float spreadYaw, float spreadPitch, float damage, float lifeTime) {
	if (!scene->GetRegistry().all_of<TransformComponent>(entity)) return;
	auto& pTc = scene->GetRegistry().get<TransformComponent>(entity);

	entt::entity bullet = scene->GetRegistry().create();
	scene->GetRegistry().emplace<TagComponent>(bullet).tag = TagType::Bullet;
	
	auto& bTc = scene->GetRegistry().emplace<TransformComponent>(bullet);
	bTc.translate = pTc.translate;
	bTc.translate.y += 1.0f; // 腰か胸の高さ
	
	bTc.rotate = pTc.rotate;
	bTc.rotate.y += spreadYaw;
	bTc.rotate.x += spreadPitch;
	
	// ★追加: ターゲットロック中のオートエイム
	if (scene->GetRegistry().all_of<CameraTargetComponent>(entity)) {
		auto& ct = scene->GetRegistry().get<CameraTargetComponent>(entity);
		if (ct.lockedTarget != entt::null && scene->GetRegistry().valid(ct.lockedTarget)) {
			auto& eTc = scene->GetRegistry().get<TransformComponent>(ct.lockedTarget);
			float dx = eTc.translate.x - bTc.translate.x;
			float dy = (eTc.translate.y + 1.0f) - bTc.translate.y; // 敵の胸を狙う
			float dz = eTc.translate.z - bTc.translate.z;
			
			float distXZ = std::sqrt(dx*dx + dz*dz);
			float yaw = std::atan2(dx, dz);
			float pitch = std::atan2(-dy, distXZ);

			bTc.rotate.y = yaw + spreadYaw;
			bTc.rotate.x = pitch + spreadPitch;
			bTc.rotate.z = 0.0f;
		}
	}

	// ★追加: プレイヤー自身の当たり判定と被らないよう、前方にオフセットする
	float cosX = std::cos(bTc.rotate.x);
	float moveX = std::sin(bTc.rotate.y) * cosX;
	float moveZ = std::cos(bTc.rotate.y) * cosX;
	float moveY = -std::sin(bTc.rotate.x);

	bTc.translate.x += moveX * 2.0f;
	bTc.translate.y += moveY * 2.0f;
	bTc.translate.z += moveZ * 2.0f;

	bTc.scale = { 0.2f, 0.2f, 0.6f };

	auto* renderer = scene->GetRenderer();
	if (renderer) {
		auto& mr = scene->GetRegistry().emplace<MeshRendererComponent>(bullet);
		mr.modelHandle = renderer->LoadObjMesh("Resources/Models/cube/cube.obj");
		mr.textureHandle = renderer->LoadTexture2D("Resources/Textures/white1x1.png");
		mr.color = { 1.0f, 0.8f, 0.2f, 1.0f }; // 黄色っぽい弾
	}

	auto& hb = scene->GetRegistry().emplace<HitboxComponent>(bullet);
	hb.isActive = true;
	hb.damage = damage;
	hb.tag = TagType::Bullet;
	hb.size = { 1.0f, 1.0f, 1.0f };

	auto& sc = scene->GetRegistry().emplace<ScriptComponent>(bullet);
	sc.scripts.push_back({"BulletScript", "", nullptr});

	auto& vc = scene->GetRegistry().emplace<VariableComponent>(bullet);
	vc.SetValue("MaxLifeTime", lifeTime);

	// ★追加: マズルフラッシュ生成（疑似ライト）
	MuzzleFlash flash;
	flash.pos = bTc.translate; // 弾のスポーン位置（銃口付近）
	flash.life = 0.06f; // 一瞬だけ光る
	flash.maxLife = 0.06f;
	muzzleFlashes_.push_back(flash);
	if (muzzleFlashes_.size() > 30) muzzleFlashes_.pop_front();

	// ★追加: 薬莢生成
	ShellCasing shell;
	shell.pos = pTc.translate;
	shell.pos.y += 1.2f; // 胸の高さ
	// プレイヤーの右側へ、上に小さく弾く
	float shellRightX = std::cos(pTc.rotate.y);
	float shellRightZ = -std::sin(pTc.rotate.y);
	shell.velocity.x = shellRightX * 3.0f + ((rand() % 100 / 100.0f - 0.5f) * 1.0f);
	shell.velocity.y = 3.0f + (rand() % 100 / 100.0f) * 2.0f;
	shell.velocity.z = shellRightZ * 3.0f + ((rand() % 100 / 100.0f - 0.5f) * 1.0f);
	shell.life = 0.6f;
	shellCasings_.push_back(shell);
	if (shellCasings_.size() > 30) shellCasings_.pop_front();
}

void PlayerScript::SwitchPlayerType(entt::entity /*entity*/, GameScene* scene) {
	if (playerType_ == PlayerType::Sword) {
		playerType_ = PlayerType::Gun;
		std::cout << "Switched to Gun Mode\n";
	} else {
		playerType_ = PlayerType::Sword;
		std::cout << "Switched to Sword Mode\n";
	}

	entt::entity sword = scene->FindObjectByName(swordName_);
	entt::entity gun = scene->FindObjectByName(gunName_);

	if (sword != entt::null && scene->GetRegistry().all_of<MeshRendererComponent>(sword)) {
		scene->GetRegistry().get<MeshRendererComponent>(sword).enabled = (playerType_ == PlayerType::Sword);
		if (playerType_ == PlayerType::Gun && scene->GetRegistry().all_of<HitboxComponent>(sword)) {
			scene->GetRegistry().get<HitboxComponent>(sword).isActive = false;
		}
	}
	if (gun != entt::null && scene->GetRegistry().all_of<MeshRendererComponent>(gun)) {
		scene->GetRegistry().get<MeshRendererComponent>(gun).enabled = (playerType_ == PlayerType::Gun);
	}
}

void PlayerScript::ExecuteSkill(entt::entity entity, GameScene* scene) {
	if (skillCooldown_ > 0.0f) return;

	if (playerType_ == PlayerType::Sword) {
		skillCooldown_ = 20.0f;
		
		auto& pTc = scene->GetRegistry().get<TransformComponent>(entity);
		entt::entity wave = scene->GetRegistry().create();
		scene->GetRegistry().emplace<TagComponent>(wave).tag = TagType::PlayerSword; // ★プレイヤーの剣攻撃として判定
		
		auto& wTc = scene->GetRegistry().emplace<TransformComponent>(wave);
		wTc.translate = pTc.translate;
		wTc.translate.y += 1.0f;
		wTc.rotate = pTc.rotate;

		float moveX = std::sin(wTc.rotate.y);
		float moveZ = std::cos(wTc.rotate.y);
		wTc.translate.x += moveX * 2.5f;
		wTc.translate.z += moveZ * 2.5f;
		
		wTc.scale = { 12.0f, 0.2f, 2.0f }; // 超横広な衝撃波（次元斬）

		auto* renderer = scene->GetRenderer();
		if (renderer) {
			auto& mr = scene->GetRegistry().emplace<MeshRendererComponent>(wave);
			mr.modelHandle = renderer->LoadObjMesh("Resources/Models/cube/cube.obj");
			mr.textureHandle = renderer->LoadTexture2D("Resources/Textures/white1x1.png");
			mr.color = { 0.1f, 0.8f, 1.0f, 0.8f }; // 青白い光
		}

		auto& hb = scene->GetRegistry().emplace<HitboxComponent>(wave);
		hb.isActive = true;
		hb.damage = 150.0f; // 大ダメージ
		hb.tag = TagType::PlayerSword; // ★プレイヤーの剣攻撃として判定
		hb.size = { 12.0f, 1.0f, 2.0f };

		auto& sc = scene->GetRegistry().emplace<ScriptComponent>(wave);
		sc.scripts.push_back({"BulletScript", "", nullptr});
		
		entt::entity sword = scene->FindObjectByName(swordName_);
		if (sword != entt::null && scene->GetRegistry().all_of<MotionComponent>(sword)) {
			auto& motion = scene->GetRegistry().get<MotionComponent>(sword);
			motion.PlayAnimation("Combo2"); // 振り下ろすモーション
			isAttacking_ = true; 
		}
		std::cout << "Executed Sword Skill: Dimension Wave\n";
	} else {
		skillCooldown_ = 0.2f;
		if (gunType_ == GunType::AssaultRifle) {
			gunType_ = GunType::Shotgun;
			std::cout << "Switched to Shotgun\n";
		} else {
			gunType_ = GunType::AssaultRifle;
			std::cout << "Switched to AssaultRifle\n";
		}
	}
}

void PlayerScript::OnEditorUI() {
#if defined(USE_IMGUI) && !defined(NDEBUG)
	ImGui::SeparatorText("Player Debug");
	ImGui::Text("Level: %d", level_);
	ImGui::Text("Experience: %.1f / %.1f", experience_, nextExperience_);
	ImGui::Text("Subscribe Count: %d", debugSubscribeCount_);
	ImGui::Text("Receive Count: %d", debugReceiveCount_);
	ImGui::Text("Last Value: %.2f", debugLastValue_);
#endif
}

void PlayerScript::DrawUI(entt::entity entity, GameScene* scene) {
	auto* renderer = Engine::Renderer::GetInstance();
	if (!renderer) return;

	// ---- 経験値バー ----
	float progress = nextExperience_ > 0.0f ? (experience_ / nextExperience_) : 0.0f;
	progress = std::clamp(progress, 0.0f, 1.0f);

	// 背景 (黒半透明)
	Engine::Renderer::SdfUIDesc bgDesc{};
	bgDesc.centerPx = {170.0f, 32.0f};
	bgDesc.sizePx = {300.0f, 24.0f};
	bgDesc.lineWidth = 0.0f;
	bgDesc.glow = 0.0f;
	bgDesc.color = {0.1f, 0.1f, 0.1f, 0.8f};
	bgDesc.shape = 0;
	bgDesc.round = 4.0f;
	bgDesc.progress = 1.0f;
	bgDesc.fill = 1.0f;
	renderer->DrawSDFUI(bgDesc);

	// 経験値バー本体 (青系)
	Engine::Renderer::SdfUIDesc barDesc{};
	barDesc.centerPx = {170.0f, 32.0f};
	barDesc.sizePx = {300.0f, 24.0f};
	barDesc.lineWidth = 0.0f;
	barDesc.glow = 2.0f;
	barDesc.color = {0.2f, 0.6f, 1.0f, 1.0f};
	barDesc.shape = 0;
	barDesc.round = 4.0f;
	barDesc.progress = progress;
	barDesc.fill = 1.0f;
	if (progress > 0.0f) {
		renderer->DrawSDFUI(barDesc);
	}

	// 外枠 (白)
	Engine::Renderer::SdfUIDesc outlineDesc{};
	outlineDesc.centerPx = {170.0f, 32.0f};
	outlineDesc.sizePx = {300.0f, 24.0f};
	outlineDesc.lineWidth = 2.0f;
	outlineDesc.glow = 0.0f;
	outlineDesc.color = {1.0f, 1.0f, 1.0f, 1.0f};
	outlineDesc.shape = 0;
	outlineDesc.round = 4.0f;
	outlineDesc.progress = 1.0f;
	outlineDesc.fill = 0.0f;
	renderer->DrawSDFUI(outlineDesc);

	// テキスト (Lvと経験値)
	char textBuf[64];
	snprintf(textBuf, sizeof(textBuf), "Lv.%d   EXP: %.1f / %.1f", level_, experience_, nextExperience_);
	renderer->DrawString(textBuf, 32.0f, 26.0f, 0.5f, {0,0,0,1});
	renderer->DrawString(textBuf, 30.0f, 24.0f, 0.5f, {1,1,1,1});

	// ==== ★追加: ロックオンレティクル ====
	if (scene && scene->GetRegistry().all_of<CameraTargetComponent>(entity)) {
		auto& ct = scene->GetRegistry().get<CameraTargetComponent>(entity);
		if (ct.lockedTarget != entt::null && scene->GetRegistry().valid(ct.lockedTarget)) {
			auto& eTc = scene->GetRegistry().get<TransformComponent>(ct.lockedTarget);
			DirectX::XMFLOAT3 targetWorldPos = eTc.translate;
			targetWorldPos.y += 1.5f; // 敵の頭上

			auto* camera = &scene->GetCamera();
			if (camera) {
				float sx = 0.0f, sy = 0.0f;
				if (UISystem::WorldToScreen(targetWorldPos, *camera, sx, sy)) {
					// ダイヤモンド型のターゲットマーカー（4つの短い線）
					float reticleSize = 20.0f;
					Engine::Vector4 reticleColor = {1.0f, 0.3f, 0.3f, 1.0f}; // 赤

					// 外枚 (ダイヤモンド型 – 4本の線)
					Engine::Renderer::SdfUIDesc rd{};
					rd.shape = 1; // Circle
					rd.centerPx = {sx, sy};
					rd.sizePx = {reticleSize * 2.0f, reticleSize * 2.0f};
					rd.lineWidth = 2.0f;
					rd.glow = 3.0f;
					rd.color = reticleColor;
					rd.progress = 1.0f;
					rd.fill = 0.0f;
					rd.round = 0.0f;
					renderer->DrawSDFUI(rd);

					// 内側のドット
					Engine::Renderer::SdfUIDesc dotDesc{};
					dotDesc.shape = 1;
					dotDesc.centerPx = {sx, sy};
					dotDesc.sizePx = {4.0f, 4.0f};
					dotDesc.lineWidth = 0.0f;
					dotDesc.glow = 2.0f;
					dotDesc.color = reticleColor;
					dotDesc.progress = 1.0f;
					dotDesc.fill = 1.0f;
					renderer->DrawSDFUI(dotDesc);

					// ターゲット名表示
					if (scene->GetRegistry().all_of<NameComponent>(ct.lockedTarget)) {
						auto& name = scene->GetRegistry().get<NameComponent>(ct.lockedTarget).name;
						renderer->DrawString(name, sx - 30.0f, sy - reticleSize - 18.0f, 0.4f, reticleColor);
					}
				}
			}
		}
	}

	// ==== ★追加: マズルフラッシュ描画 (3Dライン) ====
	float dt = 1.0f / 60.0f; // 簡易dt
	for (auto& mf : muzzleFlashes_) {
		mf.life -= dt;
		if (mf.life > 0.0f) {
			float alpha = mf.life / mf.maxLife;
			float size = 0.3f + (1.0f - alpha) * 0.5f;
			// 十字のフラッシュ
			renderer->DrawLine3D(
				{mf.pos.x - size, mf.pos.y, mf.pos.z},
				{mf.pos.x + size, mf.pos.y, mf.pos.z},
				{1.0f, 0.9f, 0.3f, alpha});
			renderer->DrawLine3D(
				{mf.pos.x, mf.pos.y - size, mf.pos.z},
				{mf.pos.x, mf.pos.y + size, mf.pos.z},
				{1.0f, 0.9f, 0.3f, alpha});
			renderer->DrawLine3D(
				{mf.pos.x, mf.pos.y, mf.pos.z - size},
				{mf.pos.x, mf.pos.y, mf.pos.z + size},
				{1.0f, 0.9f, 0.3f, alpha});
		}
	}
	while (!muzzleFlashes_.empty() && muzzleFlashes_.front().life <= 0) muzzleFlashes_.pop_front();

	// ==== ★追加: 残像描画 (3Dラインでシルエット) ====
	for (auto& ai : afterImages_) {
		ai.life -= dt;
		if (ai.life > 0.0f) {
			float alpha = (ai.life / ai.maxLife) * 0.5f;
			Engine::Vector4 ghostColor = {0.3f, 0.7f, 1.0f, alpha};
			
			float hw = ai.scale.x * 0.5f;
			float hh = ai.scale.y * 0.5f;
			// 人型のシルエット（矩形の輪郭）
			Engine::Vector3 p0 = {ai.pos.x - hw, ai.pos.y,           ai.pos.z};
			Engine::Vector3 p1 = {ai.pos.x + hw, ai.pos.y,           ai.pos.z};
			Engine::Vector3 p2 = {ai.pos.x + hw, ai.pos.y + hh*2.0f, ai.pos.z};
			Engine::Vector3 p3 = {ai.pos.x - hw, ai.pos.y + hh*2.0f, ai.pos.z};
			renderer->DrawLine3D(p0, p1, ghostColor);
			renderer->DrawLine3D(p1, p2, ghostColor);
			renderer->DrawLine3D(p2, p3, ghostColor);
			renderer->DrawLine3D(p3, p0, ghostColor);
			// 対角線（X印っぽい残像）
			renderer->DrawLine3D(p0, p2, ghostColor);
			renderer->DrawLine3D(p1, p3, ghostColor);
		}
	}
	while (!afterImages_.empty() && afterImages_.front().life <= 0) afterImages_.pop_front();

	// ==== ★追加: 薬莢描画 (3Dラインで小さな金色の線) ====
	for (auto& sc : shellCasings_) {
		sc.life -= dt;
		sc.velocity.y -= 15.0f * dt; // 重力
		sc.pos.x += sc.velocity.x * dt;
		sc.pos.y += sc.velocity.y * dt;
		sc.pos.z += sc.velocity.z * dt;

		if (sc.life > 0.0f) {
			float alpha = sc.life / 0.6f;
			Engine::Vector4 shellColor = {0.9f, 0.7f, 0.2f, alpha};
			renderer->DrawLine3D(
				{sc.pos.x, sc.pos.y, sc.pos.z},
				{sc.pos.x, sc.pos.y + 0.08f, sc.pos.z},
				shellColor);
		}
	}
	while (!shellCasings_.empty() && shellCasings_.front().life <= 0) shellCasings_.pop_front();
}

void PlayerScript::OnDestroy(entt::entity /*entity*/, GameScene* /*scene*/) {}

REGISTER_SCRIPT(PlayerScript);

} // namespace Game
