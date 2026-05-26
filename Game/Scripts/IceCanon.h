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
	// 待機用エフェクトの永続エンティティ
	entt::entity persistentMistVfx_ = entt::null;
	entt::entity persistentCrystalVfx_ = entt::null;
	bool persistentVfxCreated_ = false;
	float vfxDelayTimer_ = 0.1f;

	void CreatePersistentVFX(entt::entity entity, GameScene* scene);

	float attackInterval_ = 1.5f;
	float attackTimer_ = 0.0f;

	float attackRange_ = 30.0f;

	float damage_ = 2.0f;
	float stopTime_ = 1.0f;
};

} // namespace Game