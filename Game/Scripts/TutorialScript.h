#pragma once
#include "IScript.h"
#include "PhaseSystemScript.h"
#include "SkillTree.h"

namespace Engine {
struct Vector3;
struct Vector4;
}

namespace Game {

class TutorialScript : public IScript {
public:
	enum class TutorialStep {
		Step1_Greeting = 0,
		Step2_CoreIntro,
		Step3_SpawnerIntro,
		Step4_PhaseIntro,
		Step5_CameraControl,
		Step6_FacilityIntro,
		Step7_CannonInstall,
		Step8_TankInstall,
		Step9_PipeInstall,
		Step10_DeleteIntro,
		Step11_BattleTransition,
		Step12_PlayerAttack,
		Step13_CombatPlay,
		Step14_SkillTree,
		Step15_EndExplanation,
		Step16_FreePlayPrep,
		Step17_FreePlayBattle,
		Count
	};

	void Start(entt::entity entity, GameScene* scene) override;
	void Update(entt::entity entity, GameScene* scene, float dt) override;
	void OnDestroy(entt::entity entity, GameScene* scene) override;

	static TutorialScript* GetInstance() { return instance_; }
	TutorialStep GetCurrentStep() const { return tutorialStep_; }
	bool IsStep10PlacedExtraTank() const { return step10_placedExtraTank_; }

private:
	static TutorialScript* instance_;
	void EnterStep(TutorialStep step);
	void RequestPhaseChange(PhaseSystemScript::PhaseState nextPhase);
	void UpdatePhaseTransition(GameScene* scene);
	void ShowStepGuide();
	void UpdateSkillTree(entt::entity entity, GameScene* scene, bool& outKeyN);

	// 説明文の表示
	void ShowGuideText(entt::entity entity, GameScene* scene);

	void Installation(GameScene* scene, const std::string& objPath);
	bool TryGetTerrainHitPoint(GameScene* scene, Engine::Vector3& outHitPoint) const;
	void DrawPlacementPreview(GameScene* scene, const Engine::Vector3& hitPoint, const std::string& objPath, bool canPlace);
	void SpawnPlacedObject(GameScene* scene, const Engine::Vector3& hitPoint, const std::string& objPath);
	bool IsPlacementBlocked(GameScene* scene, const Engine::Vector3& hitPoint) const;
	bool IsPrefabPath(const std::string& path) const;
	bool ExtractPrefabRenderPaths(const std::string& prefabPath, std::string& outModelPath, std::string& outTexturePath) const;

	// 強調表示用のヘルパー
	void DrawHighlights(GameScene* scene);
	void Draw3DHighlight(GameScene* scene, entt::entity entity, const Engine::Vector4& color, float radius = 3.0f);

	// 削除（売却）モードの処理
	void UpdateSellMode(GameScene* scene);

	TutorialStep tutorialStep_ = TutorialStep::Step1_Greeting;
	bool stepGuideShown_ = false;

	float autoProceedTimer_ = 0.0f;

	PhaseSystemScript::PhaseState phaseState_ = PhaseSystemScript::PreparationPhase;
	PhaseSystemScript::PhaseState nextPhaseState_ = PhaseSystemScript::PreparationPhase;
	bool isPhaseTransitioning_ = false;
	bool isFadeFinished_ = false;

	bool isPlacementMode_ = false;
	bool isPipeSet_ = false;
	bool hasPipeStartPoint_ = false;
	float pipeStartX_ = 0.0f;
	float pipeStartY_ = 0.0f;
	float pipeStartZ_ = 0.0f;
	bool hasPlacedTank_ = false;
	bool hasPlacedPipe_ = false;
	bool hasPlacedCannon_ = false;

	std::string selectedObjPath_ = "Resources/Models/cube/cube.obj";
	std::string previewObjPath_;
	uint32_t previewModelHandle_ = 0;
	uint32_t previewTextureHandle_ = 0;

	SkillTree skillTree_;
	bool preKeyN_ = false;
	bool hasOpenedSkillTreeInGuide_ = false;

	// 進行状況・テキスト管理用
	int currentLineIndex_ = 0;

	// 各種サブ状態
	bool step10_placedExtraTank_ = false;
	bool step10_deletedTank_ = false;
	bool step14_pageSwitched_ = false;
	bool step14_upgraded_ = false;
	int step14_initialSP_ = 0;

	// 削除モード
	bool isSellMode_ = false;

	float nextTimer_ = 3.0f; // 自動で次のステップに進むまでの時間（秒）
};

} // namespace Game
