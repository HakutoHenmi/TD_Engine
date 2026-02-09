#include "OnOffComponent.h"
#include "../Actors/PlayerBall.h"
#include "GimmickFactory.h"
#include "imgui.h"
#include <sstream> // ★追加
#include <string>  // ★追加
#include <vector>  // ★追加

namespace Game {

// ★これが「魔法の1行」：エディタに出る
static GimmickRegistrar<OnOffComponent> registrar("OnOffComponent");

void OnOffComponent::Start(Engine::GameObject* owner) {
	GimmickBase::Start(owner);
	cooldown_ = 0.0f;
	wasTouching_ = false;

	// ★追加: スイッチ音ロード
	if (mode_ == MODE_SWITCH) {
		auto* audio = Engine::Audio::GetInstance();
		seSwitchHandle_ = audio->Load("Resources/Sound/suittikirikae.mp3");
	}
}

// OnOffComponent.cpp
bool OnOffComponent::IsSolidNow() const {
	if (mode_ != MODE_BLOCK)
		return true;
	return (OnOffSharedState::Get() == (OnOffColor)blockColor_);
}

void OnOffComponent::Update() {

	if (debugHitTimer_ > 0.0f) {
		debugHitTimer_ -= 1.0f / 60.0f; // 仮（60fps前提）
		if (debugHitTimer_ < 0.0f)
			debugHitTimer_ = 0.0f;
	}

	// --------------------------
	// ★Switch: 「離れた」をUpdateで確定させる
	// そのフレームに触れてなければ、次の接触は「踏んだ瞬間」になる
	// --------------------------
	if (mode_ == MODE_SWITCH) {
		if (owner_) {
			bool isRed = (OnOffSharedState::Get() == ONOFF_RED);
			owner_->color = isRed ? Engine::Vector4{1.0f, 0.0f, 0.0f, 1.0f}  // ON = 赤
			                      : Engine::Vector4{0.0f, 0.0f, 1.0f, 1.0f}; // OFF = 青
		}
		if (!touchedThisFrame_) {
			wasTouching_ = false;
		}
		// クールダウン減少
		if (cooldown_ > 0.0f) {
			cooldown_ -= 1.0f / 60.0f;
			if (cooldown_ < 0.0f)
				cooldown_ = 0.0f;
		}
	} else {
		// Blockモード
		if (owner_) {
			bool solid = IsSolidNow();

			// ★ブロックの本来色（blockColor_ で決める）
			Engine::Vector4 red = {1.0f, 0.0f, 0.0f, 1.0f};
			Engine::Vector4 blue = {0.0f, 0.0f, 1.0f, 1.0f};

			Engine::Vector4 blockCol = (blockColor_ == ONOFF_RED) ? red : blue;

			if (solid) {
				owner_->color = blockCol; // 実体：赤 or 青（不透明）
				owner_->isLocked = false;
			} else {
				owner_->color = {0.55f, 0.55f, 0.55f, 0.15f}; // ★逆状態：灰 + 低alpha（枠モード）
				owner_->isLocked = true;
			}
		}
	}

	// フレーム終了時にフラグをリセット
	touchedThisFrame_ = false;
}

void OnOffComponent::OnCollision(void* other) {
	touchedThisFrame_ = true;

	PlayerBall* player = static_cast<PlayerBall*>(other);
	if (!player)
		return;

	// ---------------------------------------------------
	// 1. SWITCH モード
	// ---------------------------------------------------
	if (mode_ == MODE_SWITCH) {
		// クールダウン中なら何もしない
		if (cooldown_ > 0.0f)
			return;

		// 「踏んだ瞬間」だけ反応
		if (!wasTouching_) {
			// 切り替え実行
			OnOffSharedState::Toggle();

			// ★追加: 切り替え音再生
			Engine::Audio::GetInstance()->Play(seSwitchHandle_);

			cooldown_ = cooldownSec_;
			wasTouching_ = true;

			debugHitCount_++;
			debugHitTimer_ = 0.2f;
		}
		return;
	}

	// ---------------------------------------------------
	// 2. BLOCK モード
	// ---------------------------------------------------
	if (mode_ == MODE_BLOCK) {
		// 実体がなければすり抜ける（押し出し処理をしない）
		if (!IsSolidNow()) {
			return;
		}

		// 実体があるなら「乗れる」
		// プレイヤーの位置とブロック上面を比較
		if (!owner_)
			return;

		Engine::Vector3 pPos = player->GetPosition();
		float pRad = player->GetRadius();
		float pBottom = pPos.y - pRad;

		// ブロックの上面Y（簡易的にAABBMaxを使うか、Scaleから計算）
		// ここでは transform.translate.y + scale.y/2 と仮定（Cube形状）
		float blockTop = owner_->transform.translate.y + (owner_->transform.scale.y * 1.0f); // 1.0fはモデルサイズ依存

		// 許容範囲内なら「乗った」とみなして押し上げる
		// pBottom が blockTop より少し下〜少し上
		float diff = pBottom - blockTop;
		if (diff <= topTolUp_ && diff >= topTolDown_) {
			// 位置補正
			Engine::Vector3 fixPos = pPos;
			fixPos.y = blockTop + pRad;
			player->SetPosition(fixPos);

			// Y速度リセット
			Engine::Vector3 v = player->GetVelocity();
			if (v.y < 0.0f)
				v.y = 0.0f;
			player->SetVelocity(v);

			player->SetGrounded(true);
		}
		return;
	}
}

void OnOffComponent::OnInspectorGUI() {
	ImGui::SeparatorText("OnOffComponent");

	const char* modes[] = {"Switch", "Block"};
	ImGui::Combo("Mode", &mode_, modes, 2);

	if (mode_ == MODE_SWITCH) {
		ImGui::Text("Shared State: %s", (OnOffSharedState::Get() == ONOFF_RED) ? "RED" : "BLUE");
		ImGui::Text("Tip: stepping toggles state");
		ImGui::DragFloat("Cooldown (sec)", &cooldownSec_, 0.01f, 0.0f, 2.0f);
		ImGui::Text("HitCount: %d", debugHitCount_);
		ImGui::Text("HitRecently: %s", (debugHitTimer_ > 0.0f) ? "YES" : "NO");
	} else {
		const char* cols[] = {"RED", "BLUE"};
		ImGui::Combo("Block Color", &blockColor_, cols, 2);

		ImGui::DragFloat("Top Tol Up", &topTolUp_, 0.01f, 0.0f, 1.0f);
		ImGui::DragFloat("Top Tol Down", &topTolDown_, 0.01f, -1.0f, 0.0f);
	}
}

// ★追加: パラメータをCSV文字列として保存
std::string OnOffComponent::SaveParameter() {
	std::stringstream ss;
	// 順序: mode, blockColor, topTolUp, topTolDown, cooldownSec
	ss << mode_ << "," << blockColor_ << "," << topTolUp_ << "," << topTolDown_ << "," << cooldownSec_;
	return ss.str();
}

// ★追加: 文字列からパラメータを復元
void OnOffComponent::LoadParameter(const std::string& param) {
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
			mode_ = std::stoi(seglist[0]);
			blockColor_ = std::stoi(seglist[1]);
			topTolUp_ = std::stof(seglist[2]);
			topTolDown_ = std::stof(seglist[3]);
			cooldownSec_ = std::stof(seglist[4]);
		} catch (...) {
			// 変換エラー時は何もしない
		}
	}
}

} // namespace Game