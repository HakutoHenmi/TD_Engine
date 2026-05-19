#pragma once
#include "IScript.h"
#include "Scenes/GameScene.h"

#include "../../externals/entt/entt.hpp"

namespace Game {

class IceCanon : public IScript {
public:
	void Start(entt::entity entity, GameScene* scene) override;
	void Update(entt::entity entity, GameScene* scene, float dt) override;
	void OnDestroy(entt::entity entity, GameScene* scene) override;

private:
	float attackInterval_ = 1.5f;
	float attackTimer_ = 0.0f;

	float attackRange_ = 25.0f;

	float damage_ = 2.0f;
	float stopTime_ = 1.0f;
	void UpdateConnection(entt::entity entity, GameScene* scene);

	bool isConnectedToTank_ = false;
	int connectedTankCount_ = 0;
	float connectionCheckTimer_ = 0.0f;
};

} // namespace Game