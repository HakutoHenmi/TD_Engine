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

private:
	// 大砲性能
	float attackRange_ = 50.0f;   // 攻撃範囲
	float attackInterval_ = 1.0f; // 攻撃間隔（秒）
	float damage_ = 10.0f;        // ダメージ量
	                              // クールダウン
	float attackTimer_ = 0.0f;
	float rotationSpeed_ = 1.0f; // タワーの回転速度（ラジアン/秒）

	int connectedTankCount = 0;
	int connectedCanonCount = 0;
	float connectionCheckTimer_ = 0.0f; // 負荷分散用タイマー
	bool isConnectedToTank_ = false;

private:
	void Debug(bool connected);
    void UpdateConnection(entt::entity entity, GameScene* scene);
};

} // namespace Game