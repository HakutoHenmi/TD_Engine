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

	std::string SerializeParameters() override;
	void DeserializeParameters(const std::string& data) override;

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

	void SetSellMode(bool mode) { isSellMode_ = mode; }

	static void PlusCoinCount(int PlusCoin) { CoinCount += PlusCoin; } // 追加: コイン数を増減させる関数

	// 返金時の計算関数
	static int CalculateRefund(int cost) {
		return static_cast<int>(cost * 0.8f);
	}

	static int GetCurrentPhase() { return currentPhase_; }
	static void ResetPhaseCount() { currentPhase_ = 0; }
	static int GetGameOverPhase() { return s_gameOverPhase_; }
	static int GetGameClearPhase() { return s_gameClearPhase_; }
	static void ResetGameOverPhase() { s_gameOverPhase_ = 0; s_gameClearPhase_ = 0; }
	static bool IsResultSequenceActive() { return s_gameOverPhase_ > 0 || s_gameClearPhase_ > 0; }

private:
	inline static PhaseState isPhase_ = PreparationPhase;
	inline static PhaseState NextPhase_ = PreparationPhase;
	inline static int s_gameOverPhase_ = 0;
	inline static int s_gameClearPhase_ = 0;
	PhaseState preIsPhase_ = PreparationPhase; // フェーズ切り替わり検知用
	inline static int currentPhase_ = 0;
	inline static float totalBattleTime_ = 0.0f;

	int StartCoinCount_ = 300; // 初期コイン数

	inline static int CoinCount; // コインの数を管理する静的変数

	bool preKeyP_ = false; // 初期化しておく
	bool preKeySpace_ = false;
	bool isPlacementMode_ = false;
	bool isPhaseTransitioning_ = false;
	bool isFadeFinished_ = false;
	bool isTutorialScene_ = false; // シーンがチュートリアルかどうかを保持するフラグ

	std::string selectedObjPath_ = "Resources/Models/cube/cube.obj";
	std::string previewModelPath = "";
	std::string previewObjPath_ = "";
	uint32_t previewModelHandle_ = 0;
	uint32_t previewTextureHandle_ = 0;

	bool isSellMode_ = false;       // 追加: 売却（削除）モード

	int canonCost_ = 100;
	int missileCost_ = 400;
	int poisonCost_ = 200;
	int iceCanonCost_ = 150;

	int selectedObjCost_ = 0; // 追加
	int currentInstallationCost_ = 0; // 現在の設置コスト

	// スキルツリー
	SkillTree skillTree_;
	bool preKeyN_ = false;

	entt::entity enemyCountUI_ = entt::null;
	entt::entity installationCostUI_ = entt::null;
	entt::entity waveCountUI_ = entt::null;

	std::string waveCountFrameTexPath_ = "";
	std::string enemyCountFrameTexPath_ = "";
	std::string timerFrameTexPath_ = "";
	DirectX::XMFLOAT2 waveCountFrameSize_ = {300.0f, 100.0f};
	DirectX::XMFLOAT2 enemyCountFrameSize_ = {300.0f, 100.0f};
	DirectX::XMFLOAT2 timerFrameSize_ = {300.0f, 100.0f};
	entt::entity waveCountFrameUI_ = entt::null;
	entt::entity enemyCountFrameUI_ = entt::null;
	entt::entity timerUI_ = entt::null;
	entt::entity timerFrameUI_ = entt::null;

	// インサートカメラ演出用
	std::vector<CameraWaypoint> insertWaypoints_;
	int currentWaypointIndex_ = 0;
	float waypointTime_ = 0.0f;
	float skipHoldTime_ = 0.0f;
	bool isInsertInitialized_ = false;
	entt::entity skipPromptUI_ = entt::null;
	entt::entity skipProgressUI_ = entt::null;
	entt::entity skipProgressBgUI_ = entt::null;
	DirectX::XMFLOAT3 originalCameraPos_ = {0.0f, 0.0f, 0.0f};
	DirectX::XMFLOAT3 originalCameraRot_ = {0.0f, 0.0f, 0.0f};

	// ゲームオーバー演出用
	float gameOverTimer_ = 0.0f;
	entt::entity resultManagerEntity_ = entt::null;
	DirectX::XMFLOAT3 goStartCamPos_ = {0,0,0};
	DirectX::XMFLOAT3 goStartCamRot_ = {0,0,0};

	// インサートカメラ演出関連のヘルパーメソッド
	void InitializeInsertPhase(GameScene* scene);
	void UpdateInsertPhase(GameScene* scene, float dt);
	void CreateSkipUI(GameScene* scene);
	void UpdateSkipUIProgress(GameScene* scene);
	void EndInsertPhase(GameScene* scene);

	// バトル開始ホールド用
	float battleStartHoldTime_ = 0.0f;
	uint32_t startButtonFrameTextureHandle_ = 0;

	// BGM関連
	uint32_t battleBgmHandle_ = 0;
	uint32_t preparationBgmHandle_ = 0;
	uint32_t resultBgmHandle_ = 0;
	
	// SE関連
	uint32_t installationSeHandle_ = 0;
	uint32_t collapseSeHandle_ = 0;

public:
	inline static size_t currentBgmVoiceHandle_ = 0;
	inline static uint32_t currentBgmAssetHandle_ = 0;
	bool isResultBgmPlaying_ = false;
	static bool isSkillTreeOpen_; // スキルツリーが開いているかどうかを管理する静的変数

	virtual void DrawUI(entt::entity entity, GameScene* scene) override;
};

} // namespace Game
