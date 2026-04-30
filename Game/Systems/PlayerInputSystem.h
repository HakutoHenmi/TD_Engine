#pragma once
#include "ISystem.h"
#include <Windows.h>
#include <cmath>
#include "../Scripts/PhaseSystemScript.h"

namespace Game {

class PlayerInputSystem : public ISystem {
public:
	void Update(entt::registry& registry, GameContext& ctx) override {
		if (!ctx.isPlaying) return;
		// ★追加: エディタUI操作中はゲーム入力を無視
		if (ctx.input && ctx.input->IsGameInputBlocked()) return;

		auto view = registry.view<PlayerInputComponent>();
		for (auto entity : view) {
			auto& pi = registry.get<PlayerInputComponent>(entity);
			if (!pi.enabled) continue;

			DirectX::XMFLOAT2 moveDir = {0.0f, 0.0f};
			if (GetAsyncKeyState('W') & 0x8000) moveDir.y += 1.0f;
			if (GetAsyncKeyState('S') & 0x8000) moveDir.y -= 1.0f;
			if (GetAsyncKeyState('A') & 0x8000) moveDir.x -= 1.0f;
			if (GetAsyncKeyState('D') & 0x8000) moveDir.x += 1.0f;

			float len = std::sqrt(moveDir.x * moveDir.x + moveDir.y * moveDir.y);
			if (len > 0.001f) {
				moveDir.x /= len;
				moveDir.y /= len;
			}
			pi.moveDir = moveDir;

			// ジャンプ入力
			bool currentSpace = (GetAsyncKeyState(VK_SPACE) & 0x8000) != 0;
			if (currentSpace && !prevSpace_)
				pi.jumpRequested = true;
			else
				pi.jumpRequested = false;
			prevSpace_ = currentSpace;

			// 攻撃入力
			pi.attackRequested = (GetAsyncKeyState('J') & 0x8000) != 0;

			// ★追加: Shiftダッシュ入力
			pi.sprintRequested = (GetAsyncKeyState(VK_LSHIFT) & 0x8000) != 0;

			// ★変更: カメラ操作
			// 準備フェーズの場合は右クリック押下時のみ視点移動可能にする
			pi.cameraYaw = 0.0f;
			pi.cameraPitch = 0.0f;
			if (ctx.input) {
				bool canMoveCamera = true;
				if (PhaseSystemScript::IsPhase() == PhaseSystemScript::PreparationPhase) {
					canMoveCamera = (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0;
				}
				
				if (canMoveCamera) {
					pi.cameraYaw = ctx.input->GetMouseDeltaX() * 0.003f;
					pi.cameraPitch = ctx.input->GetMouseDeltaY() * 0.003f;
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
