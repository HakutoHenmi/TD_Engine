#pragma once
#include "IScript.h"
#include "Scenes/GameScene.h"

#include "../../externals/entt/entt.hpp"

namespace Game {

class BaseScript : public IScript {
public:
	void Start(entt::entity entity, GameScene* scene) override;
	void Update(entt::entity entity, GameScene* scene, float dt) override;
	void OnDestroy(entt::entity entity, GameScene* scene) override;
	void OnEditorUI() override; // ★追加
	std::string SerializeParameters() override;             // ★追加
	void DeserializeParameters(const std::string& data) override; // ★追加


private:
	float rotationSpeed_ = 1.0f;  // タワーの回転速度
	float attackInterval_ = 1.0f; // 攻撃のクールダウン時間
	float attackTimer_ = 0.0f;    // 攻撃のクールダウンタイマー
	float damage_ = 10.0f;        // 攻撃のダメージ量
	float attackRange_ = 30.0f;   // 攻撃の射程距離
};

} // namespace Game