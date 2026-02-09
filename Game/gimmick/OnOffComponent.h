#pragma once
#include "GimmickBase.h"
#include "OnOffCommon.h"
#include <string>

#include "../../Engine/Audio.h"

namespace Game {

class PlayerBall;

class OnOffComponent : public GimmickBase {
public:
	std::string GetGimmickName() const override { return "OnOffComponent"; }

	void Start(Engine::GameObject* owner) override;
	void Update() override;
	void OnCollision(void* other) override;

	void OnInspectorGUI() override;

	bool IsSolidNow() const;

	// ★追加: 保存と読み込みの実装宣言
	std::string SaveParameter() override;
	void LoadParameter(const std::string& param) override;

private:
	enum Mode {
		MODE_SWITCH = 0,
		MODE_BLOCK = 1,
	};

	// -------------------------
	// Inspectorで調整する値
	// -------------------------
	int mode_ = MODE_SWITCH;

	// block用
	int blockColor_ = (int)ONOFF_RED; // ONOFF_RED / ONOFF_BLUE
	float topTolUp_ = 0.2f;           // pBottom - topY <= topTolUp_
	float topTolDown_ = -0.3f;        // pBottom - topY >= topTolDown_

	// switch用
	float cooldownSec_ = 0.2f;

	// -------------------------
	// 内部状態
	// -------------------------
	float cooldown_ = 0.0f;
	bool wasTouching_ = false;

	// デバッグ用
	int debugHitCount_ = 0;
	float debugHitTimer_ = 0.0f;
	bool touchedThisFrame_ = false;

	// ★追加: 切り替え音のハンドル
	uint32_t seSwitchHandle_ = 0;
};

} // namespace Game