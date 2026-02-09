#include "RespawnPoint.h"
#include "../Actors/PlayerBall.h"
#include "GimmickFactory.h"
#include "imgui.h"

#include <iostream>
#include <sstream> // ★追加
#include <string>  // ★追加
#include <vector>  // ★追加

namespace Game {

// ★魔法の1行：これでエディタに出る
static GimmickRegistrar<RespawnPoint> registrar("RespawnPoint");

// GameScene.cpp 側で実装する関数
void RegisterRespawnPoint(int id, const Engine::Vector3& pos);
void ActivateRespawnPoint(int id, const Engine::Vector3& pos);

void RespawnPoint::Start(Engine::GameObject* owner) {
	GimmickBase::Start(owner);
	triggered_ = false;

	if (owner_) {
		Engine::Vector3 p = owner_->transform.translate;
		p.y += yOffset_;
		RegisterRespawnPoint(id_, p);
	}
}

void RespawnPoint::Update() {
	if (!owner_)
		return;

	// 位置が動く可能性も考慮して毎フレーム登録更新（負荷が気になるならStartのみでも可）
	Engine::Vector3 p = owner_->transform.translate;
	p.y += yOffset_;
	RegisterRespawnPoint(id_, p);
}

void RespawnPoint::OnCollision(void* other) {
	PlayerBall* player = static_cast<PlayerBall*>(other);
	if (!player)
		return;
	if (!owner_)
		return;

	if (activateOnce_ && triggered_)
		return;

	triggered_ = true;

	Engine::Vector3 p = owner_->transform.translate;
	p.y += yOffset_;

	// リスポーン地点として有効化
	ActivateRespawnPoint(id_, p);

	// ログ (デバッグ用)
	// std::cout << "Respawn Point Set! ID: " << id_ << std::endl;
}

void RespawnPoint::OnInspectorGUI() {
	ImGui::InputInt("Respawn ID", &id_);
	ImGui::DragFloat("Y Offset", &yOffset_, 0.1f, -10.0f, 10.0f);
	ImGui::Checkbox("Activate Once", &activateOnce_);

	// デバッグ表示
	ImGui::Text("Triggered: %s", triggered_ ? "Yes" : "No");
}

// ★追加: パラメータをCSV文字列として保存
std::string RespawnPoint::SaveParameter() {
	std::stringstream ss;
	// 順序: id, yOffset, activateOnce
	ss << id_ << "," << yOffset_ << "," << (activateOnce_ ? 1 : 0);
	return ss.str();
}

// ★追加: 文字列からパラメータを復元
void RespawnPoint::LoadParameter(const std::string& param) {
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
			id_ = std::stoi(seglist[0]);
			yOffset_ = std::stof(seglist[1]);
			activateOnce_ = (std::stoi(seglist[2]) != 0);
		} catch (...) {
			// 変換エラー時は何もしない
		}
	}
}

} // namespace Game