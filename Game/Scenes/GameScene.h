#pragma once

#include <Windows.h>
#include <Xinput.h> // ゲームパッド用
#include <string>
#include <vector>

// ★Audioクラスのインクルード
#include "../../Engine/Audio.h"

#include "AABB.h"
#include "Camera.h"
#include "IScene.h"
#include "Renderer.h"
#include "Transform.h"
#include "WindowDX.h"

#include "Actors/World.h"
#include "PlayerBall.h"

// パーティクルシステム
#include "Particle.h"

// ゲーム固有の型定義を読み込む
#include "../ObjectTypes.h"

#pragma comment(lib, "xinput.lib")

namespace Game {

enum class GizmoAxis { None, X, Y, Z };

enum class CameraDirection { Front, Back, Left, Right, Top, Bottom };

// シーンの状態
enum class SceneState {
	Play,     // 通常プレイ中
	ClearAnim // クリア演出中
};

class GameScene final : public Engine::IScene {
public:
	void Initialize(Engine::WindowDX* dx) override;
	void Update() override;
	void Draw() override;

	bool IsEnd() const override { return end_; }
	std::string Next() const override { return next_; }

	World* GetWorld() { return &world_; }

	void SpawnBlockAtNDC(Game::ObjectType type, float ndcX, float ndcY);
	void SpawnModelAtNDC(const std::string& filename, float ndcX, float ndcY);

	void EditorUpdate(bool isHovered, float ndcX, float ndcY);

	bool IsEditorMode() const { return isEditorMode_; }

	// 最後に選択されたオブジェクトを取得
	Engine::GameObject* GetActiveObject() const;
	// 指定オブジェクトが選択されているか
	bool IsSelected(Engine::GameObject* obj) const;
	// オブジェクトを選択に追加/排他選択
	void SelectObject(Engine::GameObject* obj, bool additive);
	// 選択解除
	void ClearSelection();
	// 選択から除外
	void RemoveFromSelection(Engine::GameObject* obj);

	const Engine::Camera& GetCamera() const { return camera_; }
	void SetCameraDirection(CameraDirection dir);

	void ReSpawnParticle();

private:
	// キー入力判定
	bool WasPressed_(int vk);
	// パッドボタン判定
	bool WasButtonPressed_(WORD button);

private:
	Engine::WindowDX* dx_ = nullptr;
	Engine::Renderer* renderer_ = nullptr;

	Engine::Camera camera_;
	World world_;

	// メッシュハンドル
	Engine::Renderer::MeshHandle ballMesh_ = 0;
	// 歩行アニメーションモデル
	Engine::Renderer::MeshHandle walkModelHandle_ = 0;

	// アニメーション制御用
	std::vector<Engine::Matrix4x4> bonePalette_;
	float animationTime_ = 0.0f;

	// キャラの表示用座標
	Engine::Vector3 charPos_ = {0, 0, 0};
	float charRotateY_ = 0.0f;
	float charSpeed_ = 0.08f;

	// テクスチャハンドル
	Engine::Renderer::TextureHandle ballTex_ = 0;
	Engine::Renderer::TextureHandle uvTex_ = 0;
	Engine::Renderer::TextureHandle whiteTex_ = 0;

	// アザラシ用テクスチャハンドル
	Engine::Renderer::TextureHandle azarasiTex_ = 0;

	Engine::Renderer::TextureHandle clearMojiTex_ = 0;

	// ポーズUI
	Engine::Renderer::TextureHandle uiPauseTex_ = 0;

	// ★追加: ポーズメニュー用テクスチャ (上・中・下)
	Engine::Renderer::TextureHandle uiPauseGameTex_ = 0;   // ge-munimodoru.png
	Engine::Renderer::TextureHandle uiPauseSelectTex_ = 0; // serekutonimodoru.png
	Engine::Renderer::TextureHandle uiPauseTitleTex_ = 0;  // taitorunimodoru.png

	// ★音声データハンドル (ロードした音声を識別するID)
	uint32_t bgmHandle_ = 0;      // BGM2.mp3
	uint32_t seMoveHandle_ = 0;   // idou.mp3
	uint32_t seSelectHandle_ = 0; // serekutosenntaku.mp3
	uint32_t seDecideHandle_ = 0; // kettei.mp3

	// ★再生中のボイスハンドル（停止操作に必要）
	size_t bgmVoiceHandle_ = 0;      // BGM停止用
	size_t movingVoiceId_ = 0;       // 移動音停止用
	bool isMovingSePlaying_ = false; // 移動音が現在鳴っているかのフラグ

	PlayerBall player_{};

	float camHeight_ = 8.0f;
	float camBack_ = 10.0f;
	float camPitch_ = 0.35f;
	// smooth yaw state for camera following player direction
	float camYaw_ = 0.0f;
	float camYawVel_ = 0.0f;

	SHORT prevKey_[256]{};

	// ★パッドの前フレーム状態
	XINPUT_STATE prevPadState_{};
	// ★現在のパッド状態
	XINPUT_STATE currentPadState_{};

	bool end_ = false;
	std::string next_{};

	bool isEditorMode_ = true;
	Engine::Vector3 debugCamPos_{0, 10, -20};
	Engine::Vector3 debugCamRot_{0.5f, 0, 0};

	// Camera look-ahead state
	Engine::Vector3 camLookAhead_ = {0.0f, 0.0f, 0.0f};
	float lookAheadMax_ = 8.0f;
	float lookAheadApproachLerp_ = 0.18f;
	float lookAheadReturnLerp_ = 0.06f;

	// 選択・編集関連
	std::vector<Engine::GameObject*> selectedObjects_;
	Engine::GameObject* draggingObject_ = nullptr;
	Engine::Vector3 dragOffset_{};

	float dragPlaneY_ = 0.0f;
	GizmoAxis hoverAxis_ = GizmoAxis::None;
	GizmoAxis draggingAxis_ = GizmoAxis::None;

	float axisDragOffset_ = 0.0f;

	// 演出管理用
	SceneState state_ = SceneState::Play;
	float clearAnimTimer_ = 0.0f;        // 演出経過時間
	Engine::Vector3 clearAnimBasePos_{}; // アザラシが降り立った位置
	float clearAnimCamAngle_ = 0.0f;     // 演出用カメラ回転角

	// 各種パーティクルシステム
	Engine::ParticleSystem confettiParticles_;
	float confettiSpawnTimer_ = 0.0f;
	Engine::ParticleSystem hitParticles_;
	Engine::ParticleSystem speedLineParticles_;
	Engine::ParticleSystem dustParticles_;
	Engine::ParticleSystem respawnParticles_;

	// プレイヤーの初期位置設定済みか
	bool isPlayerPositioned_ = false;

	// ポーズ関連
	bool isPaused_ = false;
	int pauseCursor_ = 0; // 0:Game, 1:Select, 2:Title

	// ★遷移待機用の変数（ボタン離し待ち）
	bool isWaitingForRelease_ = false;
	std::string pendingNext_ = "";
	bool pendingSelectStart_ = false;
	bool kiete_ = false;
	float kieteTime_;
};

} // namespace Game