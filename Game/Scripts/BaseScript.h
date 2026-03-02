#pragma once
#include "IScript.h"
#include "Scenes/GameScene.h"

namespace Game {

class BaseScript : public IScript {
public:
	void Start(SceneObject& obj, GameScene* scene) override;
	void Update(SceneObject& obj, GameScene* scene, float dt) override;
	void OnDestroy(SceneObject& obj, GameScene* scene) override;

private:
	float rotationSpeed_ = 1.0f;  // タワーの回転速度
	float attackInterval_ = 1.0f; // 攻撃のクールダウン時間
	float attackTimer_ = 0.0f;    // 攻撃のクールダウンタイマー
	float damage_ = 10.0f;        // 攻撃のダメージ量
	float attackRange_ = 30.0f;   // 攻撃の射程距離
};

} // namespace Game