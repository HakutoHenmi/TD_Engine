#include "PlayerScript.h"
#include "ObjectTypes.h"
#include "Scenes/GameScene.h"
#include "ScriptEngine.h"
#include <iostream>
#include <cmath>

namespace Game {

void PlayerScript::Start(SceneObject& obj, GameScene* scene) {
	// 剣がシーンに既にあるか確認
	SceneObject* sword = nullptr;
	auto& objects = const_cast<std::vector<SceneObject>&>(scene->GetObjects());
	for (auto& o : objects) {
		if (o.name == swordName_) {
			sword = &o;
			break;
		}
	}

	if (!sword) {
		// 剣がなければ生成
		SceneObject newSword;
		newSword.name = swordName_;
		newSword.color = { 0.9f, 0.9f, 0.9f, 1.0f };
		newSword.scale = { 0.1f, 0.1f, 1.6f };

		auto* renderer = scene->GetRenderer();
		if (renderer) {
			newSword.modelHandle = renderer->LoadObjMesh("Resources/cube/cube.obj");
			newSword.textureHandle = renderer->LoadTexture2D("Resources/white1x1.png");
			MeshRendererComponent mr;
			mr.modelHandle = newSword.modelHandle;
			mr.textureHandle = newSword.textureHandle;
			mr.color = newSword.color;
			newSword.meshRenderers.push_back(mr);
		}

		HitboxComponent hb;
		hb.isActive = false;
		hb.damage = 25.0f;
		hb.tag = "Sword";
		hb.size = { 0.5f, 0.5f, 3.0f };
		newSword.hitboxes.push_back(hb);

		scene->SpawnObject(newSword);
	} else {
		// 既にある場合は基本的なプロパティを維持（色はエディタの設定を優先するため上書きしない）
		sword->scale = { 0.1f, 0.1f, 1.6f };
		if (!sword->meshRenderers.empty()) {
			// モデル未設定なら設定
			if (sword->modelPath == "" || sword->modelPath.empty()) {
				auto* renderer = scene->GetRenderer();
				if (renderer) {
					sword->modelHandle = renderer->LoadObjMesh("Resources/cube/cube.obj");
					sword->textureHandle = renderer->LoadTexture2D("Resources/white1x1.png");
					sword->meshRenderers[0].modelHandle = sword->modelHandle;
					sword->meshRenderers[0].textureHandle = sword->textureHandle;
				}
			}
		}
	}

	std::cout << "PlayerScript Started: " << obj.name << std::endl;
}

void PlayerScript::Update(SceneObject& obj, GameScene* scene, float dt) {
	if (!obj.healths.empty() && obj.healths[0].isDead) return;

	UpdateMovement(obj, scene, dt);
	UpdateAttack(obj, scene, dt);
	UpdateSword(obj, scene, dt);
}

void PlayerScript::UpdateMovement(SceneObject& obj, GameScene* /*scene*/, float dt) {
	bool keyW = (GetAsyncKeyState('W') & 0x8000) != 0;
	bool keyS = (GetAsyncKeyState('S') & 0x8000) != 0;
	bool keyA = (GetAsyncKeyState('A') & 0x8000) != 0;
	bool keyD = (GetAsyncKeyState('D') & 0x8000) != 0;
	bool keySpace = (GetAsyncKeyState(VK_SPACE) & 0x8000) != 0;

	float speedMul = isAttacking_ ? 0.3f : 1.0f;

	DirectX::XMFLOAT3 move = { 0, 0, 0 };
	if (keyW) move.z += speedMul;
	if (keyS) move.z -= speedMul;
	if (keyA) move.x -= speedMul;
	if (keyD) move.x += speedMul;

	if (!obj.playerInputs.empty()) {
		auto& input = obj.playerInputs[0];
		input.moveDir = { move.x, move.z };
		input.jumpRequested = keySpace;
	}

	if (std::abs(move.x) > 0.1f || std::abs(move.z) > 0.1f) {
		float targetAngle = std::atan2(move.x, move.z);
		float angleDiff = targetAngle - obj.rotate.y;
		while (angleDiff >  DirectX::XM_PI) angleDiff -= DirectX::XM_2PI;
		while (angleDiff < -DirectX::XM_PI) angleDiff += DirectX::XM_2PI;
		obj.rotate.y += angleDiff * 10.0f * dt;
	}
}

void PlayerScript::UpdateAttack(SceneObject& /*obj*/, GameScene* /*scene*/, float dt) {
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

void PlayerScript::UpdateSword(SceneObject& obj, GameScene* scene, float /*dt*/) {
	SceneObject* sword = nullptr;
	auto& objects = const_cast<std::vector<SceneObject>&>(scene->GetObjects());
	for (auto& o : objects) {
		if (o.name == swordName_) {
			sword = &o;
			break;
		}
	}
	if (!sword) return;

	DirectX::XMFLOAT3 currentSwordRotRad = { 0, 0, 0 };
	bool hitboxActive = false;

	if (isAttacking_) {
		float t = 0.0f;
		DirectX::XMFLOAT3 swordStart = startSwordRot_;
		DirectX::XMFLOAT3 swordEnd = { 0, 0, 0 };
		DirectX::XMFLOAT3 bodyEnd = { 0, 0, 0 };

		if (comboCount_ == 1) { // 1段目
			if (currentPhase_ == AttackPhase::WindUp) {
				t = EaseOutCubic(1.0f - (attackTimer_ / 0.2f));
				swordEnd.x = 5; swordEnd.y = 110; swordEnd.z = -20;
				bodyEnd.y = DirectX::XMConvertToRadians(-25.0f);
			} else {
				t = EaseOutExpo(1.0f - (attackTimer_ / 0.25f));
				swordStart.x = 5; swordStart.y = 110; swordStart.z = -20;
				swordEnd.x = 0; swordEnd.y = -110; swordEnd.z = 20;
				bodyEnd.y = DirectX::XMConvertToRadians(30.0f);
				bodyEnd.z = DirectX::XMConvertToRadians(5.0f);
				hitboxActive = true;
			}
		} else if (comboCount_ == 2) { // 2段目
			if (currentPhase_ == AttackPhase::WindUp) {
				t = EaseOutCubic(1.0f - (attackTimer_ / 0.2f));
				swordEnd.x = 50; swordEnd.y = -60; swordEnd.z = 20;
				bodyEnd.x = DirectX::XMConvertToRadians(10.0f);
			} else {
				t = EaseOutBack(1.0f - (attackTimer_ / 0.25f));
				swordStart.x = 50; swordStart.y = -60; swordStart.z = 20;
				swordEnd.x = -40; swordEnd.y = 50; swordEnd.z = -20;
				bodyEnd.x = DirectX::XMConvertToRadians(-15.0f);
				bodyEnd.y = DirectX::XMConvertToRadians(-10.0f);
				hitboxActive = true;
			}
		} else if (comboCount_ == 3) { // 3段目
			if (currentPhase_ == AttackPhase::WindUp) {
				t = EaseOutCubic(1.0f - (attackTimer_ / 0.25f));
				swordEnd.x = -130; swordEnd.y = 10; swordEnd.z = 0;
				bodyEnd.x = DirectX::XMConvertToRadians(-25.0f);
			} else if (currentPhase_ == AttackPhase::Swing) {
				t = EaseOutQuint(1.0f - (attackTimer_ / 0.35f));
				swordStart.x = -130; swordStart.y = 10; swordStart.z = 0;
				swordEnd.x = 90; swordEnd.y = 0; swordEnd.z = 0;
				bodyEnd.x = DirectX::XMConvertToRadians(40.0f);
				hitboxActive = true;
			} else {
				t = 1.0;
				swordEnd.x = 90; bodyEnd.x = DirectX::XMConvertToRadians(35.0f);
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
		currentSwordRot_.x = 0; currentSwordRot_.y = 0; currentSwordRot_.z = 0;
		currentBodyRot_.x = 0; currentBodyRot_.y = 0; currentBodyRot_.z = 0;
	}

	currentSwordRotRad.x = DirectX::XMConvertToRadians(currentSwordRot_.x);
	currentSwordRotRad.y = DirectX::XMConvertToRadians(currentSwordRot_.y);
	currentSwordRotRad.z = DirectX::XMConvertToRadians(currentSwordRot_.z);

	obj.rotate.x = currentBodyRot_.x;
	obj.rotate.z = currentBodyRot_.z;

	if (isSheathed_) {
		// 背中に背負う（斜め）
		float s = std::sin(obj.rotate.y);
		float c = std::cos(obj.rotate.y);
		
		float backX = -0.2f; float backY = 0.8f; float backZ = -0.4f;
		sword->translate.x = obj.translate.x + (backX * c + backZ * s);
		sword->translate.y = obj.translate.y + backY;
		sword->translate.z = obj.translate.z + (-backX * s + backZ * c);
		
		sword->rotate.x = DirectX::XMConvertToRadians(45.0f);
		sword->rotate.y = obj.rotate.y + DirectX::XMConvertToRadians(0.0f);
		sword->rotate.z = DirectX::XMConvertToRadians(30.0f);
	} else {
		// 手元
		float handX = 0.9f; float handY = 0.5f; float handZ = 1.2f;
		float baseRotY = obj.rotate.y + currentBodyRot_.y;
		float sy = std::sin(baseRotY);
		float cy = std::cos(baseRotY);
		
		float pivotX = obj.translate.x + (handX * cy + handZ * sy);
		float pivotY = obj.translate.y + handY;
		float pivotZ = obj.translate.z + (-handX * sy + handZ * cy);

		float swordLength = sword->scale.z;
		float halfLength = swordLength * 0.5f;

		float totalRotX = currentSwordRotRad.x + obj.rotate.x;
		float totalRotY = baseRotY + currentSwordRotRad.y;
		float totalRotZ = currentSwordRotRad.z + obj.rotate.z;

		float dirX = std::sin(totalRotY) * std::cos(totalRotX);
		float dirY = -std::sin(totalRotX);
		float dirZ = std::cos(totalRotY) * std::cos(totalRotX);

		sword->translate.x = pivotX + dirX * halfLength;
		sword->translate.y = pivotY + dirY * halfLength;
		sword->translate.z = pivotZ + dirZ * halfLength;

		sword->rotate.x = totalRotX;
		sword->rotate.y = totalRotY;
		sword->rotate.z = totalRotZ;
	}

	if (!sword->hitboxes.empty()) {
		sword->hitboxes[0].isActive = hitboxActive;
	}
}

void PlayerScript::OnDestroy(SceneObject& /*obj*/, GameScene* /*scene*/) {}

// ★追加: この1行を書くだけでエンジンに自動認識されます！
REGISTER_SCRIPT(PlayerScript);

} // namespace Game