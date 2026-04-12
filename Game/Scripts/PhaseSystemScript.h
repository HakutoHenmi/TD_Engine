#pragma once
#include "../../externals/entt/entt.hpp"
#include "IScript.h"
#include "SkillTree.h"

struct ImVec2; // 前方宣言
namespace Engine {
struct Vector3;
}

namespace Game {

class PhaseSystemScript : public IScript {
public:
	enum PhaseState { PreparationPhase, BattlePhase, Transition };

	void Start(entt::entity entity, GameScene* scene) override;
	void Update(entt::entity entity, GameScene* scene, float dt) override;
	void OnDestroy(entt::entity entity, GameScene* scene) override;

	void Installation(GameScene* scene, const std::string& objPath);
	bool TryGetTerrainHitPoint(GameScene* scene, Engine::Vector3& outHitPoint) const;
	void DrawPlacementPreview(GameScene* scene, const Engine::Vector3& hitPoint, const std::string& objPath, bool canPlace);
	void SpawnPlacedObject(GameScene* scene, const Engine::Vector3& hitPoint, const std::string& objPath);
	bool IsPlacementBlocked(GameScene* scene, const Engine::Vector3& hitPoint) const;
	bool IsPrefabPath(const std::string& path) const;
	bool ExtractPrefabRenderPaths(const std::string& prefabPath, std::string& outModelPath, std::string& outTexturePath) const;

	static PhaseState IsPreparation() { return isPhase_; };
	static void SetPreparation(PhaseState prep) { NextPhase_ = prep; }

private:
	inline static PhaseState isPhase_ = PreparationPhase;
	inline static PhaseState NextPhase_ = PreparationPhase;
	PhaseState preIsPreparation_ = PreparationPhase; // フェーズ切り替わり検知用
	int currentPhase_ = 0;

	bool preKeyP_ = false; // 初期化しておく
	bool prekeySpace_ = false;
	bool isPlacementMode_ = false;
	std::string selectedObjPath_ = "Resources/Models/cube/cube.obj";
	std::string previewObjPath_;
	uint32_t previewModelHandle_ = 0;
	uint32_t previewTextureHandle_ = 0;

	// スキルツリー
	SkillTree skillTree_;
	bool preKeyN_ = false;
};

} // namespace Game
