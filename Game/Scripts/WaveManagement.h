#pragma once
#include "IScript.h"
#include <vector>

namespace Game {

class WaveManagement : public IScript {
public:
	void Start(entt::entity entity, GameScene* scene) override;
	void Update(entt::entity entity, GameScene* scene, float dt) override;
	void OnDestroy(entt::entity entity, GameScene* scene) override;

	void SpawnSpanner(int currentWave, GameScene* scene);

	static void SetWave(int waveNumber) { currentWave_ = waveNumber; }

private:
	static int currentWave_;
	int previousWave_ = 0;

	std::vector<std::vector<entt::entity>> enemySpawners_; // 各ウェーブごとのスポーンポイントのリスト
};

} // namespace Game