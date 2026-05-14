#include "PlayerScript.h"
#ifdef USE_IMGUI
#include "../../externals/imgui/imgui.h"
#endif
#include "ObjectTypes.h"
#include "PhaseSystemScript.h"
#include "BulletScript.h"
#include "MirrorShatterScript.h"
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

	// ---- 剣（大剣）の初期化 ----
	entt::entity sword = scene->FindObjectByName(swordName_);
	if (sword == entt::null) {
		sword = scene->GetRegistry().create();
		scene->GetRegistry().emplace<NameComponent>(sword).name = swordName_;
	}

	// 必須コンポーネントを確実に付与・更新
	auto& sTc = scene->GetRegistry().get_or_emplace<TransformComponent>(sword);
	sTc.scale = { 0.45f, 0.40f, 3.2f }; // 大剣サイズ

	auto& sHierarchy = scene->GetRegistry().get_or_emplace<HierarchyComponent>(sword);
	sHierarchy.parentId = entity; // プレイヤーの子にする

	auto& sTag = scene->GetRegistry().get_or_emplace<TagComponent>(sword);
	sTag.tag = TagType::PlayerSword;

	auto* renderer = scene->GetRenderer();
	if (renderer) {
		auto& sMr = scene->GetRegistry().get_or_emplace<MeshRendererComponent>(sword);
		sMr.modelHandle = renderer->LoadObjMesh("Resources/Models/cube/cube.obj");
		sMr.textureHandle = renderer->LoadTexture2D("Resources/Textures/white1x1.png");
		sMr.color = { 0.8f, 0.8f, 0.85f, 1.0f }; // 少し金属質なグレー
		sMr.enabled = true;
	}

	auto& sMotion = scene->GetRegistry().get_or_emplace<MotionComponent>(sword);
	// 大剣用コンボモーションの定義
	auto setupGreatswordCombo = [&](const std::string& name, int index) {
		MotionComponent::MotionClip clip;
		clip.name = name;
		clip.loop = false;
		float bsz = 3.2f;
		if (index == 1) { // 横薙ぎ
			clip.totalDuration = 0.7f;
			clip.keyframes.push_back({0.00f, { 1.5f, 1.2f, -0.5f }, { 0.0f, 2.0f, 0.0f }, { 0.45f, 0.40f, bsz }});
			clip.keyframes.push_back({0.25f, { 1.8f, 1.0f, 0.0f }, { 0.0f, 1.8f, 0.0f }, { 0.45f, 0.40f, bsz + 0.5f }});
			clip.keyframes.push_back({0.45f, { 0.0f, 1.0f, 2.5f }, { 0.0f, 0.0f, 0.0f }, { 0.45f, 0.40f, bsz + 1.2f }});
			clip.keyframes.push_back({0.70f, {-2.2f, 0.8f, -0.8f }, { 0.0f, -2.2f, 0.0f }, { 0.45f, 0.40f, bsz }});
		} else if (index == 2) { // 振り下ろし
			clip.totalDuration = 0.8f;
			clip.keyframes.push_back({0.00f, { 0.0f, 3.5f, -1.0f }, { -1.5f, 0.0f, 0.0f }, { 0.45f, 0.40f, bsz }});
			clip.keyframes.push_back({0.30f, { 0.0f, 3.8f, -0.5f }, { -1.8f, 0.0f, 0.0f }, { 0.45f, 0.40f, bsz }});
			clip.keyframes.push_back({0.50f, { 0.0f, 0.5f, 3.5f }, { 0.5f, 0.0f, 0.0f }, { 0.45f, 0.40f, bsz + 1.5f }});
			clip.keyframes.push_back({0.80f, { 0.0f, 0.2f, 2.8f }, { 0.8f, 0.0f, 0.0f }, { 0.45f, 0.40f, bsz }});
		} else { // 飛翔大回転（前方に飛ばして戻す）
			clip.totalDuration = 1.2f;
			// 0.0s: 始動
			clip.keyframes.push_back({0.00f, { 0.0f, 1.2f, 0.5f }, { 0.0f, 0.0f, 0.0f }, { 0.45f, 0.40f, bsz }});
			// 0.3s: 前方に射出開始 + 回転
			clip.keyframes.push_back({0.30f, { 0.0f, 1.2f, 4.5f }, { 0.0f, 6.28f, 0.0f }, { 0.45f, 0.40f, bsz + 0.5f }});
			// 0.6s: 最遠地点で激しく回転 (Boomerang Peak)
			clip.keyframes.push_back({0.60f, { 0.0f, 1.2f, 7.5f }, { 0.0f, 12.56f, 0.0f }, { 0.45f, 0.40f, bsz + 1.0f }});
			// 0.9s: 帰還開始
			clip.keyframes.push_back({0.90f, { 0.0f, 1.2f, 3.5f }, { 0.0f, 18.84f, 0.0f }, { 0.45f, 0.40f, bsz + 0.5f }});
			// 1.2s: キャッチ
			clip.keyframes.push_back({1.20f, { -1.0f, 0.8f, -0.5f }, { 0.0f, 20.41f, 0.0f }, { 0.45f, 0.40f, bsz }});
		}
		sMotion.clips[name] = clip;
	};
	setupGreatswordCombo("Combo1", 1);
	setupGreatswordCombo("Combo2", 2);
	setupGreatswordCombo("Combo3", 3);

	// ★追加: 大剣の溜め攻撃モーション「スチーム・ダイブ・スラッシュ」
	if (auto* motion = scene->GetRegistry().try_get<MotionComponent>(sword)) {
		MotionComponent::MotionClip clip;
		clip.name = "SwordChargeAttack";
		clip.totalDuration = 1.1f;
		clip.loop = false;
		float bsz = 3.2f;

		// 0.0s: 溜め・溜め開放（身構える）
		clip.keyframes.push_back({0.00f, { 0.0f, 0.5f, 0.0f }, { -0.5f, 0.0f, 0.0f }, { 0.45f, 0.40f, bsz }});
		// 0.2s: 跳躍開始（少し上へ、少し前へ）
		clip.keyframes.push_back({0.20f, { 0.0f, 2.5f, 1.5f }, { -1.5f, 0.0f, 0.0f }, { 0.45f, 0.40f, bsz + 0.5f }});
		// 0.45s: 空中ピーク（最大限に振りかぶる）
		clip.keyframes.push_back({0.45f, { 0.0f, 5.0f, 3.5f }, { -2.8f, 0.0f, 0.0f }, { 0.45f, 0.40f, bsz + 1.0f }});
		// 0.7s: 叩きつけ（最速で地面へ、前方に大きくリーチ）
		clip.keyframes.push_back({0.70f, { 0.0f, 0.2f, 5.5f }, { 0.8f, 0.0f, 0.0f }, { 0.45f, 0.40f, bsz + 1.8f }});
		// 1.1s: 着地硬直
		clip.keyframes.push_back({1.10f, { 0.0f, 0.1f, 4.0f }, { 1.2f, 0.0f, 0.0f }, { 0.45f, 0.40f, bsz }});
		
		motion->clips[clip.name] = clip;
	}

	auto& sHb = scene->GetRegistry().get_or_emplace<HitboxComponent>(sword);
	sHb.damage = 110.0f;
	sHb.tag = TagType::Sword;
	sHb.size = { 6.0f, 4.0f, 2.5f };
	sHb.center = { 0.0f, 0.0f, 1.2f };
	sHb.enabled = true;
	if (!isAttacking_) sHb.isActive = false;

	// ---- 銃の初期化 ----
	entt::entity gun = scene->FindObjectByName(gunName_);
	if (gun == entt::null) {
		gun = scene->GetRegistry().create();
		scene->GetRegistry().emplace<NameComponent>(gun).name = gunName_;
	}

	auto& gTc = scene->GetRegistry().get_or_emplace<TransformComponent>(gun);
	gTc.scale = { 0.1f, 0.1f, 0.8f };

	auto& gHierarchy = scene->GetRegistry().get_or_emplace<HierarchyComponent>(gun);
	gHierarchy.parentId = entity;

	if (renderer) {
		auto& gMr = scene->GetRegistry().get_or_emplace<MeshRendererComponent>(gun);
		gMr.modelHandle = renderer->LoadObjMesh("Resources/Models/cube/cube.obj");
		gMr.textureHandle = renderer->LoadTexture2D("Resources/Textures/white1x1.png");
		gMr.color = { 0.2f, 0.2f, 0.2f, 1.0f };
		gMr.enabled = true;
	}

	if (scene->GetRegistry().all_of<NameComponent>(entity)) {
		std::cout << "PlayerScript Started: Greatsword & Gun Initialized.\n";
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
		// ★追加: 強化弾ヒットイベント → 鏡割れエフェクト生成
		scene->GetEventSystem().Subscribe("EnhancedBulletHit", [this, entity, scene](float entityVal) {
			entt::entity target = static_cast<entt::entity>(static_cast<uint32_t>(entityVal));
			if (scene->GetRegistry().valid(target) && scene->GetRegistry().all_of<TransformComponent>(target)) {
				auto& tc = scene->GetRegistry().get<TransformComponent>(target);
				DirectX::XMFLOAT3 hitPos = tc.translate;
				hitPos.y += 1.0f;
				// 新しい鏡割れエフェクト(Distortionベース)を生成
				entt::entity shatterVfx = scene->CreateEntity("MirrorShatter");
				auto& vfxTc = scene->GetRegistry().get<TransformComponent>(shatterVfx);
				vfxTc.translate = hitPos;
				// ★弾の飛来方向をVariableComponentで渡す（プレイヤー→敵）
				auto& vc = scene->GetRegistry().emplace<VariableComponent>(shatterVfx);
				if (scene->GetRegistry().all_of<TransformComponent>(entity)) {
					auto& pTc = scene->GetRegistry().get<TransformComponent>(entity);
					float dx = tc.translate.x - pTc.translate.x;
					float dy = (tc.translate.y + 1.0f) - (pTc.translate.y + 1.0f);
					float dz = tc.translate.z - pTc.translate.z;
					float len = std::sqrt(dx*dx + dy*dy + dz*dz);
					if (len > 0.01f) { dx /= len; dy /= len; dz /= len; }
					vc.SetValue("DirX", dx);
					vc.SetValue("DirY", dy);
					vc.SetValue("DirZ", dz);
				}
				auto& sc = scene->GetRegistry().emplace<ScriptComponent>(shatterVfx);
				sc.scripts.push_back({"MirrorShatterScript", "", nullptr});
			}
		});
		// ★追加: プレイヤー被ダメージイベント
		scene->GetEventSystem().Subscribe("PlayerTakeDamage", [this, entity, scene](float) {
			if (!scene || !scene->GetContext().camera) return;
			// 1. 強めのカメラシェイク
			scene->GetContext().camera->StartShake(0.5f, 0.4f, 0.02f);
			
			// 2. ダメージ演出タイマーセット
			damageEffectTimer_ = DAMAGE_EFFECT_DURATION;
			
			// 3. プレイヤーの無敵時間を延長（少し長めの1.0秒に設定）
			if (scene->GetRegistry().all_of<HealthComponent>(entity)) {
				auto& hc = scene->GetRegistry().get<HealthComponent>(entity);
				hc.invincibleTime = 1.0f; 
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

	// ★追加: ダメージ演出の更新
	if (damageEffectTimer_ > 0.0f) {
		damageEffectTimer_ -= dt;
		if (damageEffectTimer_ < 0.0f) damageEffectTimer_ = 0.0f;
		
		auto* renderer = scene->GetRenderer();
		if (renderer) {
			renderer->SetPostEffect("Rich"); // ★追加: 赤色ビネット対応の高品質シェーダーに切り替え
			auto params = renderer->GetPostProcessParams();
			float rate = damageEffectTimer_ / DAMAGE_EFFECT_DURATION; // 1.0 -> 0.0
			
			// ビネット強度 (最大1.5)
			params.vignette = rate * 1.5f;
			// 色収差 (最大0.05)
			params.chromaShift = rate * 0.05f;
			
			renderer->SetPostProcessParams(params);
			renderer->SetPostProcessEnabled(true);
		}
	} else {
		// 演出終了時はパラメータをリセット
		auto* renderer = scene->GetRenderer();
		if (renderer) {
			auto params = renderer->GetPostProcessParams();
			if (params.vignette > 0.0f || params.chromaShift > 0.0f) {
				params.vignette = 0.0f;
				params.chromaShift = 0.0f;
				renderer->SetPostProcessParams(params);
				renderer->ResetPostEffect(); // ★追加: 元のエフェクトに戻す
				renderer->SetPostProcessEnabled(false);
			}
		}
	}

	// ★スキルバフ持続時間の管理
	if (isSkillActive_) {
		skillDuration_ -= dt;
		if (skillDuration_ <= 0.0f) {
			isSkillActive_ = false;
			skillDuration_ = 0.0f;
			std::cout << "Gun Skill Deactivated\n";
		}
	}

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
	
	auto& pTc = scene->GetRegistry().get<TransformComponent>(entity);

	bool isPrep = (hasPhaseSystem && PhaseSystemScript::IsPhase() == PhaseSystemScript::PreparationPhase);

	if (!isPrep) {
		// ★追加: スチーム・ブースト（剣士モード専用の高速回避）
		bool currentDashKeyDown = (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0;
		if (currentDashKeyDown && !prevDashKeyDown_ && playerType_ == PlayerType::Sword) {
			if (steamPressure_ > 0.0f && !isRecharging_) { // ★少しでもあれば発動可能に
				steamPressure_ -= DASH_COST;
				if (steamPressure_ < 0.0f) steamPressure_ = 0.0f; // リチャージ判定へ

				float dx = 0.0f, dz = 0.0f;
				if (scene->GetRegistry().all_of<PlayerInputComponent>(entity)) {
					auto& input = scene->GetRegistry().get<PlayerInputComponent>(entity);
					if (std::abs(input.moveDir.x) > 0.1f || std::abs(input.moveDir.y) > 0.1f) {
						// ★修正: カメラの向きに合わせてダッシュ方向を計算 (CharacterMovementSystemと同期)
						if (auto* camera = scene->GetContext().camera) {
							auto camRot = camera->Rotation();
							float cy = std::cos(camRot.y);
							float sy = std::sin(camRot.y);
							dx = input.moveDir.x * cy + input.moveDir.y * sy;
							dz = -input.moveDir.x * sy + input.moveDir.y * cy;
						} else {
							dx = input.moveDir.x;
							dz = input.moveDir.y;
						}
					} else {
						dx = std::sin(pTc.rotate.y);
						dz = std::cos(pTc.rotate.y);
					}
				}

				float len = std::sqrt(dx * dx + dz * dz);
				if (len > 0.001f) {
					dx /= len; dz /= len;
					recoilVelocity_.x = dx * DASH_POWER;
					recoilVelocity_.z = dz * DASH_POWER;

					// ★演出: ブースト噴射エフェクト (進行方向と逆へ大量に噴射)
					for (int i = 0; i < 2; ++i) { // 2回生成して密度を倍増
						entt::entity boostVfx = scene->CreateEntity("SteamBoost_VFX");
						auto& bTc = scene->GetRegistry().get<TransformComponent>(boostVfx);
						// ★修正: 発生位置を1フレーム後の予測位置に合わせて離れすぎを防ぐ
						bTc.translate.x = pTc.translate.x + recoilVelocity_.x * dt;
						bTc.translate.y = pTc.translate.y + 1.0f;
						bTc.translate.z = pTc.translate.z + recoilVelocity_.z * dt;

						// 進行方向と逆（背面）に少しずらしてスラスター感を出す
						float backOffset = 0.8f;
						bTc.translate.x -= dx * backOffset;
						bTc.translate.z -= dz * backOffset;

						// 少しだけ横にずらして「ツインスラスター」感を出す
						float offsetX = (i == 0 ? -0.4f : 0.4f);
						float sideX = -dz * offsetX; 
						float sideZ = dx * offsetX;
						bTc.translate.x += sideX;
						bTc.translate.z += sideZ;

						scene->SetTag(boostVfx, TagType::VFX);

						auto& bVc = scene->GetRegistry().emplace<VariableComponent>(boostVfx);
						bVc.SetValue("NormalX", -dx);
						bVc.SetValue("NormalY", 0.05f); // わずか斜め上
						bVc.SetValue("NormalZ", -dz);
						bVc.SetValue("Radius", 4.0f);
						bVc.SetValue("Duration", 0.4f);
						bVc.SetValue("ScatterMode", 0.0f);
						bVc.SetValue("ScatterSpeed", 13.0f);
						bVc.SetValue("Count", 40.0f);

						auto& bSc = scene->GetRegistry().emplace<ScriptComponent>(boostVfx);
						bSc.scripts.push_back({ "SpaceShatterScript", "", nullptr });
					}

					// ★追加: ブースト発動時のカメラシェイク (弱体化)
					if (auto* camera = scene->GetContext().camera) {
						camera->StartShake(0.12f, 0.15f, 0.01f);
					}
				}
			}
		}
		prevDashKeyDown_ = (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0;

		// ★追加: 物理・反動移動の更新 (全モード共通)
		float recoilLenSq = recoilVelocity_.x * recoilVelocity_.x + recoilVelocity_.y * recoilVelocity_.y + recoilVelocity_.z * recoilVelocity_.z;
		if (recoilLenSq > 0.001f) {
			pTc.translate.x += recoilVelocity_.x * dt;
			pTc.translate.y += recoilVelocity_.y * dt; // ★高さ移動
			pTc.translate.z += recoilVelocity_.z * dt;

			// 減衰 (XZのみ): 慣性を持たせるため少し緩める (0.02 -> 0.4)
			float damping = std::pow(0.4f, dt);
			recoilVelocity_.x *= damping;
			recoilVelocity_.z *= damping;

			// ★重力と着地判定 (地形の高さを考慮)
			recoilVelocity_.y -= 55.0f * dt; // 重力
			
			float groundY = scene->GetHeightAt(pTc.translate.x, pTc.translate.z, pTc.translate.y + 1.0f);
			float landingY = groundY + 1.0f; // キャラクター中心の接地高さ

			if (pTc.translate.y < landingY) {
				// 落下速度が一定以上（空中からの着地）の時だけ摩擦をかける
				if (recoilVelocity_.y < -1.0f) {
					recoilVelocity_.x *= 0.75f;
					recoilVelocity_.z *= 0.75f;
				}
				pTc.translate.y = landingY;
				recoilVelocity_.y = 0.0f;
			}
		}
	}

	if (isPrep) {
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

		// ★追加: 蒸気圧リチャージ処理 (モードに関わらず共通で実行)
		if (isRecharging_) {
			rechargeTimer_ -= dt;
			// リチャージ中は徐々に蒸気圧が回復する
			float rechargeRate = maxSteamPressure_ / RECHARGE_TIME;
			steamPressure_ += rechargeRate * dt;
			if (steamPressure_ >= maxSteamPressure_) {
				steamPressure_ = maxSteamPressure_;
				isRecharging_ = false;
				rechargeTimer_ = 0.0f;
			}
		}

		// ★追加: 圧力が尽きた際のリチャージ開始判定 (全モード共通)
		if (steamPressure_ <= 0.0f && !isRecharging_) {
			steamPressure_ = 0.0f;
			isRecharging_ = true;
			rechargeTimer_ = RECHARGE_TIME;
		}

		// ★追加: 圧力ゲージの描画 (リチャージ中、銃モード、消費中、スキル中、または戦闘フェーズなら表示)
		bool isBattle = (hasPhaseSystem && PhaseSystemScript::IsPhase() == PhaseSystemScript::BattlePhase);
		bool shouldShowGauge = (playerType_ == PlayerType::Gun) || isRecharging_ || (steamPressure_ < maxSteamPressure_) || isSkillActive_ || isBattle;
		if (shouldShowGauge) {
			DrawPressureGauge(scene);
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
	// ★スキルバフ中は移動速度UP
	if (isSkillActive_ && playerType_ == PlayerType::Gun) {
		speedMul *= SKILL_SPEED_MULTIPLIER;
	}
	input.moveDir.x *= speedMul;
	input.moveDir.y *= speedMul;
}

void PlayerScript::UpdateAttack(entt::entity entity, GameScene* scene, float dt) {
	bool currentAttackKeyDown = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
	
	// ★追加: 大剣の溜め攻撃ロジック
	if (playerType_ == PlayerType::Sword && !isRecharging_) {
		if (currentAttackKeyDown) {
			if (!isAttacking_ && !isSwordCharging_) {
				isSwordCharging_ = true;
				swordChargeTime_ = 0.0f;
			}
			
			if (isSwordCharging_) {
				swordChargeTime_ += dt;
				// 溜め中の蒸気エフェクト
				swordChargeVfxTimer_ += dt;
				if (swordChargeVfxTimer_ > 0.15f) {
					swordChargeVfxTimer_ = 0.0f;
					auto& pTc = scene->GetRegistry().get<TransformComponent>(entity);
					entt::entity vfx = scene->CreateEntity("SwordCharge_Steam");
					auto& vTc = scene->GetRegistry().get<TransformComponent>(vfx);
					vTc.translate = pTc.translate;
					vTc.translate.y += 0.5f;
					scene->SetTag(vfx, TagType::VFX);
					auto& bVc = scene->GetRegistry().emplace<VariableComponent>(vfx);
					bVc.SetValue("NormalY", 1.0f);
					bVc.SetValue("Radius", 3.0f + std::min(swordChargeTime_, 1.0f) * 4.0f);
					bVc.SetValue("Count", 15.0f);
					bVc.SetValue("ScatterSpeed", 5.0f);
					bVc.SetValue("Duration", 0.5f);
					auto& bSc = scene->GetRegistry().emplace<ScriptComponent>(vfx);
					bSc.scripts.push_back({"SpaceShatterScript", "", nullptr});
				}
			}
		} else {
			if (isSwordCharging_) {
				isSwordCharging_ = false;
				if (swordChargeTime_ >= 0.35f && (steamPressure_ > 0.0f || isSkillActive_)) { // ★少しでもあれば発動可能
					// ★溜め攻撃発動！
					steamPressure_ -= SWORD_CHARGE_COST;
					if (steamPressure_ < 0.0f) steamPressure_ = 0.0f;

					isAttacking_ = true;
					comboCount_ = 0; // 0は溜め攻撃用
					entt::entity sword = scene->FindObjectByName(swordName_);
					if (sword != entt::null) {
						auto& motion = scene->GetRegistry().get<MotionComponent>(sword);
						motion.PlayAnimation("SwordChargeAttack");
						
						// ★前方にジャンプする推進力を与える
						auto& pTc = scene->GetRegistry().get<TransformComponent>(entity);
						float dx = std::sin(pTc.rotate.y);
						float dz = std::cos(pTc.rotate.y);
						recoilVelocity_.x = dx * 38.0f;
						recoilVelocity_.z = dz * 38.0f;
						recoilVelocity_.y = 22.0f; // ★垂直方向の推進力を追加

						if (auto* camera = scene->GetContext().camera) {
							camera->StartShake(0.35f, 0.45f, 0.02f);
						}
					}
				} else {
					// 溜めが足りない場合は通常攻撃（またはキャンセル）
					attackQueued_ = true;
				}
				swordChargeTime_ = 0.0f;
			}
		}
	}

	if (currentAttackKeyDown && !prevAttackKeyDown_) {
		if (!isSwordCharging_) attackQueued_ = true;
	}
	prevAttackKeyDown_ = currentAttackKeyDown;

	entt::entity sword = scene->FindObjectByName(swordName_);
	if (sword == entt::null) return;

	auto& motion = scene->GetRegistry().get<MotionComponent>(sword);

	if (isAttacking_) {
		sheatheTimer_ = 0.0f;
		isSheathed_ = false;

		if (!motion.isPlaying) {
			if (attackQueued_ && comboCount_ < 3 && comboCount_ >= 0) {
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
		swordTc.translate = { -0.3f, 1.2f, -0.6f };
		swordTc.rotate = { DirectX::XMConvertToRadians(35.0f), 0.0f, DirectX::XMConvertToRadians(25.0f) };
		swordTc.scale = { 0.45f, 0.40f, 3.2f }; // ★大剣サイズ

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

				// ★追加: 溜め攻撃の着地衝撃 (t=0.7s)
				if (motion->activeClip == "SwordChargeAttack") {
					static float lastImpactTime = -1.0f;
					if (t >= 0.7f && t < 0.8f && lastImpactTime < 0.0f) {
						lastImpactTime = t;
						
						// ワールド座標を取得
						Engine::Matrix4x4 worldMat = scene->GetWorldMatrix((int)sword);
						Engine::Vector3 worldPos = { worldMat.m[3][0], worldMat.m[3][1], worldMat.m[3][2] };
						float groundY = scene->GetHeightAt(worldPos.x, worldPos.z, worldPos.y + 1.0f);

						// 1. 地面が歪む巨大な衝撃波 (SpaceShatter)
						entt::entity impactVfx = scene->CreateEntity("GroundSmash_VFX");
						auto& iTc = scene->GetRegistry().get<TransformComponent>(impactVfx);
						iTc.translate = { worldPos.x, groundY + 0.1f, worldPos.z };
						scene->SetTag(impactVfx, TagType::VFX);

						auto& iVc = scene->GetRegistry().emplace<VariableComponent>(impactVfx);
						iVc.SetValue("NormalY", 1.0f);
						iVc.SetValue("Radius", 25.0f);     // 巨大な歪み
						iVc.SetValue("Duration", 0.9f);
						iVc.SetValue("ScatterMode", 0.0f);
						iVc.SetValue("ScatterSpeed", 35.0f);
						iVc.SetValue("Count", 120.0f);      // 密度高め
						auto& iSc = scene->GetRegistry().emplace<ScriptComponent>(impactVfx);
						iSc.scripts.push_back({"SpaceShatterScript", "", nullptr});

						// 2. スチームバースト (白い蒸気の爆発)
						entt::entity steamVfx = scene->CreateEntity("GroundSmash_Steam");
						auto& sTc = scene->GetRegistry().get<TransformComponent>(steamVfx);
						sTc.translate = iTc.translate;
						scene->SetTag(steamVfx, TagType::VFX);
						auto& sVc = scene->GetRegistry().emplace<VariableComponent>(steamVfx);
						sVc.SetValue("NormalY", 0.8f);
						sVc.SetValue("Radius", 15.0f);
						sVc.SetValue("Count", 60.0f);
						sVc.SetValue("ScatterSpeed", 18.0f);
						sVc.SetValue("Duration", 0.7f);
						auto& sSc = scene->GetRegistry().emplace<ScriptComponent>(steamVfx);
						sSc.scripts.push_back({"SpaceShatterScript", "", nullptr});

						// 3. 範囲ダメージ (Hitbox)
						entt::entity aoe = scene->GetRegistry().create();
						scene->GetRegistry().emplace<TagComponent>(aoe).tag = TagType::PlayerSword;
						auto& aTc = scene->GetRegistry().emplace<TransformComponent>(aoe);
						aTc.translate = iTc.translate;
						auto& aHb = scene->GetRegistry().emplace<HitboxComponent>(aoe);
						aHb.isActive = true;
						aHb.damage = 250.0f; // 溜め攻撃の超ダメージ
						aHb.size = { 18.0f, 5.0f, 18.0f };
						aHb.tag = TagType::Sword;
						scene->DestroyObject((uint32_t)aoe);

						if (auto* camera = scene->GetContext().camera) {
							camera->StartShake(0.5f, 0.8f, 0.04f); // 激しい揺れ
						}
					}
					if (t < 0.1f) lastImpactTime = -1.0f; // リセット
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
		float bladeLen = 3.2f; // ★大剣の長さに合わせる
		DirectX::XMVECTOR basePos = DirectX::XMVector3TransformCoord(DirectX::XMVectorSet(0, 0, -bladeLen*0.1f, 1), m);
		DirectX::XMVECTOR tipPos = DirectX::XMVector3TransformCoord(DirectX::XMVectorSet(0, 0, bladeLen*0.9f, 1), m);

		TrailPoint tp;
		DirectX::XMStoreFloat3(reinterpret_cast<DirectX::XMFLOAT3*>(&tp.base), basePos);
		DirectX::XMStoreFloat3(reinterpret_cast<DirectX::XMFLOAT3*>(&tp.tip), tipPos);
		tp.life = 0.5f;     // ★重さを出すため軌跡を少し長く残す
		tp.maxLife = 0.5f;
		trailPoints_.push_back(tp);
		if (trailPoints_.size() > 80) trailPoints_.pop_front();
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
		// エイム時：しっかり構える（平行に、少し低く）
		gunTc.translate = { 0.6f, 1.0f, 1.0f };
		gunTc.rotate = { 0.0f, 0.0f, 0.0f };
	} else {
		// 非エイム時：腰だめ（平行に、もっと低く）
		gunTc.translate = { 0.8f, 0.7f, 0.6f };
		gunTc.rotate = { 0.0f, 0.0f, 0.0f }; // 地面と平行に
	}
	gunTc.scale = { 0.15f, 0.15f, 1.2f }; 
}

void PlayerScript::UpdateGunAttack(entt::entity entity, GameScene* scene, float dt) {
	isAiming_ = (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0;
	bool currentAttackKeyDown = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;

	// ★リチャージ中は射撃不可（スキル発動中のみオーバークロックで許可）
	if (isRecharging_ && !isSkillActive_) {
		prevAttackKeyDown_ = currentAttackKeyDown;
		return;
	}

	auto& pTc = scene->GetRegistry().get<TransformComponent>(entity);

	// ★ロック中は敵の方を向く
	if (scene->GetRegistry().all_of<CameraTargetComponent>(entity)) {
		auto& ct = scene->GetRegistry().get<CameraTargetComponent>(entity);
		if (ct.lockedTarget != entt::null && scene->GetRegistry().valid(ct.lockedTarget)) {
			bool isActionActive = isCharging_ || currentAttackKeyDown || gunShootTimer_ > 0.0f;
			if (isActionActive) {
				auto& eTc = scene->GetRegistry().get<TransformComponent>(ct.lockedTarget);
				float dx = eTc.translate.x - pTc.translate.x;
				float dz = eTc.translate.z - pTc.translate.z;
				float targetYaw = std::atan2(dx, dz);
				float diff = targetYaw - pTc.rotate.y;
				while (diff >  DirectX::XM_PI) diff -= DirectX::XM_2PI;
				while (diff < -DirectX::XM_PI) diff += DirectX::XM_2PI;
				pTc.rotate.y += diff * std::min(1.0f, 30.0f * dt);
			}
		}
	}

	// ★反動後退の物理更新 (Updateへ移動)


	// ★チャージショット処理
	if (currentAttackKeyDown && gunShootTimer_ <= 0.0f) {
		if (!isCharging_ && !prevAttackKeyDown_) {
			// チャージ開始
			isCharging_ = true;
			chargeTime_ = 0.0f;
			chargeVfxTimer_ = 0.0f;
		}

		if (isCharging_) {
			chargeTime_ += dt;
			chargeTime_ = std::min(chargeTime_, CHARGE_TIME_MAX);

			// チャージ中の蒸気排出VFX（断続的に）
			chargeVfxTimer_ -= dt;
			if (chargeVfxTimer_ <= 0.0f && chargeTime_ > 0.15f) {
				// 銃の横から蒸気を少しずつ出す
				entt::entity gun = scene->FindObjectByName(gunName_);
				if (gun != entt::null) {
					Engine::Matrix4x4 gunWorld = scene->GetWorldMatrix((int)gun);
					DirectX::XMMATRIX m = DirectX::XMLoadFloat4x4(reinterpret_cast<DirectX::XMFLOAT4X4*>(&gunWorld));
					DirectX::XMVECTOR gunPos = DirectX::XMVector3TransformCoord(DirectX::XMVectorSet(0, 0, 0.0f, 1), m);
					DirectX::XMVECTOR gunRight = DirectX::XMVector3Normalize(DirectX::XMVector3TransformNormal(DirectX::XMVectorSet(1, 0, 0, 0), m));
					DirectX::XMVECTOR gunFwd = DirectX::XMVector3Normalize(DirectX::XMVector3TransformNormal(DirectX::XMVectorSet(0, 0, 1, 0), m));

					DirectX::XMFLOAT3 steamPos;
					DirectX::XMStoreFloat3(&steamPos, gunPos);
					DirectX::XMFLOAT3 fwd;
					DirectX::XMStoreFloat3(&fwd, gunFwd);

					// 短い蒸気エフェクト
					entt::entity steamVfx = scene->CreateEntity("ChargeSteam_VFX");
					auto& sTc = scene->GetRegistry().get<TransformComponent>(steamVfx);
					sTc.translate = steamPos;
					scene->SetTag(steamVfx, TagType::VFX);

					auto& sVc = scene->GetRegistry().emplace<VariableComponent>(steamVfx);
					sVc.SetValue("NormalX", fwd.x);
					sVc.SetValue("NormalY", fwd.y);
					sVc.SetValue("NormalZ", fwd.z);
					sVc.SetValue("Radius", 1.0f + chargeTime_ * 1.5f); // チャージが進むと大きく
					sVc.SetValue("Duration", 0.25f);
					sVc.SetValue("ScatterMode", 0.0f);
					sVc.SetValue("ScatterDelay", 0.0f);
					sVc.SetValue("ScatterSpeed", 2.0f);
					sVc.SetValue("Count", 5.0f);

					auto& sSc = scene->GetRegistry().emplace<ScriptComponent>(steamVfx);
					sSc.scripts.push_back({"SpaceShatterScript", "", nullptr});
				}

				// チャージが進むほど頻度が上がる（プシュッ、プシュッ → プシュシュシュ）
				float interval = 0.25f - (chargeTime_ / CHARGE_TIME_MAX) * 0.15f;
				chargeVfxTimer_ = std::max(0.08f, interval);
			}

			// 銃の振動（チャージが進むほど激しく）
			entt::entity gun = scene->FindObjectByName(gunName_);
			if (gun != entt::null) {
				auto& gunTc = scene->GetRegistry().get<TransformComponent>(gun);
				float intensity = (chargeTime_ / CHARGE_TIME_MAX) * 0.06f;
				gunTc.translate.x += ((rand() % 100) / 100.0f - 0.5f) * intensity;
				gunTc.translate.y += ((rand() % 100) / 100.0f - 0.5f) * intensity;
			}
		}
	}

	// ★ボタンを離した時の処理
	if (!currentAttackKeyDown && prevAttackKeyDown_ && isCharging_) {
		isCharging_ = false;

		// スキル発動中（オーバークロック）はコストを無視して撃てる
		if (chargeTime_ >= CHARGE_TIME_MIN && (isSkillActive_ || steamPressure_ >= CHARGE_SHOT_COST * 0.5f)) {
			// ★チャージショット発射！
			ShootChargeShot(entity, scene);
			if (!isSkillActive_) steamPressure_ -= CHARGE_SHOT_COST;

			// 反動後退
			float forwardX = std::sin(pTc.rotate.y);
			float forwardZ = std::cos(pTc.rotate.y);
			float recoilPower = 12.0f * (chargeTime_ / CHARGE_TIME_MAX);
			recoilVelocity_.x = -forwardX * recoilPower;
			recoilVelocity_.z = -forwardZ * recoilPower;

			gunShootTimer_ = 0.5f;
		} else if (isSkillActive_ || steamPressure_ > 0.0f) {
			// 通常射撃（短押し）- 残量に関わらず撃てる
			ShootGun(entity, scene);
			if (!isSkillActive_) steamPressure_ -= NORMAL_SHOT_COST;
			gunShootTimer_ = 0.3f;
		}

		chargeTime_ = 0.0f;

		// 圧力がなくなったらリチャージ開始 (Updateの共通処理へ移動)

	}

	// クールダウン
	if (gunShootTimer_ > 0.0f) gunShootTimer_ -= dt;

	prevAttackKeyDown_ = currentAttackKeyDown;
}

void PlayerScript::ShootChargeShot(entt::entity entity, GameScene* scene) {
	float baseDamage = 45.0f;
	float chargeMul = chargeTime_ / CHARGE_TIME_MAX;
	float damage = baseDamage * (0.5f + chargeMul * 0.5f);
	if (isSkillActive_) damage *= SKILL_DAMAGE_MULTIPLIER;

	SpawnBullet(entity, scene, 0.0f, 0.0f, damage, 3.0f, true, true);

	auto& pTc = scene->GetRegistry().get<TransformComponent>(entity);
	entt::entity gun = scene->FindObjectByName(gunName_);
	DirectX::XMFLOAT3 muzzlePos = pTc.translate;
	muzzlePos.y += 1.0f;
	float fwdX = std::sin(pTc.rotate.y);
	float fwdZ = std::cos(pTc.rotate.y);

	if (gun != entt::null) {
		Engine::Matrix4x4 gunWorld = scene->GetWorldMatrix((int)gun);
		DirectX::XMMATRIX m = DirectX::XMLoadFloat4x4(reinterpret_cast<DirectX::XMFLOAT4X4*>(&gunWorld));
		DirectX::XMVECTOR tip = DirectX::XMVector3TransformCoord(DirectX::XMVectorSet(0, 0, 0.6f, 1), m);
		DirectX::XMStoreFloat3(&muzzlePos, tip);
		DirectX::XMVECTOR forward = DirectX::XMVector3Normalize(DirectX::XMVector3TransformNormal(DirectX::XMVectorSet(0, 0, 1, 0), m));
		fwdX = DirectX::XMVectorGetX(forward);
		fwdZ = DirectX::XMVectorGetZ(forward);
	}

	entt::entity muzzleVfx = scene->CreateEntity("ChargeShot_VFX");
	auto& mTc = scene->GetRegistry().get<TransformComponent>(muzzleVfx);
	mTc.translate = muzzlePos;
	scene->SetTag(muzzleVfx, TagType::VFX);

	auto& mvc = scene->GetRegistry().emplace<VariableComponent>(muzzleVfx);
	mvc.SetValue("NormalX", fwdX);
	mvc.SetValue("NormalY", 0.0f);
	mvc.SetValue("NormalZ", fwdZ);
	mvc.SetValue("Radius", 5.0f + chargeMul * 3.0f);
	mvc.SetValue("Duration", 0.8f);
	mvc.SetValue("ScatterMode", 0.0f);
	mvc.SetValue("ScatterDelay", 0.0f);
	mvc.SetValue("ScatterSpeed", 15.0f);
	mvc.SetValue("Count", 50.0f + chargeMul * 30.0f);
	mvc.SetValue("IsSpecial", 1.0f);

	auto& msc = scene->GetRegistry().emplace<ScriptComponent>(muzzleVfx);
	msc.scripts.push_back({"SpaceShatterScript", "", nullptr});

	MuzzleFlash flash;
	flash.pos = muzzlePos;
	flash.life = 0.12f;
	flash.maxLife = 0.12f;
	muzzleFlashes_.push_back(flash);
}

void PlayerScript::DrawPressureGauge(GameScene* scene) {
	auto* renderer = scene->GetRenderer();
	if (!renderer) return;

	// ===== 1. 基本設定 =====
	float gaugeX = (float)Engine::WindowDX::kW * 0.5f;
	float gaugeY = (float)Engine::WindowDX::kH - 140.0f;
	float R = 85.0f; 
	float pressureRatio = std::clamp(steamPressure_ / maxSteamPressure_, 0.0f, 1.0f);
	float startAngle = DirectX::XM_PI * 0.75f; 
	float totalAngle = DirectX::XM_PI * 1.5f;

	// ===== 2. 背景・ベゼル =====
	renderer->DrawSDFUI({ {gaugeX + 3, gaugeY + 3}, {R + 8, R + 8}, 0, 6.0f, {0,0,0,0.3f}, 1, 0, 0, 1 });
	renderer->DrawSDFUI({ {gaugeX, gaugeY}, {R + 6, R + 6}, 0, 0, {0.25f, 0.18f, 0.06f, 1.0f}, 1, 0, 0, 1 });
	renderer->DrawSDFUI({ {gaugeX, gaugeY}, {R + 4, R + 4}, 1.5f, 0, {0.85f, 0.75f, 0.4f, 1.0f}, 1, 0, 0 });
	renderer->DrawSDFUI({ {gaugeX, gaugeY}, {R + 1, R + 1}, 1.0f, 0, {0.2f, 0.15f, 0.05f, 0.8f}, 1, 0, 0 });
	renderer->DrawSDFUI({ {gaugeX, gaugeY}, {R - 4, R - 4}, 0, 0, {0.96f, 0.94f, 0.88f, 1.0f}, 1, 0, 0, 1 });
	renderer->DrawSDFUI({ {gaugeX, gaugeY}, {R - 5, R - 5}, 8.0f, 4.0f, {0.3f, 0.2f, 0.1f, 0.15f}, 1, 0, 0 });

	// ===== 3. リベット =====
	float rivetR = R + 3.0f;
	for (int i = 0; i < 4; ++i) {
		float angle = DirectX::XM_PIDIV4 + DirectX::XM_PIDIV2 * (float)i;
		float rx = gaugeX + std::cos(angle) * rivetR;
		float ry = gaugeY + std::sin(angle) * rivetR;
		renderer->DrawSDFUI({ {rx, ry}, {3.0f, 3.0f}, 0, 0, {0.6f, 0.5f, 0.2f, 1.0f}, 1, 0, 0, 1 });
		renderer->DrawSDFUI({ {rx - 0.5f, ry - 0.5f}, {1.0f, 1.0f}, 0, 0, {1, 1, 1, 0.3f}, 1, 0, 0, 1 });
	}

	// ===== 4. 危険ゾーン（赤いドット）=====
	{
		float dangerStart = 0.75f;
		float zoneR = R - 10.0f;
		for (int i = 0; i < 8; ++i) {
			float t = dangerStart + (float)i / 8.0f * (1.0f - dangerStart);
			float angle = startAngle + t * totalAngle;
			float dotSize = 3.5f;
			renderer->DrawSDFUI({
				{ gaugeX + std::cos(angle) * zoneR, gaugeY + std::sin(angle) * zoneR },
				{ dotSize, dotSize }, 0, 0, {0.8f, 0.1f, 0.05f, 0.6f}, 1, 0, 0, 1
			});
		}
	}

	// ===== 5. 目盛り（ドットを高密度化）=====
	for (int i = 0; i <= 50; ++i) {
		float angle = startAngle + (float)i / 50.0f * totalAngle;
		bool isMajor = (i % 10 == 0);
		bool isMid = (i % 5 == 0);
		float dotSize = isMajor ? 4.8f : (isMid ? 3.2f : 1.6f); 
		float tickR = R - 10.0f; 
		float alpha = isMajor ? 1.0f : (isMid ? 0.7f : 0.35f);
		renderer->DrawSDFUI({
			{ gaugeX + std::cos(angle) * tickR, gaugeY + std::sin(angle) * tickR },
			{ dotSize, dotSize }, 0, 0, {0.15f, 0.1f, 0.05f, alpha}, 1, 0, 0, 1
		});
	}

	// ===== 6. 漢字数字ラベル（縦書き対応）=====
	const char* kanjiLabels[] = { (const char*)u8"零", (const char*)u8"五十", (const char*)u8"百" };
	float labelSteps[] = {0.0f, 0.5f, 1.0f};
	for (int i = 0; i < 3; ++i) {
		float angle = startAngle + labelSteps[i] * totalAngle;
		float labelR = R - 30.0f; 
		float sx = gaugeX + std::cos(angle) * labelR;
		float sy = gaugeY + std::sin(angle) * labelR;
		float fontSize = 0.45f;
		
		// 縦書き描画ロジック（簡易版）
		std::vector<std::string> chars;
		if (i == 1) { // "五十"
			chars.push_back((const char*)u8"五");
			chars.push_back((const char*)u8"十");
		} else {
			chars.push_back(kanjiLabels[i]);
		}

		float totalH = chars.size() * 15.0f;
		for (size_t c = 0; c < chars.size(); ++c) {
			float tw = renderer->MeasureTextWidth(chars[c].c_str(), fontSize);
			float offsetX = -tw * 0.5f;
			float offsetY = -totalH * 0.5f + c * 15.0f;
			renderer->DrawString(chars[c].c_str(), sx + offsetX, sy + offsetY, fontSize, {0.1f, 0.08f, 0.05f, 1.0f});
		}
	}

	// ===== 7. 中央ラベル（圧力計 - 軸の下側に配置）=====
	{
		const char* titleChars[] = { (const char*)u8"圧", (const char*)u8"力", (const char*)u8"計" };
		float fontSize = 0.28f;
		float startY = gaugeY + R * 0.15f; // 軸の下側に移動
		for (int i = 0; i < 3; ++i) {
			float tw = renderer->MeasureTextWidth(titleChars[i], fontSize);
			renderer->DrawString(titleChars[i], gaugeX - tw * 0.5f, startY + i * 14.0f, fontSize, {0.25f, 0.18f, 0.1f, 0.8f});
		}
	}

	// ===== 8. 針 & カウンターウェイト（微細な振動を追加）=====
	float currentAngle = startAngle + pressureRatio * totalAngle;
	
	// プルプルした振動（ジッター）の計算
	{
		float time = (float)GetTickCount() * 0.001f;
		float jitterBase = std::sin(time * 60.0f) * 0.012f; // 高速なサイン波
		jitterBase += (float(rand() % 100) / 100.0f - 0.5f) * 0.008f; // 不規則なノイズ
		
		float jitterIntensity = 0.4f + pressureRatio * 0.6f; // 圧力が高いほど震える
		if (isSkillActive_) jitterIntensity *= 2.2f;         // スキル中はさらに激しく

		// ★チャージ中の暴力的な揺れを追加
		if (isCharging_) {
			float cRatio = std::clamp(chargeTime_ / CHARGE_TIME_MAX, 0.0f, 1.0f);
			// チャージが進むほど、振幅の大きい不規則なガタつきを加える
			jitterBase += (float(rand() % 100) / 100.0f - 0.5f) * 0.15f * cRatio;
			jitterIntensity += cRatio * 2.0f;
		}
		
		currentAngle += jitterBase * jitterIntensity;
	}
	
	float cLen = 14.0f;
	float cAngle = currentAngle + DirectX::XM_PI;
	Engine::Renderer::SdfUIDesc counter;
	counter.centerPx = { gaugeX + std::cos(cAngle) * (cLen * 0.5f), gaugeY + std::sin(cAngle) * (cLen * 0.5f) };
	counter.sizePx = { cLen, 4.5f };
	counter.shape = 0;
	counter.rotateRad = -cAngle;
	counter.color = { 0.12f, 0.1f, 0.06f, 1.0f };
	counter.fill = 1.0f;
	renderer->DrawSDFUI(counter);

	float nLen = R * 0.9f;
	Engine::Renderer::SdfUIDesc needle;
	needle.centerPx = { gaugeX + std::cos(currentAngle) * (nLen * 0.5f), gaugeY + std::sin(currentAngle) * (nLen * 0.5f) };
	needle.sizePx = { nLen, 3.2f };
	needle.shape = 0;
	needle.rotateRad = -currentAngle;
	needle.color = isRecharging_ ? Engine::Vector4{0.9f, 0.1f, 0.1f, 1.0f} : Engine::Vector4{0.12f, 0.1f, 0.06f, 1.0f};
	needle.fill = 1.0f;
	renderer->DrawSDFUI(needle);

	renderer->DrawSDFUI({ {gaugeX, gaugeY}, {12, 12}, 0, 0, {0.22f, 0.16f, 0.06f, 1.0f}, 1, 0, 0, 1 });
	renderer->DrawSDFUI({ {gaugeX, gaugeY}, {8, 8}, 0, 0, {0.6f, 0.5f, 0.25f, 1.0f}, 1, 0, 0, 1 });

	// ===== 9. 特殊演出 =====
	if (isSkillActive_) {
		float p = std::sin(skillDuration_ * 12.0f) * 0.5f + 0.5f;
		renderer->DrawSDFUI({ {gaugeX, gaugeY}, {R + 6, R + 6}, 2.0f, 10.0f, {0.2f, 0.8f, 1.0f, 0.2f + p * 0.4f}, 1, 0, 0 });
		renderer->DrawString("OVERCLOCK", gaugeX - 55, gaugeY + R + 15, 0.4f, {0.3f, 0.9f, 1.0f, 1.0f});
	} else if (isRecharging_) {
		float b = std::sin(rechargeTimer_ * 10.0f) * 0.5f + 0.5f;
		renderer->DrawString("RECHARGING", gaugeX - 55, gaugeY + R + 15, 0.4f, {1.0f, 0.2f, 0.1f, 0.5f + b * 0.5f});
	}
}

void PlayerScript::ShootGun(entt::entity entity, GameScene* scene) {
	float baseDamage = 15.0f;
	float damage = isSkillActive_ ? baseDamage * SKILL_DAMAGE_MULTIPLIER : baseDamage;
	SpawnBullet(entity, scene, 0.0f, 0.0f, damage, 2.0f, isSkillActive_, false);

	// ★空間割れマズルエフェクト（銃口の前に小規模なガラス割れを生成）
	auto& pTc = scene->GetRegistry().get<TransformComponent>(entity);
	entt::entity gun = scene->FindObjectByName(gunName_);
	DirectX::XMFLOAT3 muzzlePos = pTc.translate;
	muzzlePos.y += 1.0f;
	float fwdX = std::sin(pTc.rotate.y);
	float fwdZ = std::cos(pTc.rotate.y);

	if (gun != entt::null) {
		Engine::Matrix4x4 gunWorld = scene->GetWorldMatrix((int)gun);
		DirectX::XMMATRIX m = DirectX::XMLoadFloat4x4(reinterpret_cast<DirectX::XMFLOAT4X4*>(&gunWorld));
		// 銃のモデルの先端（ローカルZ前方）を計算
		DirectX::XMVECTOR tip = DirectX::XMVector3TransformCoord(DirectX::XMVectorSet(0, 0, 0.6f, 1), m);
		DirectX::XMStoreFloat3(&muzzlePos, tip);
		
		// 向きも銃の向きに合わせる
		DirectX::XMVECTOR forward = DirectX::XMVector3Normalize(DirectX::XMVector3TransformNormal(DirectX::XMVectorSet(0, 0, 1, 0), m));
		fwdX = DirectX::XMVectorGetX(forward);
		fwdZ = DirectX::XMVectorGetZ(forward);
	} else {
		muzzlePos.x += fwdX * 1.5f;
		muzzlePos.z += fwdZ * 1.5f;
	}

	entt::entity muzzleVfx = scene->CreateEntity("MuzzleShatter_VFX");
	auto& mTc = scene->GetRegistry().get<TransformComponent>(muzzleVfx);
	mTc.translate = muzzlePos;
	scene->SetTag(muzzleVfx, TagType::VFX);

	auto& mvc = scene->GetRegistry().emplace<VariableComponent>(muzzleVfx);
	mvc.SetValue("NormalX", fwdX);
	mvc.SetValue("NormalY", 0.0f);
	mvc.SetValue("NormalZ", fwdZ);
	mvc.SetValue("Radius", 3.0f);             // 拡大
	mvc.SetValue("Duration", 0.5f);           // 素早く消える
	mvc.SetValue("ScatterMode", 0.0f);        // マズルモード
	mvc.SetValue("ScatterDelay", 0.0f);       // 即座に噴射
	mvc.SetValue("ScatterSpeed", 12.0f);      // 勢いよく

	auto& msc = scene->GetRegistry().emplace<ScriptComponent>(muzzleVfx);
	msc.scripts.push_back({"SpaceShatterScript", "", nullptr});
}

void PlayerScript::SpawnBullet(entt::entity entity, GameScene* scene, float spreadYaw, float spreadPitch, float damage, float lifeTime, bool enhanced, bool explode) {
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

	float cosX = std::cos(bTc.rotate.x);
	float moveX = std::sin(bTc.rotate.y) * cosX;
	float moveZ = std::cos(bTc.rotate.y) * cosX;
	float moveY = -std::sin(bTc.rotate.x);

	// ★銃の先端から弾を出す
	entt::entity gun = scene->FindObjectByName(gunName_);
	if (gun != entt::null) {
		Engine::Matrix4x4 gunWorld = scene->GetWorldMatrix((int)gun);
		DirectX::XMMATRIX m = DirectX::XMLoadFloat4x4(reinterpret_cast<DirectX::XMFLOAT4X4*>(&gunWorld));
		DirectX::XMVECTOR tip = DirectX::XMVector3TransformCoord(DirectX::XMVectorSet(0, 0, 0.6f, 1), m);
		DirectX::XMStoreFloat3(&bTc.translate, tip);
		
		// 弾の向きを銃の向き（プラス拡散分）に合わせる
		DirectX::XMVECTOR forward = DirectX::XMVector3Normalize(DirectX::XMVector3TransformNormal(DirectX::XMVectorSet(0, 0, 1, 0), m));
		moveX = DirectX::XMVectorGetX(forward);
		moveY = DirectX::XMVectorGetY(forward);
		moveZ = DirectX::XMVectorGetZ(forward);

		float fYaw = std::atan2(moveX, moveZ);
		float fPitch = -std::asin(std::max(-1.0f, std::min(1.0f, moveY)));
		bTc.rotate.y = fYaw + spreadYaw;
		bTc.rotate.x = fPitch + spreadPitch;
	} else {
		bTc.translate.x += moveX * 2.0f;
		bTc.translate.y += moveY * 2.0f;
		bTc.translate.z += moveZ * 2.0f;
	}

	bTc.scale = { 0.2f, 0.2f, 0.6f };

	auto* renderer = scene->GetRenderer();
	if (renderer) {
		auto& mr = scene->GetRegistry().emplace<MeshRendererComponent>(bullet);
		mr.modelHandle = renderer->LoadObjMesh("Resources/Models/cube/cube.obj");
		mr.textureHandle = renderer->LoadTexture2D("Resources/Textures/white1x1.png");
		mr.color = enhanced ? DirectX::XMFLOAT4{0.4f, 0.9f, 1.0f, 1.0f} : DirectX::XMFLOAT4{1.0f, 0.8f, 0.2f, 1.0f};
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
	if (explode) {
		vc.SetValue("Enhanced", 1.0f);
	}

	// ★強化: マズルでの空間割れエフェクト（着弾時と同等の迫力へ）
	{
		entt::entity muzzleShatter = scene->CreateEntity("MuzzleShatter_VFX");
		auto& msTc = scene->GetRegistry().get<TransformComponent>(muzzleShatter);
		msTc.translate = bTc.translate; // 銃口位置
		
		auto& msVc = scene->GetRegistry().emplace<VariableComponent>(muzzleShatter);
		msVc.SetValue("NormalX", moveX);
		msVc.SetValue("NormalY", moveY);
		msVc.SetValue("NormalZ", moveZ);
		msVc.SetValue("Radius", 2.8f);            // ★スチームの迫力を出すため拡大
		msVc.SetValue("Count", 35.0f); 
		msVc.SetValue("Duration", 0.6f);          // ★少し長めに残る
		msVc.SetValue("ScatterMode", 0.0f);       // ★射撃（マズル）モードに設定
		msVc.SetValue("ScatterDelay", 0.15f);     // ★綺麗な状態を長く(0.15秒)
		msVc.SetValue("ScatterSpeed", 1.5f);      // ★飛びすぎないように調整

		auto& msSc = scene->GetRegistry().emplace<ScriptComponent>(muzzleShatter);
		msSc.scripts.push_back({"SpaceShatterScript", "", nullptr});
	}

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

void PlayerScript::SpawnCrystalBurst(const DirectX::XMFLOAT3& pos, int count, bool enhanced) {
	for (int i = 0; i < count; ++i) {
		CrystalParticle cp;
		cp.pos = pos;
		float angle = (rand() % 360) * 3.14159f / 180.0f;
		float upAngle = (rand() % 100 / 100.0f - 0.3f) * 3.14159f * 0.5f;
		float speed = 3.0f + (rand() % 100 / 100.0f) * 5.0f;
		cp.velocity.x = std::cos(angle) * std::cos(upAngle) * speed;
		cp.velocity.y = std::sin(upAngle) * speed * 0.5f + 1.5f;
		cp.velocity.z = std::sin(angle) * std::cos(upAngle) * speed;
		cp.life = 0.3f + (rand() % 100 / 100.0f) * 0.4f;
		cp.maxLife = cp.life;
		cp.size = 0.05f + (rand() % 100 / 100.0f) * 0.1f;
		cp.rotSpeed = (rand() % 100 / 100.0f - 0.5f) * 20.0f;
		cp.rot = 0.0f;
		if (enhanced) {
			cp.color = {0.3f + (rand() % 100 / 100.0f) * 0.2f, 0.8f + (rand() % 100 / 100.0f) * 0.2f, 1.0f, 1.0f};
			cp.size *= 1.5f;
		} else {
			cp.color = {0.5f + (rand() % 100 / 100.0f) * 0.3f, 0.7f + (rand() % 100 / 100.0f) * 0.3f, 0.9f + (rand() % 100 / 100.0f) * 0.1f, 1.0f};
		}
		crystalParticles_.push_back(cp);
		if (crystalParticles_.size() > 100) crystalParticles_.pop_front();
	}
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
		// ★銃スキル: 移動速度UP + ダメージUP + エフェクト強化バフ
		skillCooldown_ = SKILL_COOLDOWN_TIME;
		isSkillActive_ = true;
		skillDuration_ = SKILL_MAX_DURATION;
		steamPressure_ = maxSteamPressure_; // ★オーバークロック発動時に圧力を100まで戻す
		isRecharging_ = false; // リチャージ状態も強制解除
		std::cout << "Gun Skill Activated: Crystal Enhancement!\n";
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

	// ==== ★追加: クリスタル飛散エフェクト描画 ====
	// (ユーザー要望により、古い青い線のクリスタルは描画しない)
	/*
	for (auto& cp : crystalParticles_) {
		cp.life -= dt;
		cp.pos.x += cp.velocity.x * dt;
		cp.pos.y += cp.velocity.y * dt;
		cp.pos.z += cp.velocity.z * dt;
		cp.velocity.y -= 8.0f * dt;
		cp.rot += cp.rotSpeed * dt;
		if (cp.life > 0.0f) {
			float alpha = (cp.life / cp.maxLife);
			float s = cp.size;
			float c = std::cos(cp.rot);
			float sn = std::sin(cp.rot);
			Engine::Vector3 top    = {cp.pos.x + sn * s, cp.pos.y + c * s, cp.pos.z};
			Engine::Vector3 right  = {cp.pos.x + c * s, cp.pos.y - sn * s, cp.pos.z + s * 0.3f};
			Engine::Vector3 bottom = {cp.pos.x - sn * s, cp.pos.y - c * s, cp.pos.z};
			Engine::Vector3 left   = {cp.pos.x - c * s, cp.pos.y + sn * s, cp.pos.z - s * 0.3f};
			Engine::Vector4 col = {cp.color.x, cp.color.y, cp.color.z, alpha * cp.color.w};
			renderer->DrawLine3D(top, right, col, true);
			renderer->DrawLine3D(right, bottom, col, true);
			renderer->DrawLine3D(bottom, left, col, true);
			renderer->DrawLine3D(left, top, col, true);
			Engine::Vector4 colInner = {cp.color.x * 1.2f, cp.color.y * 1.2f, cp.color.z * 1.2f, alpha * 0.6f};
			renderer->DrawLine3D(top, bottom, colInner, true);
			renderer->DrawLine3D(left, right, colInner, true);
		}
	}
	while (!crystalParticles_.empty() && crystalParticles_.front().life <= 0) crystalParticles_.pop_front();
	*/

	// ==== ★追加: 残像描画 (3Dラインでシルエット) ====
	// (ユーザー要望により、ワイヤーフレームの残像は描画しない)
	/*
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
	*/

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


	// ==== ★追加: スキルバフ中のUI表示 ====
	if (isSkillActive_ && playerType_ == PlayerType::Gun) {
		float remaining = skillDuration_;
		float ratio = remaining / SKILL_MAX_DURATION;
		Engine::Renderer::SdfUIDesc skillBg{};
		skillBg.centerPx = {640.0f, 680.0f};
		skillBg.sizePx = {200.0f, 16.0f};
		skillBg.lineWidth = 0.0f;
		skillBg.glow = 0.0f;
		skillBg.color = {0.05f, 0.05f, 0.1f, 0.7f};
		skillBg.shape = 0;
		skillBg.round = 4.0f;
		skillBg.progress = 1.0f;
		skillBg.fill = 1.0f;
		renderer->DrawSDFUI(skillBg);
		Engine::Renderer::SdfUIDesc skillBar{};
		skillBar.centerPx = {640.0f, 680.0f};
		skillBar.sizePx = {200.0f, 16.0f};
		skillBar.lineWidth = 0.0f;
		skillBar.glow = 3.0f;
		skillBar.color = {0.3f, 0.9f, 1.0f, 1.0f};
		skillBar.shape = 0;
		skillBar.round = 4.0f;
		skillBar.progress = ratio;
		skillBar.fill = 1.0f;
		renderer->DrawSDFUI(skillBar);
		char skillText[64];
		snprintf(skillText, sizeof(skillText), "CRYSTAL ENHANCE  %.1fs", remaining);
		renderer->DrawString(skillText, 560.0f, 674.0f, 0.4f, {0.3f, 0.9f, 1.0f, 1.0f});
	}
}

void PlayerScript::OnDestroy(entt::entity /*entity*/, GameScene* /*scene*/) {}

REGISTER_SCRIPT(PlayerScript);

} // namespace Game
