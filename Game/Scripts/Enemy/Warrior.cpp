#include "Warrior.h"
#include "../ScriptEngine.h"

namespace Game {
void Game::Warrior::ExecuteAttack(entt::entity entity, GameScene* scene, float /*dt*/) {
	auto& registry = scene->GetRegistry();

	// HitBoxが無ければ該当コンポーネントを追加
	if (!registry.all_of<HitboxComponent>(entity)) {
		registry.emplace<HitboxComponent>(entity);

		// HitBoxコンポーネントの設定
		auto& hb = registry.get<HitboxComponent>(entity);
		hb.isActive = true;
		hb.damage = 20.0f;
		hb.size = { 2.0f, 2.0f, 2.0f };
	}
}

REGISTER_SCRIPT(Warrior);
} // namespace Game