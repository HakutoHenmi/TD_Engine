#pragma once
#include "../Engine/Input.h"
#include "IScript.h"
#include <DirectXMath.h>
#include <Windows.h>
#include <cmath>
#include <deque>
#include "../../externals/entt/entt.hpp"
#include "../Engine/Matrix4x4.h"
#include "ObjectTypes.h"

namespace Game {

class PlayerScript : public IScript {
public:
	void Start(entt::entity entity, GameScene* scene) override;
	void Update(entt::entity entity, GameScene* scene, float dt) override;
	void OnEditorUI() override;
	void DrawUI(entt::entity entity, GameScene* scene) override;
	void OnDestroy(entt::entity entity, GameScene* scene) override;

private:
	float speed_ = 7.0f; // ★復元: 5.0f -> 7.0f (快適な機動力へ)
	float jumpPower_ = 15.0f;

	// 攻撃関連
	enum class AttackPhase { WindUp, Swing, Recovery };
	enum class SheatheState { Hand, Back, Transitioning };
	AttackPhase currentPhase_ = AttackPhase::WindUp;
	SheatheState sheatheState_ = SheatheState::Back;

	// ★追加: プレイヤータイプと銃関連
	enum class PlayerType { Sword, Gun };
	PlayerType playerType_ = PlayerType::Sword;
	bool isAiming_ = false;
	float skillCooldown_ = 0.0f;

	// ★スキルバフ関連
	bool isSkillActive_ = false;
	float skillDuration_ = 0.0f;
	const float SKILL_MAX_DURATION = 10.0f;
	const float SKILL_COOLDOWN_TIME = 15.0f;
	const float SKILL_SPEED_MULTIPLIER = 1.8f;
	const float SKILL_DAMAGE_MULTIPLIER = 2.5f;
	float gunShootTimer_ = 0.0f;
	
	bool prevPlayerSwitchKeyDown_ = false;
	bool prevSkillKeyDown_ = false;
	
	bool isCursorVisible_ = true;
	bool prevCursorToggle_ = false;
	bool prevDashKeyDown_ = false;

	std::string gunName_ = "PlayerGun";

	bool isSheathed_ = true;
	float sheatheTimer_ = 0.0f;
	const float AUTO_SHEATHE_TIME = 3.0f;

	int comboCount_ = 0;
	float attackTimer_ = 0.0f;
	bool isAttacking_ = false;
	bool attackQueued_ = false;
	bool prevAttackKeyDown_ = false;
	
	int gunComboStep_ = 0;
	float gunComboResetTimer_ = 0.0f;
	float gunComboAnimTimer_ = 0.0f;
	int gunSubStep_ = 0; // アニメーション中の発射状態を管理する用
	float gunSubTimer_ = 0.0f; // 次の弾までの間隔

	std::string swordName_ = "PlayerSword";

	void UpdateMovement(entt::entity entity, GameScene* /*scene*/, float dt);
	void UpdateAttack(entt::entity /*entity*/, GameScene* /*scene*/, float dt);
	void UpdateSword(entt::entity entity, GameScene* scene, float dt);

	// ★追加: 銃とスキルのアップデート関数
	void UpdateGun(entt::entity entity, GameScene* scene, float dt);
	void UpdateGunAttack(entt::entity entity, GameScene* scene, float dt);
	void SwitchPlayerType(entt::entity entity, GameScene* scene);
	void ExecuteSkill(entt::entity entity, GameScene* scene);
	void SpawnBullet(entt::entity entity, GameScene* scene, float spreadYaw, float spreadPitch, float damage, float lifeTime = 5.0f, bool enhanced = false, bool explode = false);
	void ShootGun(entt::entity entity, GameScene* scene);
	void SpawnCrystalBurst(const DirectX::XMFLOAT3& pos, int count, bool enhanced);
	void ApplySkillEffects(entt::entity entity, GameScene* scene);
	float experience_ = 0.0f;
	int level_ = 1;
	float nextExperience_ = 100.0f;
	bool isSubscribed_ = false;
	int debugSubscribeCount_ = 0;
	int debugReceiveCount_ = 0;
	float debugLastValue_ = 0.0f;

	// ★修正: 剣の軌跡 (DrawLine3Dベース)
	struct TrailPoint {
		Engine::Vector3 tip;
		Engine::Vector3 base;
		float life;
		float maxLife;
	};
	std::deque<TrailPoint> trailPoints_;

