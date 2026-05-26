#pragma once
#include "IScript.h"
#include "Scenes/GameScene.h"

#include "../../externals/entt/entt.hpp"

namespace Game {

class PoisonTrap : public IScript {
public:
	void Start(entt::entity entity, GameScene* scene) override;
	void Update(entt::entity entity, GameScene* scene, float dt) override;
	void OnDestroy(entt::entity entity, GameScene* scene) override;
	void OnEditorUI() override;

private:
	void Debug(bool connected);
	bool IsEnemyInRange(entt::entity entity, GameScene* scene, float range);
	void CreatePoisonAttackArea(entt::entity entity, GameScene* scene, float damage, float range);

private:
	float poisonDamage_ = 3.0f;
	float poisonRange_ = 10.0f;

	float poisonActiveTime_ = 2.0f;
	float poisonCoolTime_ = 3.0f;

	float poisonActiveTimer_ = 0.0f;
	float poisonCoolTimer_ = 0.0f;

	float skillPowerRate_ = 1.0f;
	float skillSpeedRate_ = 1.0f;
	float skillRangeRate_ = 1.0f;
	float poisonDurationRate_ = 1.0f;
	float poisonCooldownRate_ = 1.0f;
	bool persistentVfxCreated_ = false;
	float finalPoisonActiveTime_ = 5.0f;
	float finalPoisonCoolTime_ = 5.0f;
	entt::entity persistentGasVfx_ = entt::null;
	float vfxDelayTimer_ = 0.0f;
	void CreatePersistentVFX(entt::entity entity, GameScene* scene);
};

} // namespace Game