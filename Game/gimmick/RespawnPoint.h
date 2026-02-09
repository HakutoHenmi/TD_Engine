#pragma once
#include "GimmickBase.h"
#include <string>

namespace Game {

class RespawnPoint : public GimmickBase {
public:
	// エディタのプルダウンに表示される名前
	std::string GetGimmickName() const override { return "RespawnPoint"; }

	// 初期化（アタッチ時やロード時に1回）
	void Start(Engine::GameObject* owner) override;

	// 毎フレーム更新（使わなければ空でOK）
	void Update() override;

	// プレイヤーが衝突した時
	void OnCollision(void* other) override;

	// Inspectorでのパラメータ調整
	void OnInspectorGUI() override;

	// ★追加: 保存と読み込みの実装宣言
	std::string SaveParameter() override;
	void LoadParameter(const std::string& param) override;

private:
	// 識別ID（将来拡張用）
	int id_ = 0;

	// チェックポイント座標を少し上げる（埋まり防止）
	float yOffset_ = 2.0f;

	// 1回触れたら二度と更新しない
	bool activateOnce_ = false;

	// 起動済みフラグ
	bool triggered_ = false;
};

} // namespace Game