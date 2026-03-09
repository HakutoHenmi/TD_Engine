#pragma once
#include "IScript.h"
#include "Scenes/GameScene.h"
namespace Game {

class EnemyDamageBySpike : public IScript {
public:
	void Start(SceneObject& obj, GameScene* scene) override;
	void Update(SceneObject& obj, GameScene* scene, float dt) override;
	void OnDestroy(SceneObject& obj, GameScene* scene) override;

	void IsEnemyTouchingSpike(SceneObject& obj, GameScene* scene, float dt);

private:
	float hitRange_ = 2.0f;

	float damageInterval_ = 1.0f; // 1秒ごと
	float damageTimer_ = 0.0f;
	float damage_ = 9990.0f;
};

} // namespace Game