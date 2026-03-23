#include "PlayerScript.h"
#ifdef USE_IMGUI
#include "../../externals/imgui/imgui.h"
#endif
#include "ObjectTypes.h"
#include "PhaseSystemScript.h"
#include "Scenes/GameScene.h"
#include "ScriptEngine.h"
#include <cmath>
#include <iostream>

namespace Game {

void PlayerScript::Start(entt::entity entity, GameScene* scene) {
	// ★追加: プレイヤーを物理演算から切り離し、CharacterController方式（レイキャスト）で制御する
	if (scene->GetRegistry().all_of<RigidbodyComponent>(entity)) {
		scene->GetRegistry().get<RigidbodyComponent>(entity).isKinematic = true;
	}
	if (scene->GetRegistry().all_of<CharacterMovementComponent>(entity)) {
		scene->GetRegistry().get<CharacterMovementComponent>(entity).heightOffset = 1.0f; // 2m高キャラの中心がy=1になるように
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
		tc.scale = { 0.1f, 0.1f, 1.6f };
		
		auto& cc = scene->GetRegistry().emplace<ColorComponent>(sword);
		cc.color = { 0.9f, 0.9f, 0.9f, 1.0f };

		auto& tcTag = scene->GetRegistry().emplace<TagComponent>(sword);
		tcTag.tag = "PlayerSword";

		auto* renderer = scene->GetRenderer();
		if (renderer) {
			auto& mr = scene->GetRegistry().emplace<MeshRendererComponent>(sword);
			mr.modelHandle = renderer->LoadObjMesh("Resources/cube/cube.obj");
			mr.textureHandle = renderer->LoadTexture2D("Resources/white1x1.png");
			mr.color = { 0.9f, 0.9f, 0.9f, 1.0f };
			mr.enabled = true;
		}

		auto& hb = scene->GetRegistry().emplace<HitboxComponent>(sword);
		hb.isActive = false;
		hb.damage = 25.0f;
		hb.tag = "Sword";
		hb.size = { 1.0f, 1.0f, 1.0f }; // スケールを考慮して1.0に（1.0 * 1.6 = 1.6m）
		hb.enabled = true;
	} else {
		// 既にタグがない場合は追加
		if (!scene->GetRegistry().all_of<TagComponent>(sword)) {
			scene->GetRegistry().emplace<TagComponent>(sword).tag = "PlayerSword";
		}
		// 既にある場合は基本的なプロパティを維持（色はエディタの設定を優先するため上書きしない）
		scene->GetRegistry().get<TransformComponent>(sword).scale = { 0.1f, 0.1f, 1.6f };
		if (scene->GetRegistry().all_of<MeshRendererComponent>(sword)) {
			auto& mr = scene->GetRegistry().get<MeshRendererComponent>(sword);
			// モデル未設定なら設定
			if (mr.modelHandle == 0) {
				auto* renderer = scene->GetRenderer();
				if (renderer) {
					mr.modelHandle = renderer->LoadObjMesh("Resources/cube/cube.obj");
					mr.textureHandle = renderer->LoadTexture2D("Resources/white1x1.png");
				}
			}
		}
	}

	if (scene->GetRegistry().all_of<NameComponent>(entity)) {
		std::cout << "PlayerScript Started: " << scene->GetRegistry().get<NameComponent>(entity).name << std::endl;
	}
}

void PlayerScript::Update(entt::entity entity, GameScene* scene, float dt) {
	if (scene->GetRegistry().all_of<HealthComponent>(entity)) {
		if (scene->GetRegistry().get<HealthComponent>(entity).isDead) return;
	}

	

	if (!isSubscribed_) {
		scene->GetEventSystem().Subscribe("GainGold", [this](float amount) {
			experience_ += amount;

			debugReceiveCount_ += 1;
			debugLastValue_ = amount;
		});

		debugSubscribeCount_ += 1;
		isSubscribed_ = true;
	}

	if (PhaseSystemScript::IsPreparation()) {
		if (scene->GetRegistry().all_of<CameraTargetComponent>(entity)) scene->GetRegistry().get<CameraTargetComponent>(entity).enabled = false;
		if (scene->GetRegistry().all_of<PlayerInputComponent>(entity))  scene->GetRegistry().get<PlayerInputComponent>(entity).enabled = false;
	} else {
		UpdateMovement(entity, scene, dt);
		UpdateAttack(entity, scene, dt);
		UpdateSword(entity, scene, dt);
		if (scene->GetRegistry().all_of<CameraTargetComponent>(entity)) scene->GetRegistry().get<CameraTargetComponent>(entity).enabled = true;
		if (scene->GetRegistry().all_of<PlayerInputComponent>(entity))  scene->GetRegistry().get<PlayerInputComponent>(entity).enabled = true;
	}

#if defined(USE_IMGUI) && !defined(NDEBUG)
ImGui::Begin("Player Debug");

	ImGui::Text("Experience: %.1f", experience_);
	ImGui::Text("Subscribe Count: %d", debugSubscribeCount_);
	ImGui::Text("Receive Count: %d", debugReceiveCount_);
	ImGui::Text("Last Value: %.2f", debugLastValue_);

	ImGui::End();
#endif
}

void PlayerScript::UpdateMovement(entt::entity entity, GameScene* scene, float /*dt*/) {
	if (!scene->GetRegistry().all_of<PlayerInputComponent>(entity)) return;

	auto& input = scene->GetRegistry().get<PlayerInputComponent>(entity);
	
	// 攻撃中や待機状態への戻り動作中は移動速度を大幅に減衰させる
	float speedMul = 1.0f;
	if (isAttacking_) {
		speedMul = 0.2f;
	} else if (isReturning_) {
		speedMul = 0.5f;
	}

	// 入力方向を現在の状態（攻撃中など）に合わせて調整
	input.moveDir.x *= speedMul;
	input.moveDir.y *= speedMul;

	// 回転処理は CharacterMovementSystem に任せる
}

void PlayerScript::UpdateAttack(entt::entity /*entity*/, GameScene* /*scene*/, float dt) {
	bool currentAttackKeyDown = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
	if (currentAttackKeyDown && !prevAttackKeyDown_) {
		attackQueued_ = true;
	}
	prevAttackKeyDown_ = currentAttackKeyDown;

	if (isAttacking_) {
		sheatheTimer_ = 0.0f;
		isSheathed_ = false;
		attackTimer_ -= dt;
		if (attackTimer_ <= 0.0f) {
			if (currentPhase_ == AttackPhase::WindUp) {
				currentPhase_ = AttackPhase::Swing;
				attackTimer_ = (comboCount_ == 3) ? 0.35f : 0.25f;
			} else if (currentPhase_ == AttackPhase::Swing) {
				if (comboCount_ == 3) {
					currentPhase_ = AttackPhase::Recovery;
					attackTimer_ = 0.5f;
				} else {
					if (attackQueued_) {
						comboCount_++;
						currentPhase_ = AttackPhase::WindUp;
						attackTimer_ = 0.2f;
						attackQueued_ = false;
					} else {
						isAttacking_ = false;
						isReturning_ = true;
						returnTimer_ = 0.5f;
						comboCount_ = 0;
					}
				}
			} else if (currentPhase_ == AttackPhase::Recovery) {
				isAttacking_ = false;
				isReturning_ = true;
				returnTimer_ = 0.5f;
				comboCount_ = 0;
			}
			startSwordRot_ = currentSwordRot_;
			startBodyRot_ = currentBodyRot_;
		}
	} else if (isReturning_) {
		returnTimer_ -= dt;
		if (returnTimer_ <= 0.0f) {
			isReturning_ = false;
			sheatheTimer_ = 0.0f; // 納刀タイマー開始
		}
		if (attackQueued_) {
			isAttacking_ = true;
			isReturning_ = false;
			isSheathed_ = false;
			comboCount_ = 1;
			currentPhase_ = AttackPhase::WindUp;
			attackTimer_ = 0.2f;
			attackQueued_ = false;
			startSwordRot_ = currentSwordRot_;
			startBodyRot_ = currentBodyRot_;
		}
	} else {
		// 待機中
		if (attackQueued_) {
			isAttacking_ = true;
			isSheathed_ = false;
			comboCount_ = 1;
			currentPhase_ = AttackPhase::WindUp;
			attackTimer_ = 0.2f;
			attackQueued_ = false;
			startSwordRot_ = currentSwordRot_;
			startBodyRot_ = currentBodyRot_;
		} else {
			// 自動納刀処理
			if (!isSheathed_) {
				sheatheTimer_ += dt;
				if (sheatheTimer_ >= AUTO_SHEATHE_TIME) {
					isSheathed_ = true;
				}
			}
		}
	}
}

void PlayerScript::UpdateSword(entt::entity entity, GameScene* scene, float /*dt*/) {
	entt::entity sword = entt::null;
	auto view = scene->GetRegistry().view<NameComponent>();
	for (auto e : view) {
		if (view.get<NameComponent>(e).name == swordName_) {
			sword = e;
			break;
		}
	}
	if (sword == entt::null) return;

	DirectX::XMFLOAT3 currentSwordRotRad = {0, 0, 0};
	bool hitboxActive = false;

	if (isAttacking_) {
		float t = 0.0f;
		DirectX::XMFLOAT3 swordStart = startSwordRot_;
		DirectX::XMFLOAT3 swordEnd = {0, 0, 0};
		DirectX::XMFLOAT3 bodyEnd = {0, 0, 0};

		if (comboCount_ == 1) { // 1段目
			if (currentPhase_ == AttackPhase::WindUp) {
				t = EaseOutCubic(1.0f - (attackTimer_ / 0.2f));
				swordEnd.x = 5;
				swordEnd.y = 110;
				swordEnd.z = -20;
				bodyEnd.y = DirectX::XMConvertToRadians(-25.0f);
			} else {
				t = EaseOutExpo(1.0f - (attackTimer_ / 0.25f));
				swordStart.x = 5;
				swordStart.y = 110;
				swordStart.z = -20;
				swordEnd.x = 0;
				swordEnd.y = -110;
				swordEnd.z = 20;
				bodyEnd.y = DirectX::XMConvertToRadians(30.0f);
				bodyEnd.z = DirectX::XMConvertToRadians(5.0f);
				hitboxActive = true;
			}
		} else if (comboCount_ == 2) { // 2段目
			if (currentPhase_ == AttackPhase::WindUp) {
				t = EaseOutCubic(1.0f - (attackTimer_ / 0.2f));
				swordEnd.x = 50;
				swordEnd.y = -60;
				swordEnd.z = 20;
				bodyEnd.x = DirectX::XMConvertToRadians(10.0f);
			} else {
				t = EaseOutBack(1.0f - (attackTimer_ / 0.25f));
				swordStart.x = 50;
				swordStart.y = -60;
				swordStart.z = 20;
				swordEnd.x = -40;
				swordEnd.y = 50;
				swordEnd.z = -20;
				bodyEnd.x = DirectX::XMConvertToRadians(-15.0f);
				bodyEnd.y = DirectX::XMConvertToRadians(-10.0f);
				hitboxActive = true;
			}
		} else if (comboCount_ == 3) { // 3段目
			if (currentPhase_ == AttackPhase::WindUp) {
				t = EaseOutCubic(1.0f - (attackTimer_ / 0.25f));
				swordEnd.x = -130;
				swordEnd.y = 10;
				swordEnd.z = 0;
				bodyEnd.x = DirectX::XMConvertToRadians(-25.0f);
			} else if (currentPhase_ == AttackPhase::Swing) {
				t = EaseOutQuint(1.0f - (attackTimer_ / 0.35f));
				swordStart.x = -130;
				swordStart.y = 10;
				swordStart.z = 0;
				swordEnd.x = 90;
				swordEnd.y = 0;
				swordEnd.z = 0;
				bodyEnd.x = DirectX::XMConvertToRadians(40.0f);
				hitboxActive = true;
			} else {
				t = 1.0;
				swordEnd.x = 90;
				bodyEnd.x = DirectX::XMConvertToRadians(35.0f);
			}
		}

		currentSwordRot_.x = swordStart.x + (swordEnd.x - swordStart.x) * t;
		currentSwordRot_.y = swordStart.y + (swordEnd.y - swordStart.y) * t;
		currentSwordRot_.z = swordStart.z + (swordEnd.z - swordStart.z) * t;

		currentBodyRot_.x = startBodyRot_.x + (bodyEnd.x - startBodyRot_.x) * t;
		currentBodyRot_.y = startBodyRot_.y + (bodyEnd.y - startBodyRot_.y) * t;
		currentBodyRot_.z = startBodyRot_.z + (bodyEnd.z - startBodyRot_.z) * t;

	} else if (isReturning_) {
		float t = EaseOutCubic(1.0f - (returnTimer_ / 0.5f));
		currentSwordRot_.x = startSwordRot_.x * (1.0f - t);
		currentSwordRot_.y = startSwordRot_.y * (1.0f - t);
		currentSwordRot_.z = startSwordRot_.z * (1.0f - t);
		currentBodyRot_.x = startBodyRot_.x * (1.0f - t);
		currentBodyRot_.y = startBodyRot_.y * (1.0f - t);
		currentBodyRot_.z = startBodyRot_.z * (1.0f - t);
	} else {
		// 待機中
		currentSwordRot_.x = 0;
		currentSwordRot_.y = 0;
		currentSwordRot_.z = 0;
		currentBodyRot_.x = 0;
		currentBodyRot_.y = 0;
		currentBodyRot_.z = 0;
	}

	currentSwordRotRad.x = DirectX::XMConvertToRadians(currentSwordRot_.x);
	currentSwordRotRad.y = DirectX::XMConvertToRadians(currentSwordRot_.y);
	currentSwordRotRad.z = DirectX::XMConvertToRadians(currentSwordRot_.z);

	auto& objTc = scene->GetRegistry().get<TransformComponent>(entity);
	auto& swordTc = scene->GetRegistry().get<TransformComponent>(sword);

	objTc.rotate.x = currentBodyRot_.x;
	objTc.rotate.z = currentBodyRot_.z;

	if (isSheathed_) {
		// 背中に背負う（斜め）
		float s = std::sin(objTc.rotate.y);
		float c = std::cos(objTc.rotate.y);
		
		float backX = -0.2f; float backY = 0.8f; float backZ = -0.4f;
		swordTc.translate.x = objTc.translate.x + (backX * c + backZ * s);
		swordTc.translate.y = objTc.translate.y + backY;
		swordTc.translate.z = objTc.translate.z + (-backX * s + backZ * c);
		
		swordTc.rotate.x = DirectX::XMConvertToRadians(45.0f);
		swordTc.rotate.y = objTc.rotate.y + DirectX::XMConvertToRadians(0.0f);
		swordTc.rotate.z = DirectX::XMConvertToRadians(30.0f);
	} else {
		// 手元
		float handX = 0.9f; float handY = 0.5f; float handZ = 1.2f;
		float baseRotY = objTc.rotate.y + currentBodyRot_.y;
		float sy = std::sin(baseRotY);
		float cy = std::cos(baseRotY);
		
		float pivotX = objTc.translate.x + (handX * cy + handZ * sy);
		float pivotY = objTc.translate.y + handY;
		float pivotZ = objTc.translate.z + (-handX * sy + handZ * cy);

		float swordLength = swordTc.scale.z;
		float halfLength = swordLength * 0.5f;

		float totalRotX = currentSwordRotRad.x + objTc.rotate.x;
		float totalRotY = baseRotY + currentSwordRotRad.y;
		float totalRotZ = currentSwordRotRad.z + objTc.rotate.z;

		float dirX = std::sin(totalRotY) * std::cos(totalRotX);
		float dirY = -std::sin(totalRotX);
		float dirZ = std::cos(totalRotY) * std::cos(totalRotX);

		swordTc.translate.x = pivotX + dirX * halfLength;
		swordTc.translate.y = pivotY + dirY * halfLength;
		swordTc.translate.z = pivotZ + dirZ * halfLength;

		swordTc.rotate.x = totalRotX;
		swordTc.rotate.y = totalRotY;
		swordTc.rotate.z = totalRotZ;
	}

	if (scene->GetRegistry().all_of<HitboxComponent>(sword)) {
		scene->GetRegistry().get<HitboxComponent>(sword).isActive = hitboxActive;
	}
}

void PlayerScript::OnDestroy(entt::entity /*entity*/, GameScene* /*scene*/) {}

// ★追加: この1行を書くだけでエンジンに自動認識されます！
REGISTER_SCRIPT(PlayerScript);

} // namespace Game