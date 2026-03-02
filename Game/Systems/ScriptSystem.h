#pragma once
#include "../Scripts/ScriptEngine.h"
#include "ISystem.h"

namespace Game {

class GameScene; // 前方宣言

class ScriptSystem : public ISystem {
public:
	void SetScene(GameScene* scene) { scene_ = scene; }

	void Update(std::vector<SceneObject>& objects, GameContext& ctx) override {
		if (!ctx.isPlaying)
			return;

		auto* scriptEngine = ScriptEngine::GetInstance();
		for (auto& obj : objects) {
			if (!obj.scripts.empty() && obj.scripts[0].enabled && !obj.scripts[0].scriptPath.empty()) {
				scriptEngine->Execute(obj, scene_, ctx.dt);
			}
		}
	}

	void Reset(std::vector<SceneObject>& objects) override {
		for (auto& obj : objects) {
			for (auto& sc : obj.scripts) {
				sc.instance = nullptr;
			}
		}
	}

private:
	GameScene* scene_ = nullptr;
};

} // namespace Game
