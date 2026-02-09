//======================================================
// JumpPadGimmick.cpp
//======================================================
#include "JumpPadGimmick.h"
#include "../Actors/PlayerBall.h"
#include "GimmickFactory.h"
#include "imgui.h"
#include <cmath>
#include <sstream> // ★追加
#include <string>  // ★追加
#include <vector>  // ★追加

namespace Game {

// 登録名。GetGimmickName()と一致させる必要があります。
static GimmickRegistrar<JumpPadGimmick> registrar("JumpPadGimmick");

void JumpPadGimmick::Start(Engine::GameObject* owner) {
	GimmickBase::Start(owner);

	// ★追加: ジャンプ音ロード
	auto* audio = Engine::Audio::GetInstance();
	seJumpHandle_ = audio->Load("Resources/Sound/janpu.mp3");
}

void JumpPadGimmick::Update() {
	float dt = 1.0f / 60.0f;

	if (cooldownTimer_ > 0.0f) {
		cooldownTimer_ -= dt;
		if (cooldownTimer_ < 0.0f)
			cooldownTimer_ = 0.0f;
	}

	// 離れていれば wasInside_ を必ず false に戻す
	if (owner_ && lastPlayer_) {
		const Engine::Vector3 padPos = owner_->transform.translate;
		const Engine::Vector3 ppos = lastPlayer_->GetPosition();

		float dx = ppos.x - padPos.x;
		float dz = ppos.z - padPos.z;
		float dist2 = dx * dx + dz * dz;
		float r = radius_ + lastPlayer_->GetRadius();

		if (dist2 > r * r + 0.1f) {
			wasInside_ = false;
			lastPlayer_ = nullptr;
		}
	}
}

void JumpPadGimmick::OnCollision(void* other) {
	if (!owner_)
		return;

	PlayerBall* player = static_cast<PlayerBall*>(other);
	if (!player)
		return;

	lastPlayer_ = player;

	// クールダウン中は無視
	if (cooldownTimer_ > 0.0f)
		return;

	// 高さ判定
	float pY = player->GetPosition().y;
	float padY = owner_->transform.translate.y;
	// プレイヤーがパッドより極端に下にいるなら無視
	// (padY - pY) が大きい = プレイヤーが下
	if ((padY - pY) > heightTolerance_)
		return;
	// プレイヤーがパッドより高すぎる場合も無視するかはゲーム性次第だが
	// ここでは「パッドに乗った」判定なので、足元付近であることをチェック
	if (std::abs(pY - padY) > (heightTolerance_ + player->GetRadius())) {
		// return; // 必要に応じてコメントアウト解除
	}

	// 半径判定 (XZ)
	const Engine::Vector3 padPos = owner_->transform.translate;
	const Engine::Vector3 ppos = player->GetPosition();
	float dx = ppos.x - padPos.x;
	float dz = ppos.z - padPos.z;
	float dist2 = dx * dx + dz * dz;
	float r = radius_ + player->GetRadius();

	bool inside = (dist2 <= r * r);

	if (inside && !wasInside_) {
		// 落下中のみフラグ
		if (onlyWhenFalling_) {
			float vy = player->GetVelocity().y;
			if (vy > 0.1f) { // 上昇中は無視
				wasInside_ = true;
				return;
			}
		}

		// ジャンプ処理
		// Y速度を上書き
		auto v = player->GetVelocity();
		v.y = jumpPower_; // 固定値で飛ばす
		player->SetVelocity(v);

		// ★追加: ジャンプ音再生
		Engine::Audio::GetInstance()->Play(seJumpHandle_);

		// 位置補正（埋まり防止のため、パッドの上に持ち上げる）
		{
			auto p = player->GetPosition();
			p.y = padY + 0.5f; // 少し上に
			player->SetPosition(p);
		}

		player->SetGrounded(false);

		cooldownTimer_ = cooldownSec_;

		wasInside_ = true; // 連打防止
		return;
	}

	wasInside_ = inside;
}

void JumpPadGimmick::OnInspectorGUI() {
	ImGui::DragFloat("Radius", &radius_, 0.01f, 0.1f, 10.0f);
	ImGui::DragFloat("Jump Power", &jumpPower_, 0.1f, 0.0f, 100.0f);
	ImGui::DragFloat("Cooldown Sec", &cooldownSec_, 0.01f, 0.0f, 5.0f);
	ImGui::Checkbox("Only When Falling", &onlyWhenFalling_);
	ImGui::DragFloat("Height Tolerance", &heightTolerance_, 0.01f, 0.0f, 5.0f);

	// デバッグ表示
	ImGui::Text("Cooldown: %.3f", cooldownTimer_);
	ImGui::Text("Inside: %d", (int)wasInside_);
}

// ★追加: パラメータをCSV文字列として保存
std::string JumpPadGimmick::SaveParameter() {
	std::stringstream ss;
	// 順序: radius, jumpPower, cooldownSec, onlyWhenFalling, heightTolerance
	ss << radius_ << "," << jumpPower_ << "," << cooldownSec_ << "," << (onlyWhenFalling_ ? 1 : 0) << "," << heightTolerance_;
	return ss.str();
}

// ★追加: 文字列からパラメータを復元
void JumpPadGimmick::LoadParameter(const std::string& param) {
	if (param.empty())
		return;

	std::stringstream ss(param);
	std::string segment;
	std::vector<std::string> seglist;
	while (std::getline(ss, segment, ',')) {
		seglist.push_back(segment);
	}

	// 5つ以上のデータがあれば読み込む
	if (seglist.size() >= 5) {
		try {
			radius_ = std::stof(seglist[0]);
			jumpPower_ = std::stof(seglist[1]);
			cooldownSec_ = std::stof(seglist[2]);
			onlyWhenFalling_ = (std::stoi(seglist[3]) != 0);
			heightTolerance_ = std::stof(seglist[4]);
		} catch (...) {
			// 変換エラー時は何もしない
		}
	}
}

} // namespace Game