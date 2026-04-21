#pragma once
#include "IScript.h"
#include "PhaseSystemScript.h"
#include "SkillTree.h"

namespace Engine {
struct Vector3;
}

namespace Game {

class TutorialScript : public IScript {
public:
	enum class TutorialStep {
		Preparation,
		InstallCannonGuide,
		InstallTankGuide,
		InstallPipeGuide,
		FirstBattle,
		SkillTreeGuide,
		Finish, // 終了
	};

	void Start(entt::entity entity, GameScene* scene) override;
	void Update(entt::entity entity, GameScene* scene, float dt) override;
	void OnDestroy(entt::entity entity, GameScene* scene) override;

private:
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



	TutorialStep tutorialStep_ = TutorialStep::Preparation;
	bool stepGuideShown_ = false;

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
};

} // namespace Game
