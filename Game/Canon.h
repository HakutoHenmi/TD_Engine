#pragma once
#include "IScript.h"
#include "Scenes/GameScene.h"

namespace Game {

class Canon : public IScript {
public:
	void Start(SceneObject& obj, GameScene* scene) override;
	void Update(SceneObject& obj, GameScene* scene, float dt) override;
	void OnDestroy(SceneObject& obj, GameScene* scene) override;

private:
	// 大砲性能
	float attackRange_ = 50.0f;    // 攻撃範囲
	float attackInterval_ = 1.0f; // 攻撃間隔（秒）
	float damage_ = 10.0f;        // ダメージ量
	                              // クールダウン
	float attackTimer_ = 0.0f;
	float rotationSpeed_ = 1.0f; // タワーの回転速度（ラジアン/秒）

		int objectCount = 0;
	int pipeCount = 0;
	int enemyCount = 0;
};

} // namespace Game