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
	float speed_ = 7.0f;
	float jumpPower_ = 8.0f;

	// 攻撃関連
	enum class AttackPhase { WindUp, Swing, Recovery };
	enum class SheatheState { Hand, Back, Transitioning };
	AttackPhase currentPhase_ = AttackPhase::WindUp;
	SheatheState sheatheState_ = SheatheState::Back;

	// ★追加: プレイヤータイプと銃関連
	enum class PlayerType { Sword, Gun };
	enum class GunType { AssaultRifle, Shotgun };
	PlayerType playerType_ = PlayerType::Sword;
	GunType gunType_ = GunType::AssaultRifle;

	bool isAiming_ = false;
	float skillCooldown_ = 0.0f;
	float gunShootTimer_ = 0.0f;
	
	bool prevPlayerSwitchKeyDown_ = false;
	bool prevSkillKeyDown_ = false;
	
	bool isCursorVisible_ = true;
	bool prevCursorToggle_ = false;

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
	float gunCombo3Dir_ = 1.0f; // ★追加: 3段目のダッシュ方向 (-1.0: 左, 1.0: 右)

	std::string swordName_ = "PlayerSword";

	void UpdateMovement(entt::entity entity, GameScene* /*scene*/, float dt);
	void UpdateAttack(entt::entity /*entity*/, GameScene* /*scene*/, float dt);
	void UpdateSword(entt::entity entity, GameScene* scene, float dt);

	// ★追加: 銃とスキルのアップデート関数
	void UpdateGun(entt::entity entity, GameScene* scene, float dt);
	void UpdateGunAttack(entt::entity entity, GameScene* scene, float dt);
	void SwitchPlayerType(entt::entity entity, GameScene* scene);
	void ExecuteSkill(entt::entity entity, GameScene* scene);
	void SpawnBullet(entt::entity entity, GameScene* scene, float spreadYaw, float spreadPitch, float damage, float lifeTime = 5.0f);
	void ShootGun(entt::entity entity, GameScene* scene);

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
};

} // namespace Game
