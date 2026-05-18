#pragma once
#include "IScript.h"
#include <vector>

namespace Game {

class WaveManagement : public IScript {
public:
	void Start(entt::entity entity, GameScene* scene) override;
	void Update(entt::entity entity, GameScene* scene, float dt) override;
	void OnDestroy(entt::entity entity, GameScene* scene) override;

	void OnEditorUI() override;
	std::string SerializeParameters() override;
	void DeserializeParameters(const std::string& data) override;

	void SpawnSpanner(int currentWave, GameScene* scene);

	static void SetWave(int waveNumber) { currentWave_ = waveNumber; }
	static entt::entity GetManagerEntity() { return managerEntity_; }
	static bool IsWaveEnded() { return isEnded_; }
	static void ResetState() {
		isEnded_ = false;
		currentWave_ = -1;
		managerEntity_ = static_cast<entt::entity>(entt::null);
		instance_ = nullptr;
	}
	static bool IsLastWave() {
		if (currentWave_ < 0) return false;
		if (!instance_) return false;
		if (instance_->enemySpawners_.empty()) return false;
		return currentWave_ >= static_cast<int>(instance_->enemySpawners_.size()) - 1;
	}
	static void EndGame() { isEnded_ = true; }

	int GetTotalMaxEnemies(GameScene* scene);
	int GetTotalRemainingEnemies(GameScene* scene);

private:
	static int currentWave_;
	static inline entt::entity managerEntity_ = static_cast<entt::entity>(entt::null);
	static inline WaveManagement* instance_ = nullptr;
	int previousWave_ = -1;

	static inline bool isEnded_ = false;

	// 各ウェーブごとのスポナー（エンティティ名）のリスト (シリアライズ用)
	std::vector<std::vector<std::string>> enemySpawnerNames_;
	// 実行時・Editor時の実体
	std::vector<std::vector<entt::entity>> enemySpawners_;

	// 最後に Update で参照したシーン (UI生成用)
	GameScene* cachedScene_ = nullptr;

	int currentWaveMax_ = 0;
	int currentWaveKilled_ = 0;
	int lastAliveCount_ = 0;
	int lastTotalSpawned_ = 0;
};

} // namespace Game