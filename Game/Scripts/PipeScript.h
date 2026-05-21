#pragma once
#include "IScript.h"
#include "Scenes/GameScene.h"
#include "../../externals/entt/entt.hpp"
#include "../../Engine/Matrix4x4.h"
#include <unordered_map>

namespace Game {

class PipeScript : public IScript {
public:
	void Start(entt::entity entity, GameScene* scene) override;
	void Update(entt::entity entity, GameScene* scene, float dt) override;
	void Draw(entt::entity entity, GameScene* scene) override;
	void OnDestroy(entt::entity entity, GameScene* scene) override;

	float rotationSpeed_ = 1.0f; // パイプの回転速度（ラジアン/秒）
	uint32_t cylinderModelHandle_ = 0;
	uint32_t cylinderTextureHandle_ = 0;
	uint32_t glowMesh_ = 0;
	uint32_t glowTex_ = 0;
	float timer_ = 0.0f;

private:
	float connectionCheckTimer_ = 0.0f;
	float connectionCheckInterval_ = 2.0f; // ★最適化: 接続チェック間隔を2.0秒に延長してCPUスパイクを劇的削減

	std::vector<entt::entity> currentConnections_;
};

}// namespace Game