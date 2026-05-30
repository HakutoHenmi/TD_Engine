#pragma once
#include "IScript.h"
#include "PhaseSystemScript.h"
#include "SkillTree.h"
#include <DirectXMath.h>

namespace Engine {
struct Vector3;
struct Vector4;
}

namespace Game {
class GameScene;

class TutorialScript : public IScript {
public:
	enum class TutorialStep {
		Step1_Greeting = 0,
		Step2_CoreIntro,
		Step3_SpawnerIntro,
		Step4_PhaseIntro,
		Step5_CameraControl,
		Step6_CannonInstall,
		Step7_DeleteIntro,
		Step8_BattleTransition,
		Step9_BuffExplanation,
		Step10_BuffPractice,
		Step11_PlayerAttack,
		Step12_CombatPlay,
		Step13_SkillTree,
		Step14_EndExplanation,
		Step15_FreePlayBattle,
		Count
	};

	void Start(entt::entity entity, GameScene* scene) override;
	void Update(entt::entity entity, GameScene* scene, float dt) override;
	void OnDestroy(entt::entity entity, GameScene* scene) override;

	static TutorialScript* GetInstance() { return instance_; }
	TutorialStep GetCurrentStep() const { return tutorialStep_; }
	bool IsStep7PlacedExtraCannon() const { return step7_placedExtraCannon_; }
	bool IsAuraEnabled() const { return tutorialStep_ >= TutorialStep::Step10_BuffPractice; }
	bool IsEnemyTimeStopped() const { return enemyTimeStopped_; }
	void SetEnemyTimeStopped(bool stopped) { enemyTimeStopped_ = stopped; }
	int GetBrokenShieldCount() const { return brokenShieldCount_; }
	void IncrementBrokenShieldCount() { brokenShieldCount_++; }
	void ResetBrokenShieldCount() { brokenShieldCount_ = 0; }

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
	bool step5_moved_ = false;
	bool step5_rotated_ = false;
	bool step6_clickedCannonButton_ = false;
	int step6_cannonCount_ = 0;
	bool step7_placedExtraCannon_ = false;
	bool step7_deletedCannon_ = false;
	bool step13_pageSwitched_ = false;
	bool step13_upgraded_ = false;
	int step13_initialSP_ = 0;
	bool enemyTimeStopped_ = false;
	int brokenShieldCount_ = 0;
	float step11Timer_ = 0.0f;

	entt::entity currentEntity_{};
	GameScene* currentScene_ = nullptr;

	// 削除モード
	bool isSellMode_ = false;

	float nextTimer_ = 6.0f; // 自動で次のステップに進むまでの時間（秒）

	// カメラフォーカス用変数
	bool isCameraOverriding_ = false;
	bool cameraTargetFound_ = false;
	DirectX::XMFLOAT3 cameraOverrideTargetPos_{0,0,0};
	DirectX::XMFLOAT3 cameraOverrideEyePos_{0,0,0};
	float cameraTransitionTime_ = 0.0f;
	float cameraTransitionMax_ = 1.5f;
	DirectX::XMFLOAT3 startEye_{0,0,0};
	DirectX::XMFLOAT3 startTarget_{0,0,0};
	std::vector<entt::entity> cameraTargetEntities_;
	
	// SE関連
	uint32_t installationSeHandle_ = 0;
	uint32_t collapseSeHandle_ = 0;
	
	void UpdateCameraFocus(GameScene* scene, float dt);
};

} // namespace Game
