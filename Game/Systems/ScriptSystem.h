#pragma once
#include "../Scripts/ScriptEngine.h"
#include "ISystem.h"
#include "../../Engine/JobSystem.h"

namespace Game {

class GameScene; // 前方宣言

class ScriptSystem : public ISystem {
public:
	void SetScene(GameScene* scene) { scene_ = scene; }

	void Update(entt::registry& registry, GameContext& ctx) override {
		if (!ctx.isPlaying)
			return;

		auto* scriptEngine = ScriptEngine::GetInstance();

		auto view = registry.view<ScriptComponent>();
		std::vector<entt::entity> entities(view.begin(), view.end());
		if (entities.empty()) return;

		Engine::JobSystem::Dispatch((uint32_t)entities.size(), 64, [&](uint32_t i) {
			auto entity = entities[i];
			if (!registry.valid(entity)) return;
			auto& sc = registry.get<ScriptComponent>(entity);
			if (sc.enabled && !sc.scripts.empty()) {
				scriptEngine->Execute(entity, scene_, ctx.dt);
			}
		});
		
		// 全スクリプトの実行完了を待機
		Engine::JobSystem::Wait();
	}

	void Reset(entt::registry& registry) override {
		auto view = registry.view<ScriptComponent>();
		for (auto entity : view) {
			auto& sc = registry.get<ScriptComponent>(entity);
			for (auto& entry : sc.scripts) {
				entry.instance = nullptr;
			}
		}
	}

private:
	GameScene* scene_ = nullptr;
};

} // namespace Game
