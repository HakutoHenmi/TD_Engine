//======================================================
// BoostRingGimmick.h
//======================================================
#pragma once
#include "../Actors/PlayerBall.h"
#include "GimmickBase.h"
#include <string>

#include "../../Engine/Audio.h"

namespace Game {

class BoostRingGimmick : public GimmickBase {
public:
	std::string GetGimmickName() const override { return "BoostRing"; }

	void Start(Engine::GameObject* owner) override;
	void Update() override;
	void OnCollision(void* other) override;
	void OnInspectorGUI() override;
	// 衝突じゃなく「内側に入ったか」を判定して加速
	void CheckTrigger(PlayerBall* player);

	// ★追加: 保存と読み込みの実装宣言
	std::string SaveParameter() override;
	void LoadParameter(const std::string& param) override;

private:
	bool IsHitXZ_(const Engine::Vector3& playerPos, float playerRadius, const Engine::Vector3& ringPos) const;

private:
	float radius_ = 1.5f;

	float boostSpeed_ = 2.0f; // 保証したい最低速度
	float cooldownSec_ = 0.15f;
	float heightTolerance_ = 0.3f;

	float cooldownTimer_ = 0.0f;
	float innerRadius_ = 1.2f; // 内側判定半径（見た目に合わせて調整）
	bool wasInside_ = false;

	float thickness_ = 0.25f; // ローカルX方向の厚み
	float margin_ = 0.10f;    // 余裕

	// BoostRingGimmick.h の private に追加（シングルプレイヤー想定）
	Engine::Vector3 prevPlayerPos_{};
	bool hasPrevPlayerPos_ = false;

	// ★追加: 効果音のハンドル
	uint32_t seBoostHandle_ = 0;
};

} // namespace Game