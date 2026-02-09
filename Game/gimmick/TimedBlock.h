//======================================================
// TimedBlock.h  (Game/gimmick/TimedBlock.h)
//======================================================
#pragma once
#include "GimmickBase.h"
#include <string>

namespace Game {
class PlayerBall;

class TimedBlock : public GimmickBase {
public:
	// エディタのプルダウン表示名
	std::string GetGimmickName() const override { return "TimedBlock"; }

	void Start(Engine::GameObject* owner) override;
	void Update() override;
	void OnCollision(void* other) override;
	void OnInspectorGUI() override;
	bool IsSolidNow() const { return active_; } // active_ を返すだけ                                            
	std::string SaveParameter() override;// ★保存・読み込み（マニュアル通り）
	void LoadParameter(const std::string& param) override;


private:
	bool IsActive_(float globalTimeSec) const;
	bool IsOnTop_(const Engine::Vector3& p, float pr) const;

private:
	// ---- Inspectorで調整するパラメータ ----
	float periodSec_ = 2.0f; // 周期(秒)
	float duty01_ = 0.5f;    // 出現割合(0..1)
	float phase01_ = 0.0f;   // 位相(0..1)

	// “足場として乗せる”判定の許容
	float topTolUp_ = 0.20f;   // pBottom - topY <= +0.20
	float topTolDown_ = 0.30f; // pBottom - topY >= -0.30

	// ---- 内部状態 ----
	float timeSec_ = 0.0f; // Update内で加算する“グローバル時間(代替)”
	bool active_ = true;
	// TimedBlock.h のクラス内（private か public）に追加
	bool startOn_ = true; // ★初期値：trueならON開始、falseならOFF開始
};
} // namespace Game
