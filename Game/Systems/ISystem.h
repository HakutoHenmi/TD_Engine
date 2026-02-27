#pragma once
#include "../ObjectTypes.h"
#include "../../Engine/Camera.h"
#include "../../Engine/Renderer.h"
#include "../../Engine/Input.h"
#include <vector>

namespace Game {

// 各Systemに渡す共有コンテキスト
struct GameContext {
	float dt = 0.0f;
	Engine::Camera* camera = nullptr;
	Engine::Renderer* renderer = nullptr;
	Engine::Input* input = nullptr;
	bool isPlaying = false;
	std::vector<SceneObject>* pendingSpawns = nullptr; // SpawnObject等の遅延追加用
};

// System基底インターフェース
class ISystem {
public:
	virtual ~ISystem() = default;
	virtual void Update(std::vector<SceneObject>& objects, GameContext& ctx) = 0;
	virtual void Reset(std::vector<SceneObject>& /*objects*/) {} // Play開始時のリセット
};

} // namespace Game
