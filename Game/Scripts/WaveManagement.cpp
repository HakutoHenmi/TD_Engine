#include "WaveManagement.h"
#include "ObjectTypes.h"
#include "Scenes/GameScene.h"
#include "ScriptEngine.h"
#include "EnemySpawnerScript.h"
#include <cmath>
#include <iostream>

#include "PhaseSystemScript.h"

namespace Game {

void WaveManagement::Start(entt::entity /*entity*/, GameScene* scene) {
	if (!scene) return;

}

void WaveManagement::Update(entt::entity /*entity*/, GameScene* scene, float /*dt*/) {

	if (currentWave_!=previousWave_) {
		SpawnSpanner(currentWave_, scene);
	}

	previousWave_ = currentWave_;
}

void WaveManagement::OnDestroy(entt::entity /*entity*/, GameScene* /*scene*/) {}

void WaveManagement::SpawnSpanner(int currentWave, GameScene* scene) {
	
}


REGISTER_SCRIPT(WaveManagement);

} // namespace Game