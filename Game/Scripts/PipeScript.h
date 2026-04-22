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
	void OnDestroy(entt::entity entity, GameScene* scene) override;

private:
	float rotationSpeed_ = 1.0f; // パイプの回転速度（ラジアン/秒）
	std::unordered_map<entt::entity, entt::entity> connectionCylinders_;
};

}// namespace Game