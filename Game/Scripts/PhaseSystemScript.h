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
	void Draw(entt::entity entity, GameScene* scene) override;
	void OnEditorUI() override;
	void OnDestroy(entt::entity entity, GameScene* scene) override;

	void Installation(GameScene* scene, const std::string& objPath);
	bool TryGetTerrainHitPoint(GameScene* scene, Engine::Vector3& outHitPoint) const;
	void DrawPlacementPreview(GameScene* scene, const Engine::Vector3& hitPoint, const std::string& objPath, bool canPlace);
	void SpawnPlacedObject(GameScene* scene, const Engine::Vector3& hitPoint, const std::string& objPath);
	bool IsPlacementBlocked(GameScene* scene, const Engine::Vector3& hitPoint) const;
	bool IsPrefabPath(const std::string& path) const;
	bool ExtractPrefabRenderPaths(const std::string& prefabPath, std::string& outModelPath, std::string& outTexturePath) const;
	void RequestPhaseChange(PhaseState nextPhase);
	void UpdatePhaseTransition();

	static PhaseState IsPhase() { return isPhase_; };
	static void SetPreparation(PhaseState prep) { NextPhase_ = prep; }
	static PhaseState GetRequestedPhase() { return NextPhase_; }
	static void ForcePhaseState(PhaseState phase) {
		isPhase_ = phase;
		NextPhase_ = phase;
	}

	static void PlusCoinCount(int PlusCoin) { CoinCount += PlusCoin; } // 追加: コイン数を増減させる関数

private:
	inline static PhaseState isPhase_ = PreparationPhase;
	inline static PhaseState NextPhase_ = PreparationPhase;
	PhaseState preIsPhase_ = PreparationPhase; // フェーズ切り替わり検知用
	int currentPhase_ = 0;

	int StartCoinCount_ = 600; // 初期コイン数

	inline static int CoinCount; // コインの数を管理する静的変数

	bool preKeyP_ = false; // 初期化しておく
	bool preKeySpace_ = false;
	bool isPlacementMode_ = false;
	bool isPhaseTransitioning_ = false;
	bool isFadeFinished_ = false;

	std::string selectedObjPath_ = "Resources/Models/cube/cube.obj";
	std::string previewModelPath = "";
	std::string previewObjPath_ = "";
	uint32_t previewModelHandle_ = 0;
	uint32_t previewTextureHandle_ = 0;

	// 施設の値段
	int tankCost_ = 100;
	int pipeCost_ = 5;
	int canonCost_ = 150;
	int missileCost_ = 200;
	int poisonCost_ = 120;

	// パイプ専用
	bool isPipeSet_ = false;
	bool hasPipeStartPoint_ = false;
	float pipeStartX_ = 0.0f;
	float pipeStartY_ = 0.0f;
	float pipeStartZ_ = 0.0f;

	int selectedObjCost_ = 0; // 追加

	// スキルツリー
	SkillTree skillTree_;
	bool preKeyN_ = false;
};

} // namespace Game
