#pragma once
#include "GimmickBase.h"
#include <string>

namespace Game {

// 移動方向（縦 or 横）
enum class LiftAxis { Vertical = 0, Horizontal = 1, Depth = 2 };

// 「配置した位置」がどっち端か
// Vertical: Top / Bottom
// Horizontal: Left / Right
enum class LiftAnchor {
	Min = 0, // Bottom または Left
	Max = 1  // Top    または Right
};

class MovingLiftGimmick : public GimmickBase {
public:
	// ★エディタのプルダウンに表示される名前
	std::string GetGimmickName() const override { return "MovingLift"; }

	void Start(Engine::GameObject* owner) override;
	void Update() override;
	void OnCollision(void* other) override;
	void OnInspectorGUI() override;

	// ★追加: 保存と読み込みの実装宣言
	std::string SaveParameter() override;
	void LoadParameter(const std::string& param) override;
	void SnapToBaseForSave();
	//void OnBeforeSave() override;

private:
	// -----------------------------
	// Inspector で調整するパラメータ
	// -----------------------------
	LiftAxis axis_ = LiftAxis::Vertical;
	LiftAnchor anchor_ = LiftAnchor::Min; // Min=Bottom/Left, Max=Top/Right

	float moveDistance_ = 5.0f; // 片道距離
	float moveSpeed_ = 2.0f;    // units/sec（定番）

	// -----------------------------
	// 内部状態
	// -----------------------------
	Engine::Vector3 origin_ = {0, 0, 0}; // 基準点（配置した場所）
	float timer_ = 0.0f;                 // 時間経過
	bool touchedThisFrame_ = false;      // 今フレーム乗っているか

	// プレイヤー移動用（慣性移動などさせるため）
	Engine::Vector3 prevPos_ = {0, 0, 0};
	// 最後に乗ったプレイヤー（簡易ポインタ）
	// 本来は GameObject ID などで管理する方が安全だが、簡易実装としてポインタ保持
	// (シーン遷移で無効になるので注意)
	class PlayerBall* lastPlayer_ = nullptr;

	// ★基準（配置）座標：ここを保存してロードで復元する
	Engine::Vector3 basePos_ = {0, 0, 0};
	bool hasBasePos_ = false;
};

} // namespace Game