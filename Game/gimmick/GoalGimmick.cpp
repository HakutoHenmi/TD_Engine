#include "GoalGimmick.h"
#include "../Actors/PlayerBall.h"
#include "GimmickFactory.h"
#include "imgui.h"
#include <iostream>
#include <string>

namespace Game {

// GameScene.cpp 側で定義・検知される通知関数
// これを呼ぶと GameScene::Update 内で SceneState::ClearAnim に切り替わります
void NotifyGoalReached();

// エディタへの登録
static GimmickRegistrar<GoalGimmick> registrar("Goal");

void GoalGimmick::Start(Engine::GameObject* owner) {
	GimmickBase::Start(owner);
	isTriggered_ = false;
}

void GoalGimmick::Update() {
	// ゴールオブジェクト自体をクルクル回して目立たせる
	if (owner_) {
		owner_->transform.rotate.y += 0.02f;
	}
}

void GoalGimmick::OnCollision(void* other) {
	// すでにゴール判定済みなら何もしない
	if (isTriggered_) {
		return;
	}

	PlayerBall* player = static_cast<PlayerBall*>(other);
	if (player) {
		// 1. フラグを立てる（二重判定防止）
		isTriggered_ = true;

		// 2. ゲームシーンへ通知を送る
		// これにより GameScene 側でカメラやアザラシの演出が始まります
		NotifyGoalReached();

		std::cout << "[GoalGimmick] Triggered! Requesting GameScene animation..." << std::endl;
	}
}

void GoalGimmick::OnInspectorGUI() { ImGui::Checkbox("Triggered (Debug)", &isTriggered_); }

// パラメータ保存
std::string GoalGimmick::SaveParameter() {
	// ゴール済みかどうかを保存（通常は0に戻りますが、状態維持したい場合のため）
	return isTriggered_ ? "1" : "0";
}

// パラメータ読み込み
void GoalGimmick::LoadParameter(const std::string& param) {
	if (param.empty())
		return;
	try {
		int val = std::stoi(param);
		isTriggered_ = (val != 0);
	} catch (...) {
		// 変換失敗時は何もしない
	}
}

} // namespace Game