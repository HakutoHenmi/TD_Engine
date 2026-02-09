#pragma once

#include "GimmickBase.h"
#include <string>

namespace Game {

class GoalGimmick : public GimmickBase {
public:
	// エディタのプルダウンに表示される名前
	std::string GetGimmickName() const override { return "Goal"; }

	// 初期化
	void Start(Engine::GameObject* owner) override;

	// 毎フレーム更新（ゴールの回転などを行う）
	void Update() override;

	// 衝突時の処理（演出開始の合図を送る）
	void OnCollision(void* other) override;

	// Inspectorでのデバッグ表示
	void OnInspectorGUI() override;

	// 保存と読み込み
	std::string SaveParameter() override;
	void LoadParameter(const std::string& param) override;

private:
	// 一度だけ発動させるためのフラグ
	bool isTriggered_ = false;
};

} // namespace Game