#include "Hammer.h"
#include "GimmickFactory.h"
#include "PlayerBall.h" // PlayerBallを操作するために必要
#include <cmath>
#include <iostream>
#include <sstream> // ★追加
#include <vector>  // ★追加

// ★追加: ImGuiを使ってパラメータ調整画面を描く
#include "imgui.h"

using namespace DirectX; // ベクトル演算用

namespace Game {

// テンプレートクラスとして正しく登録
static GimmickRegistrar<Hammer> registrar("Hammer");

void Hammer::Start(Engine::GameObject* owner) {
	GimmickBase::Start(owner);
	// ランダムな開始時間にして、複数のハンマーが同期しないようにする
	if (owner) {
		timer_ = (float)(rand() % 100) * 0.01f;
	}
}

void Hammer::Update() {
	if (owner_) {
		timer_ += 1.0f / 60.0f;

		// 振り子運動 (Z軸回転)
		// sin波を使って行ったり来たりさせる
		float angle = std::sin(timer_ * speed_) * maxAngle_;
		owner_->transform.rotate.z = angle;
	}
}

void Hammer::OnCollision(void* other) {
	// void* を PlayerBall* にキャストして操作する
	PlayerBall* player = static_cast<PlayerBall*>(other);

	if (player && owner_) {
		// ハンマーの位置
		XMVECTOR hammerPos = XMLoadFloat3((XMFLOAT3*)&owner_->transform.translate);
		// プレイヤーの位置
		XMVECTOR playerPos = XMLoadFloat3((XMFLOAT3*)&player->GetPosition());

		// ハンマーからプレイヤーへの向きを計算
		XMVECTOR dir = XMVector3Normalize(playerPos - hammerPos);

		// ★吹き飛ばし処理
		// その方向へ強く押し出す
		XMVECTOR knockback = dir * knockbackPower_;

		// 少し上方向(Y)への力を加えて、放物線を描くように飛ばす
		knockback = XMVectorSetY(knockback, 0.4f); // 高さ成分固定

		// プレイヤーの速度を強制的に書き換え
		Engine::Vector3 v;
		XMStoreFloat3((XMFLOAT3*)&v, knockback);
		player->SetVelocity(v);

		// コンソールにログ出し（デバッグ用）
		std::cout << "Hammer Hit! Power: " << knockbackPower_ << std::endl;
	}
}

void Hammer::OnInspectorGUI() {
	// ImGuiでパラメータを調整できるようにする
	ImGui::DragFloat("Speed", &speed_, 0.1f, 0.0f, 10.0f);
	ImGui::DragFloat("Max Angle", &maxAngle_, 0.1f, 0.0f, 3.14f);
	ImGui::DragFloat("Knockback", &knockbackPower_, 0.1f, 0.0f, 10.0f);
}

// ★追加: パラメータをCSV文字列として保存
std::string Hammer::SaveParameter() {
	std::stringstream ss;
	// 順序: speed, maxAngle, knockbackPower
	ss << speed_ << "," << maxAngle_ << "," << knockbackPower_;
	return ss.str();
}

// ★追加: 文字列からパラメータを復元
void Hammer::LoadParameter(const std::string& param) {
	if (param.empty())
		return;

	std::stringstream ss(param);
	std::string segment;
	std::vector<std::string> seglist;
	while (std::getline(ss, segment, ',')) {
		seglist.push_back(segment);
	}

	// 3つ以上のデータがあれば読み込む
	if (seglist.size() >= 3) {
		try {
			speed_ = std::stof(seglist[0]);
			maxAngle_ = std::stof(seglist[1]);
			knockbackPower_ = std::stof(seglist[2]);
		} catch (...) {
			// 変換エラー時は何もしない
		}
	}
}

} // namespace Game