#pragma once
#include "IScript.h"
#include "Scenes/GameScene.h"

#include "../../externals/entt/entt.hpp"

namespace Game {
class Canon : public IScript {
public:
	void Start(entt::entity entity, GameScene* scene) override;
	void Update(entt::entity entity, GameScene* scene, float dt) override;
	void OnDestroy(entt::entity entity, GameScene* scene) override;
	void OnEditorUI() override;
	void DrawUI(entt::entity entity, GameScene* scene);

private:
	void UpdateConnection(entt::entity entity, GameScene* scene);
	void Debug(bool connected);

private:
	
	float idleSteamTimer_ = 0.0f; // 常時蒸気用タイマー
	float rotationSpeed_ = 1.0f;
	float attackRange_ = 15.0f;
	float attackInterval_ = 2.0f;
	float currentAttackInterval_ = 2.0f;
	float damage_ = 10.0f;
	float attackTimer_ = 0.0f;
	float connectionCheckTimer_ = 0.0f;
	bool isConnectedToTank_ = false;
	int connectedTankCount = 0;
	int connectedCanonCount = 1;
	float skillPowerRate = 1.0f;
	float skillSpeedRate = 1.0f;
	float skillRangeRate = 1.0f;
	entt::entity currentTarget_ = entt::null;
	entt::entity baseEntity_ = entt::null;
};
} // namespace Game