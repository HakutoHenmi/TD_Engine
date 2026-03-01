#pragma once
#include "ISystem.h"
#include <cmath>

namespace Game {

class CharacterMovementSystem : public ISystem {
public:
	void Update(std::vector<SceneObject>& objects, GameContext& ctx) override {
		if (!ctx.isPlaying) return;

		for (auto& obj : objects) {
			DirectX::XMFLOAT2 moveDir = {0, 0};
			bool wantJump = false;
			if (!obj.playerInputs.empty() && obj.playerInputs[0].enabled) {
				moveDir = obj.playerInputs[0].moveDir;
				wantJump = obj.playerInputs[0].jumpRequested;
			}

			for (auto& cm : obj.characterMovements) {
				if (!cm.enabled) continue;

				auto camRot = ctx.camera->Rotation();
				float cy = std::cos(camRot.y);
				float sy = std::sin(camRot.y);

				float moveX = moveDir.x * cy + moveDir.y * sy;
				float moveZ = -moveDir.x * sy + moveDir.y * cy;

				obj.translate.x += moveX * cm.speed * ctx.dt;
				obj.translate.z += moveZ * cm.speed * ctx.dt;

				if (std::abs(moveDir.x) > 0.01f || std::abs(moveDir.y) > 0.01f) {
					float targetYaw = std::atan2(moveX, moveZ);
					obj.rotate.y = targetYaw;
				}

				if (!cm.isGrounded) {
					cm.velocityY -= cm.gravity * ctx.dt;
				}

				if (cm.isGrounded && wantJump) {
					cm.velocityY = cm.jumpPower;
					cm.isGrounded = false;
				}

				obj.translate.y += cm.velocityY * ctx.dt;

				// 床判定(1.0f)は削除（PhysicsSystemでコライダー同士が処理する）
			}
		}
	}

	void Reset(std::vector<SceneObject>& objects) override {
		for (auto& obj : objects) {
			for (auto& cm : obj.characterMovements) {
				cm.velocityY = 0.0f;
				cm.isGrounded = false;
			}
		}
	}
};

} // namespace Game
