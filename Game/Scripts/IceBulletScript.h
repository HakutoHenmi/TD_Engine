#pragma once
#include "IScript.h"
#include "Scenes/GameScene.h"

#include "../../externals/entt/entt.hpp"

namespace Game {

class IceBulletScript : public IScript {



public:
	void Start(entt::entity entity, GameScene* scene) override;
	void Update(entt::entity entity, GameScene* scene, float dt) override;
	void OnDestroy(entt::entity entity, GameScene* scene) override;

private:
	DirectX::XMFLOAT3 lastTargetPosition_;
	bool hasLastTargetPosition_ = false;
	float speed_ = 20.0f;
	float lifeTime_ = 0.0f;
	float maxLifeTime_ = 6.0f;
	float upTime_ = 0.4f;

	entt::entity bulletVfx_ = entt::null;
	bool vfxCreated_ = false;

	bool hasTarget_ = false;
	entt::entity target_ = entt::null;

	float damage_ = 2.0f;
	float stopTime_ = 1.0f;



	float spiralTime_ = 1.0f;

	float spiralAngle_ = 0.0f;
	float spiralRadius_ = 0.0f;

	DirectX::XMFLOAT3 centerPosition_ = {0.0f, 0.0f, 0.0f};
};

} // namespace Game