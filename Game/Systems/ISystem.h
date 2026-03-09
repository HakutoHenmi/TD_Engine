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
	class GameScene* scene = nullptr; // ★追加
	Engine::Input* input = nullptr;
	bool isPlaying = false;
	std::vector<SceneObject>* pendingSpawns = nullptr; // SpawnObject等の遅延追加用

	// ★追加: 座標系補正用 (エディターGameビュー等での相対座標)
	bool useOverrideMouse = false;
	float overrideMouseX = 0.0f;
	float overrideMouseY = 0.0f;
};

// System基底インターフェース
class ISystem {
public:
	virtual ~ISystem() = default;
	virtual void Update(std::vector<SceneObject>& objects, GameContext& ctx) = 0;
	virtual void Draw(std::vector<SceneObject>& /*objects*/, GameContext& /*ctx*/) {} // 描画処理用
	virtual void Reset(std::vector<SceneObject>& /*objects*/) {} // Play開始時のリセット
};

} // namespace Game
