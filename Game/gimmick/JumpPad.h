#pragma once
#include "GimmickBase.h"
#include <string>

namespace Game {

class JumpPad : public GimmickBase {
public:
	// エディタ表示用・保存用の名前を返す
	std::string GetGimmickName() const override { return "JumpPad"; }

	// 初期化
	void Start(Engine::GameObject* owner) override;

	// 毎フレーム更新 (アニメーション処理)
	void Update() override;

	// 衝突時の処理 (ジャンプさせる)
	void OnCollision(void* other) override;

	// ★追加: Inspectorでのパラメータ調整
	void OnInspectorGUI() override;

	// ★追加: 保存と読み込みの実装宣言
	std::string SaveParameter() override;
	void LoadParameter(const std::string& param) override;

private:
	bool isTriggered_ = false;    // 起動フラグ
	float animationTime_ = 0.0f;  // アニメーション用タイマー
	float originalScaleY_ = 1.0f; // 元のYスケール

	// ★追加: ジャンプ力パラメータ
	float jumpPower_ = 15.0f;
};

} // namespace Game