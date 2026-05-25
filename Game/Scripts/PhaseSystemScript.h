#pragma once
#include "../../externals/entt/entt.hpp"
#include "IScript.h"
#include "SkillTree.h"
#include <DirectXMath.h>

struct ImVec2; // 前方宣言
namespace Engine {
struct Vector3;
}

namespace Game {

class PhaseSystemScript : public IScript {
public:
	enum PhaseState { PreparationPhase, BattlePhase, Transition, InsertPhase };

	struct CameraWaypoint {
		DirectX::XMFLOAT3 position;
		DirectX::XMFLOAT3 rotation; // Pitch, Yaw, Roll
		float duration;             // Duration to travel to this point
	};

	void Start(entt::entity entity, GameScene* scene) override;
	void Update(entt::entity entity, GameScene* scene, float dt) override;
	void Draw(entt::entity entity, GameScene* scene) override;
	void OnEditorUI() override;
	void OnDestroy(entt::entity entity, GameScene* scene) override;

	void Installation(GameScene* scene, const std::string& objPath);
	bool TryGetTerrainHitPoint(GameScene* scene, Engine::Vector3& outHitPoint) const;
	void DrawPlacementPreview(GameScene* scene, const Engine::Vector3& hitPoint, const std::string& objPath, bool canPlace, bool drawExtras = true);
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

	// 返金時の計算関数
	static int CalculateRefund(int cost) {
		return static_cast<int>(cost * 0.8f);
	}

	static int GetCurrentPhase() { return currentPhase_; }

private:
	inline static PhaseState isPhase_ = PreparationPhase;
	inline static PhaseState NextPhase_ = PreparationPhase;
	PhaseState preIsPhase_ = PreparationPhase; // フェーズ切り替わり検知用
	inline static int currentPhase_ = 0;

	int StartCoinCount_ = 60000; // 初期コイン数

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

	bool isSellMode_ = false;       // 追加: 売却（削除）モード

	int canonCost_ = 150;
	int missileCost_ = 200;
	int poisonCost_ = 120;
	int iceCanonCost_ = 250;

	int selectedObjCost_ = 0; // 追加
	int currentInstallationCost_ = 0; // 現在の設置コスト

	// スキルツリー
	SkillTree skillTree_;
	bool preKeyN_ = false;

	entt::entity enemyCountUI_ = entt::null;
	entt::entity installationCostUI_ = entt::null;

	// インサートカメラ演出用
	std::vector<CameraWaypoint> insertWaypoints_;
	int currentWaypointIndex_ = 0;
	float waypointTime_ = 0.0f;
	float skipHoldTime_ = 0.0f;
	bool isInsertInitialized_ = false;
	entt::entity skipPromptUI_ = entt::null;
	entt::entity skipProgressUI_ = entt::null;
	DirectX::XMFLOAT3 originalCameraPos_ = {0.0f, 0.0f, 0.0f};
	DirectX::XMFLOAT3 originalCameraRot_ = {0.0f, 0.0f, 0.0f};

	// インサートカメラ演出関連のヘルパーメソッド
	void InitializeInsertPhase(GameScene* scene);
	void UpdateInsertPhase(GameScene* scene, float dt);
	void CreateSkipUI(GameScene* scene);
	void UpdateSkipUIProgress(GameScene* scene);
	void EndInsertPhase(GameScene* scene);
};

} // namespace Game
