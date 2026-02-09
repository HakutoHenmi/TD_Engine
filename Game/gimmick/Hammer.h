#pragma once
#include "GimmickBase.h"
#include <string>

namespace Game {

class Hammer : public GimmickBase {
public:
	// エディタ表示用・保存用の名前
	std::string GetGimmickName() const override { return "Hammer"; }

	void Start(Engine::GameObject* owner) override;
	void Update() override;

	// 衝突時の反応 (void* で受け取る)
	void OnCollision(void* other) override;

	// ★追加: Inspectorでのパラメータ調整
	void OnInspectorGUI() override;

	// ★追加: 保存と読み込みの実装宣言
	std::string SaveParameter() override;
	void LoadParameter(const std::string& param) override;

private:
	float timer_ = 0.0f;
	float speed_ = 2.0f;    // 振れる速さ
	float maxAngle_ = 1.0f; // 振れる角度の大きさ(ラジアン)

	// 吹き飛ばし強さ
	float knockbackPower_ = 0.8f;
};

} // namespace Game