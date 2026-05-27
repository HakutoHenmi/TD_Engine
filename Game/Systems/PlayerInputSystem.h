#pragma once
#include "ISystem.h"
#include <Windows.h>
#include <cmath>
#include "../Scripts/PhaseSystemScript.h"
#include "../../Engine/Input.h"

namespace Game {

class PlayerInputSystem : public ISystem {
public:
	void Update(entt::registry& registry, GameContext& ctx) override {
		if (!ctx.isPlaying) return;
		// ★追加: エディタUI操作中やインサート演出中はゲーム入力を無視
		if (ctx.input && ctx.input->IsGameInputBlocked()) return;
		if (Game::PhaseSystemScript::IsPhase() == Game::PhaseSystemScript::InsertPhase) return;

		auto view = registry.view<PlayerInputComponent>();
		for (auto entity : view) {
			auto& pi = registry.get<PlayerInputComponent>(entity);
			if (!pi.enabled) continue;

			if (ctx.input) {
				auto* input = ctx.input;
				DirectX::XMFLOAT2 moveDir = {0.0f, 0.0f};
				if (input->Down(DIK_W)) moveDir.y += 1.0f;
				if (input->Down(DIK_S)) moveDir.y -= 1.0f;
				if (input->Down(DIK_A)) moveDir.x -= 1.0f;
				if (input->Down(DIK_D)) moveDir.x += 1.0f;

				// コントローラー左スティック
				float padX = input->GetLeftStickX();
				float padY = input->GetLeftStickY();
				if (std::abs(padX) > 0.1f) moveDir.x += padX;
				if (std::abs(padY) > 0.1f) moveDir.y += padY;

				float len = std::sqrt(moveDir.x * moveDir.x + moveDir.y * moveDir.y);
				if (len > 0.001f) {
					moveDir.x /= len;
					moveDir.y /= len;
				}
				pi.moveDir = moveDir;

				// ジャンプ入力
				bool currentSpace = input->Down(DIK_SPACE) || input->IsControllerButtonDown(XINPUT_GAMEPAD_A);
				if (currentSpace && !prevSpace_)
					pi.jumpRequested = true;
				else
					pi.jumpRequested = false;
				prevSpace_ = currentSpace;

				// 攻撃入力
				pi.attackRequested = input->Down(DIK_J);

				// Shiftダッシュ入力
				pi.sprintRequested = input->Down(DIK_LSHIFT);

				// ★追加: 準備フェーズ中はジャンプ・攻撃・ダッシュ等のキャラクターアクション入力を無効化する（移動・視点操作は許可）
				if (PhaseSystemScript::IsPhase() == PhaseSystemScript::PreparationPhase) {
					pi.jumpRequested = false;
					pi.attackRequested = false;
					pi.sprintRequested = false;
				}

				// ★変更: カメラ操作
				// 準備フェーズの場合は右クリック押下時のみ視点移動可能にする
				pi.cameraYaw = 0.0f;
				pi.cameraPitch = 0.0f;
				
				bool canMoveCamera = true;
				bool hasRightStickInput = (std::abs(input->GetRightStickX()) > 0.1f || std::abs(input->GetRightStickY()) > 0.1f);
				if (PhaseSystemScript::IsPhase() == PhaseSystemScript::PreparationPhase) {
					canMoveCamera = input->IsMouseDown(1) || hasRightStickInput; 
				}
				
				if (canMoveCamera) {
					pi.cameraYaw = input->GetMouseDeltaX() * 0.003f;
					pi.cameraPitch = input->GetMouseDeltaY() * 0.003f;
					
					pi.cameraYaw += input->GetRightStickX() * 0.05f;
					pi.cameraPitch -= input->GetRightStickY() * 0.05f;
				}
			}
		}
	}

	void Reset(entt::registry& /*registry*/) override {
		prevSpace_ = false;
	}

private:
	bool prevSpace_ = false;
};

} // namespace Game
