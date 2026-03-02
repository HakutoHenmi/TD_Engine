#pragma once
#include "IScript.h"
#include "../Engine/Input.h"
#include <DirectXMath.h>
#include <Windows.h>
#include <string>
#include <cmath>

namespace Game {

class PlayerScript : public IScript {
public:
	void Start(SceneObject& obj, GameScene* scene) override;
	void Update(SceneObject& obj, GameScene* scene, float dt) override;
	void OnDestroy(SceneObject& obj, GameScene* scene) override;

private:
	float speed_ = 7.0f;
	float jumpPower_ = 8.0f;
	
	// 攻撃関連
	enum class AttackPhase { WindUp, Swing, Recovery };
	enum class SheatheState { Hand, Back, Transitioning };
	AttackPhase currentPhase_ = AttackPhase::WindUp;
	SheatheState sheatheState_ = SheatheState::Back;

	bool isSheathed_ = true;
	float sheatheTimer_ = 0.0f; // 最後に攻撃してから納刀するまでのタイマー
	const float AUTO_SHEATHE_TIME = 3.0f; // 3秒で自動納刀

	int comboCount_ = 0;       // 0: なし, 1-3: コンボ段数
	float attackTimer_ = 0.0f;  // 現在のフェーズの残り時間
	bool isAttacking_ = false;
	bool attackQueued_ = false; // 先行入力フラグ
	bool prevAttackKeyDown_ = false; // 前フレームの入力状態
	
	bool isReturning_ = false;  // 待機状態への戻り動作中
	float returnTimer_ = 0.0f;
	
	DirectX::XMFLOAT3 currentSwordRot_ = { 0.0f, 0.0f, 0.0f };
	DirectX::XMFLOAT3 startSwordRot_ = { 0.0f, 0.0f, 0.0f };

	DirectX::XMFLOAT3 currentBodyRot_ = { 0.0f, 0.0f, 0.0f };
	DirectX::XMFLOAT3 startBodyRot_ = { 0.0f, 0.0f, 0.0f };

	// イージングヘルパー: 0.0-1.0 を引数に取り、初速が早く後半ゆっくりになる
	float EaseOutCubic(float t) { return 1.0f - std::powf(1.0f - t, 3.0f); }
	float EaseOutQuint(float t) { return 1.0f - std::powf(1.0f - t, 5.0f); }
	float EaseOutBack(float t) { 
		float c1 = 1.70158f;
		float c3 = c1 + 1.0f;
		return 1.0f + c3 * std::powf(t - 1.0f, 3.0f) + c1 * std::powf(t - 1.0f, 2.0f); 
	}
	float EaseOutExpo(float t) { return (t == 1.0f) ? 1.0f : 1.0f - std::powf(2.0f, -10.0f * t); }

	// 剣オブジェクトの名前（ヒエラルキーから探す用）
	std::string swordName_ = "PlayerSword";

	void UpdateMovement(SceneObject& obj, GameScene* /*scene*/, float dt);
	void UpdateAttack(SceneObject& /*obj*/, GameScene* /*scene*/, float dt);
	void UpdateSword(SceneObject& obj, GameScene* scene, float dt);
};

} // namespace Game
