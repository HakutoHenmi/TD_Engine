#pragma once
#include "IScene.h"
#include <Windows.h>
#include <string>
#include <vector>

// Audioクラスのインクルード
#include "../../Engine/Audio.h"

// XInput関連
#include <Xinput.h>
#pragma comment(lib, "xinput.lib")

// 描画・カメラ関連のヘッダ
#include "Camera.h"
#include "Renderer.h"
#include "Transform.h"

namespace Game {

// シーン内の状態定義
enum class TitleState {
	WaitInput,  // タイトル画面で待機（カメラ回転・魚が降る）
	Zooming,    // 山に向かってズーム中
	SelectStage // 山に接近してステージ選択（UI表示）
};

class TitleScene final : public Engine::IScene {
public:
	~TitleScene();

	void Initialize(Engine::WindowDX* dx) override;
	void Update() override;
	void Draw() override;

	bool IsEnd() const override { return end_; }
	std::string Next() const override { return next_; }

	// 選択されたステージ番号を保持する静的変数
	static int SelectedStageIndex;

	// セレクト画面から開始するかどうかのフラグ
	static bool IsSelectStart;

private:
	bool WasPressed_(int vk) {
		SHORT now = GetAsyncKeyState(vk);
		bool pressed = ((now & 0x8000) != 0) && ((prevKey_[vk] & 0x8000) == 0);
		prevKey_[vk] = now;
		return pressed;
	}

	// パッドのボタンが押された瞬間を判定
	bool WasPadPressed(WORD button) const { return ((currentPadState_.Gamepad.wButtons & button) != 0) && ((prevPadState_.Gamepad.wButtons & button) == 0); }

	struct CloudData {
		Engine::Transform transform;
		float speed = 0.0f;
	};

	struct TreeData {
		Engine::Transform transform;
	};

	struct MountainData {
		Engine::Transform transform;
	};

	struct Particle {
		Engine::Transform transform;
		Engine::Vector3 velocity;
		Engine::Vector4 color;
		float swayPhase;
		float swaySpeed;
		float animTime;
	};

	// ステージごとのカメラ設定を保持する構造体
	struct StageViewSettings {
		float angle;
		float height;
		float distance;
		Engine::Vector3 lookAtOffset;
	};

private:
	bool end_ = false;
	std::string next_{};
	SHORT prevKey_[256]{};

	// コントローラー状態保持用
	XINPUT_STATE currentPadState_{};
	XINPUT_STATE prevPadState_{};

	Engine::Renderer* renderer_ = nullptr;
	Engine::Camera camera_{};

	// 山（Peak）
	Engine::Renderer::MeshHandle peakMesh_ = 0;
	Engine::Renderer::TextureHandle peakTex_ = 0;
	Engine::Transform peakTransform_{};

	// タイトルロゴ
	Engine::Renderer::MeshHandle titleBodyMesh_ = 0;
	Engine::Renderer::TextureHandle titleBodyTex_ = 0;
	Engine::Renderer::MeshHandle titleFutiMesh_ = 0;
	Engine::Renderer::TextureHandle titleFutiTex_ = 0;
	Engine::Transform titleTransform_{};

	// ステージマーカー（11個）
	static const int kStageCount = 11;
	std::vector<Engine::Renderer::MeshHandle> stageMeshes_;
	std::vector<Engine::Transform> stageTransforms_;

	// 背景・環境
	std::vector<MountainData> bgMountains_;
	Engine::Renderer::MeshHandle planeMesh_ = 0;
	Engine::Renderer::TextureHandle planeTex_ = 0;
	Engine::Transform planeTransform_{};

	Engine::Renderer::MeshHandle cloudMesh_ = 0;
	Engine::Renderer::TextureHandle cloudTex_ = 0;
	std::vector<CloudData> clouds_;

	Engine::Renderer::MeshHandle treeMesh_ = 0;
	Engine::Renderer::TextureHandle treeTex_ = 0;
	std::vector<TreeData> trees_;

	// 魚（gltf）関連
	Engine::Renderer::MeshHandle fishMesh_ = 0;
	std::vector<Particle> particles_;

	// アニメーション制御用
	std::vector<Engine::Matrix4x4> fishBonePalette_;

	// UI
	Engine::Renderer::TextureHandle pressTex_ = 0; // moji2.png

	// セレクト画面用UI
	Engine::Renderer::TextureHandle uiSelectMoveTex_ = 0;   // senntakuUI.png
	Engine::Renderer::TextureHandle uiSelectDecideTex_ = 0; // serekutoUI2.png

	// ★修正: 音声ハンドル (データ識別用)
	uint32_t bgmHandle_ = 0;
	uint32_t seDecisionHandle_ = 0;
	uint32_t seSelectHandle_ = 0;

	// ★追加: 再生中のBGMを操作するためのハンドル
	size_t bgmVoiceHandle_ = 0;

	float pressTimer_ = 0.0f;

	// 制御用
	TitleState state_ = TitleState::WaitInput;
	float animTimer_ = 0.0f;
	float zoomTimer_ = 0.0f;
	const float kZoomDuration_ = 1.0f;
	float cameraAngle_ = 0.0f;

	int currentStage_ = 0;
	float currentViewAngle_ = 0.0f;
	float currentViewHeight_ = 0.0f;
	float currentViewDist_ = 0.0f;
	Engine::Vector3 currentLookAt_ = {};

	std::vector<StageViewSettings> stageSettings_;
};

} // namespace Game