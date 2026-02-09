//======================================================
// JumpPadGimmick.h
//======================================================
#pragma once
#include "GimmickBase.h"
#include <string>

#include "../../Engine/Audio.h"

namespace Game {
class PlayerBall;
class JumpPadGimmick : public GimmickBase {
public:
	// ★修正: cpp側のRegistrarの名前と一致させる ("JumpPad" -> "JumpPadGimmick")
	std::string GetGimmickName() const override { return "JumpPadGimmick"; }

	void Start(Engine::GameObject* owner) override;
	void Update() override;
	void OnCollision(void* other) override;
	void OnInspectorGUI() override;

	// ★追加: 保存と読み込みの実装宣言
	std::string SaveParameter() override;
	void LoadParameter(const std::string& param) override;

private:
	float radius_ = 1.0f;

	float jumpPower_ = 1.3f;
	float cooldownSec_ = 0.15f;
	bool onlyWhenFalling_ = true;

	float heightTolerance_ = 0.3f;
	float cooldownTimer_ = 0.0f;
	bool wasInside_ = false;
	PlayerBall* lastPlayer_ = nullptr;

	// ★追加: ジャンプ音のハンドル
	uint32_t seJumpHandle_ = 0;
};

} // namespace Game