	// ★追加: マズルフラッシュ
	struct MuzzleFlash {
		DirectX::XMFLOAT3 pos;
		float life;
		float maxLife;
	};
	std::deque<MuzzleFlash> muzzleFlashes_;

	// ★追加: 残像（アフターイメージ）
	struct AfterImage {
		DirectX::XMFLOAT3 pos;
		DirectX::XMFLOAT3 rotate;
		DirectX::XMFLOAT3 scale;
		float life;
		float maxLife;
	};
	std::deque<AfterImage> afterImages_;
	float afterImageTimer_ = 0.0f;

	// ★追加: 薬莢
	struct ShellCasing {
		DirectX::XMFLOAT3 pos;
		DirectX::XMFLOAT3 velocity;
		float life;
	};
	std::deque<ShellCasing> shellCasings_;

	// ★追加: クリスタル飛散エフェクト
	struct CrystalParticle {
		DirectX::XMFLOAT3 pos;
		DirectX::XMFLOAT3 velocity;
		float life;
		float maxLife;
		float size;
		float rotSpeed;  // 回転速度
		float rot;       // 現在の回転角
		DirectX::XMFLOAT4 color;
	};
	std::deque<CrystalParticle> crystalParticles_;

	// ★追加: 蒸気圧システム (射撃用)
	float steamPressure_ = 100.0f;       // 現在の蒸気圧
	float maxSteamPressure_ = 100.0f;    // 最大蒸気圧
	bool isRecharging_ = false;          // 圧力リチャージ中か
	float rechargeTimer_ = 0.0f;         // リチャージ残り時間
	static constexpr float RECHARGE_TIME = 2.5f;        // リチャージにかかる時間
	static constexpr float NORMAL_SHOT_COST = 15.0f;    // 通常射撃の圧力コスト
	static constexpr float CHARGE_SHOT_COST = 45.0f;    // チャージショットの圧力コスト

	// ★追加: 飛行システム (別圧力計)
	float flightPressure_ = 100.0f;
	float maxFlightPressure_ = 100.0f;
	bool isFlying_ = false;
	const float FLIGHT_COST_PER_SEC = 25.0f; // ★毎秒25.0消費 (4秒飛べる)
	const float MAX_FLIGHT_HEIGHT = 15.0f;   // 最大飛行高度
	bool prevRightClickDown_ = false;
	entt::entity lockedEnemy_ = entt::null;
	static constexpr float CHARGE_TIME_MAX = 1.2f;      // 最大チャージ時間
	static constexpr float CHARGE_TIME_MIN = 0.35f;     // チャージショット判定の最低時間
	static constexpr float DASH_COST = 20.0f;           // スチーム・ブーストのコスト
	static constexpr float DASH_POWER = 155.0f;         // スチーム・ブーストの推進力 (さらにもう少しだけ伸ばす調整)

	DirectX::XMFLOAT3 initialPos_ = {0, 0, 0}; // ★フェーズクリア時に戻るための初期位置

	// ★追加: チャージショット
	bool isCharging_ = false;            // チャージ中か
	float chargeTime_ = 0.0f;            // 現在のチャージ時間
	float chargeVfxTimer_ = 0.0f;        // チャージ中の蒸気排出タイマー

	// ★追加: 大剣の溜め攻撃
	bool isSwordCharging_ = false;
	float swordChargeTime_ = 0.0f;
	float swordChargeVfxTimer_ = 0.0f;
	const float SWORD_CHARGE_MAX = 1.2f;
	const float SWORD_CHARGE_COST = 35.0f;

	// ★追加: 反動後退
	DirectX::XMFLOAT3 recoilVelocity_ = {0, 0, 0};

	// ★追加: ダメージ演出
	float damageEffectTimer_ = 0.0f;
	const float DAMAGE_EFFECT_DURATION = 0.6f;

	void ShootChargeShot(entt::entity entity, GameScene* scene);
	void DrawPressureGauge(GameScene* scene);
	void DrawReticle(entt::entity playerEntity, GameScene* scene);


	// Skills
	float playerMoveSpeedRate_ = 1.0f;
	float playerGunDamageRate_ = 1.0f;
	float playerMaxSteamRate_ = 1.0f;
};

} // namespace Game
