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
	uint32_t pipe1Model_ = 0;
	uint32_t pipe1Tex_ = 0;
	uint32_t pipe2Model_ = 0;
	uint32_t pipe2Tex_ = 0;
	uint32_t pipe3Model_ = 0;
	uint32_t pipe3Tex_ = 0;
	uint32_t glowMesh_ = 0;
	uint32_t glowTex_ = 0;
	float timer_ = 0.0f;

	uint32_t currentModel_ = 0;
	uint32_t currentTex_ = 0;
	float currentRotY_ = 0.0f;
	float currentScale_ = 1.0f;
	float currentScaleY_ = 2.0f;
	float currentOffsetX_ = 0.0f;
	float currentOffsetY_ = 0.0f;
	float currentOffsetZ_ = 0.0f;
	Engine::Vector4 pipeColor_ = {0.75f, 0.75f, 0.75f, 1.0f};

	bool drawPipeX_ = false;
	bool drawPipeZ_ = false;

private:
	float connectionCheckTimer_ = 0.0f;
	float connectionCheckInterval_ = 2.0f; // ★最適化: 接続チェック間隔を2.0秒に延長してCPUスパイクを劇的削減

	std::vector<entt::entity> currentConnections_;
	std::vector<entt::entity> allConnections_;
};

}// namespace Game