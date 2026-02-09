#include "JumpPad.h"
#include "../Actors/PlayerBall.h" // PlayerBall操作用
#include "GimmickFactory.h"
#include "imgui.h" // ImGui用
#include <cmath>
#include <iostream>
#include <string>

namespace Game {

// テンプレートクラスとして実体化
static GimmickRegistrar<JumpPad> registrar("JumpPad");

void JumpPad::Start(Engine::GameObject* owner) {
	GimmickBase::Start(owner);
	if (owner_) {
		originalScaleY_ = owner_->transform.scale.y;
	}
}

void JumpPad::OnCollision(void* other) {
	// void* を PlayerBall* にキャスト
	PlayerBall* player = static_cast<PlayerBall*>(other);

	if (player) {
		// ★ジャンプ処理
		// プレイヤーの速度を取得し、Y成分（上方向）を書き換える
		Engine::Vector3 vel = player->GetVelocity();
		vel.y = jumpPower_;
		player->SetVelocity(vel);

		// アニメーション開始（連打防止のためTriggeredチェックしても良いが、バネは何度でも跳ねるべきなので上書きする）
		isTriggered_ = true;
		animationTime_ = 0.0f;

		// ログ出力 (デバッグ用)
		// std::cout << "[JumpPad] Boing!! Power:" << jumpPower_ << std::endl;
	}
}

void JumpPad::Update() {
	if (isTriggered_ && owner_) {
		animationTime_ += 1.0f / 60.0f;
		const float kDuration = 0.5f;

		if (animationTime_ < kDuration) {
			float t = animationTime_ / kDuration;

			// バネのような伸縮アニメーション
			// 0.0 -> 0.2 : 縮む (エネルギー溜め)
			// 0.2 -> 1.0 : 伸びてビヨヨンと戻る (減衰振動)
			float scale = 1.0f;
			if (t < 0.2f) {
				// 縮むフェーズ
				float subT = t / 0.2f;
				scale = 1.0f - (0.4f * subT); // 最大40%縮む
			} else {
				// 伸びるフェーズ (減衰振動)
				float subT = (t - 0.2f) / 0.8f; // 0.0 -> 1.0
				// sin波で揺らす
				scale = 1.0f + (0.4f * std::cos(subT * 15.0f) * (1.0f - subT));
			}
			owner_->transform.scale.y = originalScaleY_ * scale;

		} else {
			// アニメーション終了
			owner_->transform.scale.y = originalScaleY_;
			isTriggered_ = false;
		}
	}
}

void JumpPad::OnInspectorGUI() {
	// ImGuiでパラメータ調整
	ImGui::DragFloat("Jump Power", &jumpPower_, 0.1f, 0.0f, 100.0f);
}

// ★追加: パラメータをCSV文字列として保存
std::string JumpPad::SaveParameter() {
	// float1つだけなのでシンプルに変換
	return std::to_string(jumpPower_);
}

// ★追加: 文字列からパラメータを復元
void JumpPad::LoadParameter(const std::string& param) {
	if (param.empty())
		return;

	try {
		jumpPower_ = std::stof(param);
	} catch (...) {
		// 変換失敗時は何もしない
	}
}

} // namespace Game